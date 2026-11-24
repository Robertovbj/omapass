#include "browserintegration.h"

#include <QCryptographicHash>

#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QtConcurrent>

#include "browsermanifests.h"
#include "browserstore.h"
#include "i18n.h"
#include "totp.h"

namespace {

// Where a login saved from the browser goes when the extension names no
// group: KeePassXC's own default, so both apps put them in the same place.
const QString defaultGroup = QStringLiteral("KeePassXC-Browser Passwords");

QString lastSegment(const QString &path) {
    return path.section(QLatin1Char('/'), -1);
}

// The URLs to match an entry by: its own and the additional ones. Without
// any known, an entry named like a host ("Web/github.com") stands for that
// site: the convention pass users follow (browserpass, passff), and pass
// cannot list URLs without decrypting every entry.
QStringList matchUrlsOf(const QString &entry, const EntryMeta &meta) {
    QStringList urls;
    if (!meta.url.trimmed().isEmpty())
        urls.append(meta.url);
    urls.append(meta.extraUrls);
    if (!urls.isEmpty())
        return urls;
    const QString name = lastSegment(entry);
    if (name.contains(QLatin1Char('.')) && !name.contains(QLatin1Char(' ')))
        urls.append(name);
    return urls;
}

bool isVaultPrompt(const QVariantMap &prompt) {
    return prompt.value(QStringLiteral("kind")).toString() != QLatin1String("conflict");
}

}

BrowserIntegration::BrowserIntegration(BrowserApp *app, const QString &localDir, QObject *parent)
    : QObject(parent), m_app(app), m_localDir(localDir), m_server(this) {}

BrowserIntegration::~BrowserIntegration() {
    waitForReads();
    m_server.stop();
}

bool BrowserIntegration::supported() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool BrowserIntegration::running() const {
    return m_server.running();
}

// ---------------------------------------------------------------- lifecycle

void BrowserIntegration::setEnabled(bool enabled, bool quiet) {
    if (!supported())
        return;
    m_enabled = enabled;
    const QString proxy = BrowserManifests::proxyPath();

    if (!enabled) {
        // A question about the manifests no longer applies.
        for (int i = m_prompts.size() - 1; i >= 0; --i) {
            if (!isVaultPrompt(m_prompts.at(i).shown))
                m_prompts.removeAt(i);
        }
        if (!m_prompt.isEmpty() && !isVaultPrompt(m_prompt)) {
            m_prompt.clear();
            emit promptChanged();
            showNextPrompt();
        }
        const bool wasRunning = m_server.running();
        m_server.stop();
        BrowserManifests::uninstall(QDir::homePath(), proxy);
        if (wasRunning)
            emit runningChanged();
        return;
    }

    if (!m_server.running()) {
        QString error;
        if (!m_server.start(&error)) {
            emit message(error == QLatin1String("busy")
                             ? I18n::t(QStringLiteral("browser.busy"))
                             : I18n::t(QStringLiteral("browser.start_error"), QStringLiteral("err"), error),
                         true);
            return;
        }
        emit runningChanged();
    }
    install(false, quiet);
}

void BrowserIntegration::install(bool replace, bool quiet) {
    const BrowserManifests::Result result =
        BrowserManifests::install(QDir::homePath(), BrowserManifests::proxyPath(), replace);
    if (!result.failed.isEmpty())
        emit message(I18n::t(QStringLiteral("browser.manifest_error"), QStringLiteral("browsers"),
                             result.failed.join(QStringLiteral(", "))),
                     true);
    if (!result.conflicts.isEmpty()) {
        ask({{QStringLiteral("kind"), QStringLiteral("conflict")},
             {QStringLiteral("browsers"), result.conflicts}},
            [this, quiet](const QVariant &answer) {
                if (answer.toBool() && m_enabled)
                    install(true, quiet);
            });
        return;
    }
    if (quiet || !result.failed.isEmpty())
        return;
    if (result.installed.isEmpty())
        emit message(I18n::t(QStringLiteral("browser.no_browsers")), true);
    else
        emit message(I18n::t(QStringLiteral("browser.enabled"), QStringLiteral("browsers"),
                             result.installed.join(QStringLiteral(", "))),
                     false);
}

void BrowserIntegration::vaultChanged() {
    ++m_generation;
    const auto open = m_app->browserVaults();
    m_hashes.removeIf([&](QHash<const Vault *, QString>::iterator it) { return !open.contains(it.key()); });
    for (const auto *vault : open) {
        if (!vault || m_hashes.contains(vault))
            continue;
        QString hash = BrowserStore(vault, m_localDir).databaseHash();
        // A copy of a KDBX retains its root UUID. Keep the first file's
        // native hash and distinguish other open copies by path.
        if (m_hashes.values().contains(hash))
            hash = QString::fromLatin1(QCryptographicHash::hash(hash.toLatin1() + vault->path().toUtf8(),
                                       QCryptographicHash::Sha256).toHex());
        m_hashes.insert(vault, hash);
    }
    m_handedOut.removeIf([this](QHash<QString, QSet<QString>>::iterator it) { return !m_hashes.values().contains(it.key()); });
    m_pendingGroups.clear();
    m_store.reset();
    m_storeVault = nullptr;
    dropPrompts();
    if (m_server.running())
        m_server.broadcast({{QStringLiteral("action"), m_app->browserVault()
                                                           ? QStringLiteral("database-unlocked")
                                                           : QStringLiteral("database-locked")}});
}

void BrowserIntegration::waitForReads() {
    for (QFuture<void> &read : m_reads)
        read.waitForFinished();
    m_reads.clear();
}

void BrowserIntegration::entryMoved(const QString &from, const QString &to) {
    if (BrowserStore *s = store())
        s->moveEntry(from, to);
}

void BrowserIntegration::entryRemoved(const QString &path) {
    if (BrowserStore *s = store())
        s->removeEntry(path);
}

BrowserStore *BrowserIntegration::store() const {
    const Vault *vault = m_app->browserVault();
    if (!vault)
        return nullptr;
    if (vault != m_storeVault || !m_store) {
        m_store = std::make_unique<BrowserStore>(vault, m_localDir);
        m_storeVault = vault;
    }
    return m_store.get();
}

QString BrowserIntegration::pathOfUuid(const Vault *vault, const QString &uuid) const {
    if (!vault)
        return QString();
    const QHash<QString, QString> uuids = BrowserStore(vault, m_localDir).entryUuids(m_app->entriesFor(vault));
    for (auto it = uuids.cbegin(); it != uuids.cend(); ++it) {
        if (it.value().compare(uuid, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    return QString();
}

// ---------------------------------------------------------------- prompts

void BrowserIntegration::ask(const QVariantMap &shown, std::function<void(const QVariant &)> answer) {
    m_prompts.append({shown, std::move(answer)});
    if (m_prompt.isEmpty())
        showNextPrompt();
}

void BrowserIntegration::showNextPrompt() {
    if (!m_prompt.isEmpty() || m_prompts.isEmpty())
        return;
    m_prompt = m_prompts.first().shown;
    emit promptChanged();
    emit raiseRequested();
}

void BrowserIntegration::answer(const QVariant &value) {
    if (m_prompt.isEmpty() || m_prompts.isEmpty())
        return;
    const Prompt current = m_prompts.takeFirst();
    m_prompt.clear();
    emit promptChanged();
    current.answer(value);
    showNextPrompt();
}

void BrowserIntegration::dropPrompts() {
    QList<Prompt> dropped;
    for (int i = m_prompts.size() - 1; i >= 0; --i) {
        if (isVaultPrompt(m_prompts.at(i).shown))
            dropped.prepend(m_prompts.takeAt(i));
    }
    if (!m_prompt.isEmpty() && isVaultPrompt(m_prompt)) {
        m_prompt.clear();
        emit promptChanged();
    }
    // Refused, so the browser hears an answer instead of waiting forever.
    for (const Prompt &prompt : std::as_const(dropped))
        prompt.answer(QVariant());
    showNextPrompt();
}

void BrowserIntegration::answerAssociate(const QString &name) {
    answer(name.trimmed());
}

bool BrowserIntegration::associationExists(const QString &name) const {
    BrowserStore *s = store();
    return s && s->associations().contains(name.trimmed());
}

void BrowserIntegration::answerAccess(const QStringList &allowed, bool remember) {
    answer(QVariantMap{{QStringLiteral("allowed"), allowed}, {QStringLiteral("remember"), remember}});
}

void BrowserIntegration::answerUpdate(bool accepted) {
    answer(accepted);
}

void BrowserIntegration::answerConflict(bool replace) {
    answer(replace);
}

// ---------------------------------------------------------------- BrowserHost

bool BrowserIntegration::unlocked() const {
    return m_app->browserVault() != nullptr;
}

void BrowserIntegration::showUnlock() {
    emit raiseRequested();
}

QString BrowserIntegration::databaseHash() const {
    return hashForVault(m_app->browserVault());
}

QString BrowserIntegration::hashForVault(const Vault *vault) const {
    if (!vault)
        return {};
    const auto it = m_hashes.constFind(vault);
    return it == m_hashes.cend() ? BrowserStore(vault, m_localDir).databaseHash() : it.value();
}

QStringList BrowserIntegration::databaseHashes() const {
    QStringList hashes;
    for (const auto *vault : m_app->browserVaults())
        if (vault)
            hashes.append(hashForVault(vault));
    hashes.removeDuplicates();
    return hashes;
}

const Vault *BrowserIntegration::vaultByHash(const QString &hash) const {
    for (const auto *vault : m_app->browserVaults())
        if (vault && hashForVault(vault) == hash)
            return vault;
    return nullptr;
}

QString BrowserIntegration::keyForDatabase(const QString &hash, const QString &id) const {
    const auto *vault = vaultByHash(hash);
    return vault ? BrowserStore(vault, m_localDir).key(id) : QString();
}

QString BrowserIntegration::associationKey(const QString &id) const {
    BrowserStore *s = store();
    return s ? s->key(id) : QString();
}

void BrowserIntegration::associate(const QString &idKey, std::function<void(const QString &)> done) {
    const Vault *vault = m_app->browserVault();
    if (!vault) {
        done(QString());
        return;
    }
    const quint64 generation = m_generation;
    ask({{QStringLiteral("kind"), QStringLiteral("associate")},
         {QStringLiteral("vault"), Vault::displayName({vault->path(), vault->kind()})}},
        [this, generation, idKey, done](const QVariant &answer) {
            const QString name = answer.toString();
            BrowserStore *s = store();
            if (name.isEmpty() || generation != m_generation || !s) {
                done(QString());
                return;
            }
            QString error;
            if (!s->setKey(name, idKey, &error)) {
                emit message(I18n::t(QStringLiteral("browser.save_error"), QStringLiteral("err"), error), true);
                done(QString());
                return;
            }
            emit message(I18n::t(QStringLiteral("browser.associated"), QStringLiteral("name"), name), false);
            done(name);
        });
}

void BrowserIntegration::findLogins(const BrowserLoginQuery &query,
                                    std::function<void(const QVector<BrowserLogin> &)> done) {
    const QStringList hashes = query.databases.isEmpty() ? QStringList{databaseHash()} : query.databases;
    struct Results {
        int pending;
        QVector<BrowserLogin> logins;
    };
    auto results = std::make_shared<Results>(Results{int(hashes.size()), {}});
    const auto generation = m_generation;
    for (const auto &hash : hashes) {
        findVaultLogins(vaultByHash(hash), query, [this, results, done, generation](const QVector<BrowserLogin> &items) {
            results->logins.append(items);
            if (--results->pending == 0)
                done(generation == m_generation ? results->logins : QVector<BrowserLogin>{});
        });
    }
}

void BrowserIntegration::findVaultLogins(const Vault *vault, const BrowserLoginQuery &query,
                                         std::function<void(const QVector<BrowserLogin> &)> done) {
    if (!vault) {
        done({});
        return;
    }
    BrowserStore db(vault, m_localDir);
    const QString site = BrowserProtocol::hostOf(query.url);
    QString form = BrowserProtocol::hostOf(query.submitUrl);
    if (form == site)
        form.clear();
    if (site.isEmpty()) {
        done({});
        return;
    }

    const QHash<QString, EntryMeta> meta = m_app->metaFor(vault);
    const QHash<QString, BrowserStore::Access> access = db.access();
    QStringList allowed;
    QStringList unknown;
    for (const QString &entry : m_app->entriesFor(vault)) {
        const EntryMeta info = meta.value(entry);
        if (info.type != QLatin1String("login"))
            continue;
        const QStringList urls = matchUrlsOf(entry, info);
        if (std::none_of(urls.cbegin(), urls.cend(),
                         [&query](const QString &url) { return BrowserProtocol::urlMatches(url, query.url); }))
            continue;
        const BrowserStore::Access rules = access.value(entry);
        if (rules.deny.contains(site) || (!form.isEmpty() && rules.deny.contains(form)))
            continue;
        if (rules.allow.contains(site) && (form.isEmpty() || rules.allow.contains(form)))
            allowed.append(entry);
        else
            unknown.append(entry);
    }

    if (unknown.isEmpty()) {
        deliver(vault, allowed, done);
        return;
    }

    QVariantList shownEntries;
    for (const QString &entry : std::as_const(unknown))
        shownEntries.append(QVariantMap{
            {QStringLiteral("path"), entry},
            {QStringLiteral("name"), lastSegment(entry)},
            {QStringLiteral("login"), meta.value(entry).username}});

    const quint64 generation = m_generation;
    ask({{QStringLiteral("kind"), QStringLiteral("access")},
         {QStringLiteral("vault"), Vault::displayName({vault->path(), vault->kind()})},
         {QStringLiteral("site"), site},
         {QStringLiteral("entries"), shownEntries}},
        [this, vault, generation, allowed, unknown, site, form, done](const QVariant &answer) {
            if (generation != m_generation) {
                done({});
                return;
            }
            const QVariantMap reply = answer.toMap();
            QStringList picked;
            for (const QString &path : reply.value(QStringLiteral("allowed")).toStringList()) {
                if (unknown.contains(path))
                    picked.append(path);
            }

            BrowserStore db(vault, m_localDir);
            if (reply.value(QStringLiteral("remember")).toBool()) {
                const QHash<QString, BrowserStore::Access> access = db.access();
                QString error;
                for (const QString &entry : unknown) {
                    BrowserStore::Access rules = access.value(entry);
                    QStringList &add = picked.contains(entry) ? rules.allow : rules.deny;
                    QStringList &drop = picked.contains(entry) ? rules.deny : rules.allow;
                    for (const QString &host : {site, form}) {
                        if (host.isEmpty())
                            continue;
                        drop.removeAll(host);
                        if (!add.contains(host))
                            add.append(host);
                    }
                    if (!db.setAccess(entry, rules, &error)) {
                        emit message(I18n::t(QStringLiteral("browser.save_error"), QStringLiteral("err"), error),
                                     true);
                        break;
                    }
                }
            }
            deliver(vault, allowed + picked, done);
        });
}

void BrowserIntegration::deliver(const Vault *vault, const QStringList &paths,
                                 std::function<void(const QVector<BrowserLogin> &)> done) {
    if (paths.isEmpty() || !vault) {
        done({});
        return;
    }

    BrowserStore db(vault, m_localDir);
    const QString hash = hashForVault(vault);
    const QHash<QString, QString> uuids = db.entryUuids(paths);
    QHash<QString, QString> names;
    for (const QString &path : paths)
        names.insert(path, vault->titleFor(path, lastSegment(path)));

    // Reading can mean a process per entry (pass, 1Password): never here.
    const QFuture<QVector<BrowserLogin>> read = QtConcurrent::run([vault, paths, uuids, names, hash]() {
        QVector<BrowserLogin> logins;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        for (const QString &path : paths) {
            EntryData data;
            QString error;
            if (!vault->fetchEntry(path, &data, &error))
                continue;
            BrowserLogin login;
            login.databaseHash = hash;
            login.uuid = uuids.value(path);
            login.name = names.value(path);
            login.login = data.username;
            login.password = data.password;
            login.group = lastSegment(parentGroup(path));
            if (!data.otp.isEmpty()) {
                if (const auto params = Totp::parse(data.otp.toString()))
                    login.totp = Totp::code(*params, now);
            }
            logins.append(login);
        }
        return logins;
    });
    m_reads.append(QFuture<void>(read));

    const quint64 generation = m_generation;
    auto *watcher = new QFutureWatcher<QVector<BrowserLogin>>(this);
    connect(watcher, &QFutureWatcher<QVector<BrowserLogin>>::finished, this,
            [this, watcher, generation, done]() {
        const QVector<BrowserLogin> logins = watcher->result();
        watcher->deleteLater();
        // A finished read is one less to wait for when the vault goes.
        for (int i = m_reads.size() - 1; i >= 0; --i) {
            if (m_reads.at(i).isFinished())
                m_reads.removeAt(i);
        }
        if (generation != m_generation) {
            done({});
            return;
        }
        for (const BrowserLogin &login : logins)
            m_handedOut[login.databaseHash].insert(login.uuid.toLower());
        done(logins);
    });
    watcher->setFuture(read);
}

void BrowserIntegration::totp(const QString &uuid, std::function<void(const QString &)> done) {
    databaseTotp(databaseHash(), uuid, done);
}

void BrowserIntegration::databaseTotp(const QString &hash, const QString &uuid, std::function<void(const QString &)> done) {
    const Vault *vault = vaultByHash(hash);
    // Only for an entry this browser was already given: get-totp names no
    // site, so there is nothing else to check it against.
    const QString path = m_handedOut.value(hash).contains(uuid.toLower()) ? pathOfUuid(vault, uuid) : QString();
    if (path.isEmpty()) {
        done(QString());
        return;
    }
    deliver(vault, {path}, [done](const QVector<BrowserLogin> &logins) {
        done(logins.isEmpty() ? QString() : logins.first().totp);
    });
}

void BrowserIntegration::saveLogin(const BrowserSaveRequest &request, std::function<void(bool)> done) {
    const Vault *target = request.databaseHash.isEmpty() ? m_app->browserVault() : vaultByHash(request.databaseHash);
    if (!target) {
        done(false);
        return;
    }
    BrowserStore db(target, m_localDir);
    const QString site = BrowserProtocol::hostOf(request.url);
    const QString form = BrowserProtocol::hostOf(request.submitUrl);
    const QString login = request.login;
    const Secret password = request.password;
    const quint64 generation = m_generation;

    if (!request.uuid.isEmpty()) {
        const QString path = pathOfUuid(target, request.uuid);
        if (path.isEmpty()) {
            done(false);
            return;
        }
        ask({{QStringLiteral("kind"), QStringLiteral("update")},
             {QStringLiteral("vault"), Vault::displayName({target->path(), target->kind()})},
             {QStringLiteral("name"), lastSegment(path)},
             {QStringLiteral("login"), login},
             {QStringLiteral("site"), site}},
            [this, generation, target, path, login, password, done](const QVariant &answer) {
                if (!answer.toBool() || generation != m_generation) {
                    done(false);
                    return;
                }
                const bool started = m_app->browserChange(target,
                    [path, login, password](const Vault *vault, QString *error) {
                        EntryData data;
                        if (!vault->fetchEntry(path, &data, error))
                            return false;
                        data.username = login;
                        data.password = password;
                        return vault->editEntry(path, path, parentGroup(path), data, error);
                    },
                    done);
                if (!started)
                    done(false);
            });
        return;
    }

    // The group the extension picked, by id; else by name; else KeePassXC's.
    QStringList groups;
    if (target == m_app->browserVault()) {
        groups = m_app->browserGroups();
    } else {
        QStringList entries;
        target->list(&entries, &groups);
    }
    QString group;
    bool found = false;
    if (!request.groupUuid.isEmpty()) {
        if (target == m_app->browserVault() && m_pendingGroups.contains(request.groupUuid)) {
            group = m_pendingGroups.value(request.groupUuid);
            found = true;
        } else {
            const QHash<QString, QString> ids = db.groupUuids(groups);
            for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
                if (it.value().compare(request.groupUuid, Qt::CaseInsensitive) == 0) {
                    group = it.key();
                    found = true;
                    break;
                }
            }
            if (!found && request.groupUuid.compare(target->databaseUuid(), Qt::CaseInsensitive) == 0)
                found = true;
        }
    }
    if (!found)
        group = request.group.trimmed().isEmpty() ? defaultGroup : request.group.trimmed();

    // Titled after the site, like KeePassXC; a second login for it gets a
    // number rather than replacing the first.
    const QString base = site.isEmpty() ? request.url : site;
    const QStringList entries = m_app->entriesFor(target);
    const auto pathFor = [&group](const QString &title) {
        return group.isEmpty() ? title : group + QLatin1Char('/') + title;
    };
    QString title = base;
    for (int n = 2; entries.contains(pathFor(title)); ++n)
        title = QStringLiteral("%1 (%2)").arg(base).arg(n);
    const QString path = pathFor(title);

    EntryData data;
    data.username = login;
    data.password = password;
    data.url = request.url;

    const bool started = m_app->browserChange(target,
        [path, group, data](const Vault *vault, QString *error) {
            return vault->addEntry(path, group, data, error);
        },
        [this, generation, target, path, site, form, done](bool saved) {
            // The site that saved it may have it back without asking.
            if (saved && generation == m_generation) {
                BrowserStore store(target, m_localDir);
                BrowserStore::Access rules;
                rules.allow.append(site);
                if (!form.isEmpty() && form != site)
                    rules.allow.append(form);
                QString error;
                store.setAccess(path, rules, &error);
            }
            done(saved);
        });
    if (!started)
        done(false);
}

QJsonArray BrowserIntegration::groups() const {
    const Vault *vault = m_app->browserVault();
    BrowserStore *s = store();
    if (!vault || !s)
        return {};

    // KeePass lists its recycle bin as a group; the ids it has leave it out.
    QStringList paths = m_app->browserGroups();
    const QHash<QString, QString> real = vault->groupUuids();
    if (!real.isEmpty()) {
        paths.erase(std::remove_if(paths.begin(), paths.end(),
                                   [&real](const QString &path) { return !real.contains(path); }),
                    paths.end());
    }
    for (auto it = m_pendingGroups.cbegin(); it != m_pendingGroups.cend(); ++it) {
        if (!paths.contains(it.value()))
            paths.append(it.value());
    }
    paths.sort();
    const QHash<QString, QString> ids = s->groupUuids(paths);

    const std::function<QJsonArray(const QString &)> childrenOf = [&](const QString &parent) {
        QJsonArray children;
        for (const QString &path : std::as_const(paths)) {
            if (parentGroup(path) != parent)
                continue;
            const QString id = m_pendingGroups.key(path, ids.value(path));
            children.append(QJsonObject{{QStringLiteral("name"), lastSegment(path)},
                                        {QStringLiteral("uuid"), id},
                                        {QStringLiteral("children"), childrenOf(path)}});
        }
        return children;
    };

    QString rootId = vault->databaseUuid();
    if (rootId.isEmpty())
        rootId = s->groupUuids({QString()}).value(QString());
    return {QJsonObject{{QStringLiteral("name"), Vault::displayName({vault->path(), vault->kind()})},
                        {QStringLiteral("uuid"), rootId},
                        {QStringLiteral("children"), childrenOf(QString())}}};
}

void BrowserIntegration::createGroup(const QString &path,
                                     std::function<void(const QString &, const QString &)> done) {
    BrowserStore *s = store();
    QString wanted = path.trimmed();
    while (wanted.startsWith(QLatin1Char('/')))
        wanted.remove(0, 1);
    while (wanted.endsWith(QLatin1Char('/')))
        wanted.chop(1);
    if (!s || wanted.isEmpty()) {
        done(QString(), QString());
        return;
    }
    const QString id = s->groupUuids({wanted}).value(wanted);
    // A group exists here only with something in it, so a new one is only
    // remembered until a login is saved into it.
    if (!m_app->browserGroups().contains(wanted))
        m_pendingGroups.insert(id, wanted);
    done(lastSegment(wanted), id);
}

Secret BrowserIntegration::generatePassword() {
    return m_app->browserGeneratePassword();
}

void BrowserIntegration::lock() {
    m_app->browserLock();
}
