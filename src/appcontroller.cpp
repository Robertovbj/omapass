#include "appcontroller.h"

#include "bitwardenvault.h"
#include "pin.h"
#include "clipboard.h"
#include "filter.h"
#include "generator.h"
#include "i18n.h"
#include "iconchoice.h"
#include "onepasswordvault.h"
#include "passstore.h"
#include "sshaskpass.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFutureWatcher>
#include <QRect>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>

#include <tuple>
#include <utility>

namespace {

// Como o timer só precisa perceber o estouro do timeout dentro de uma folga
// perceptível (o padrão é de minutos), não há necessidade de checar a cada
// segundo — 5s mantém o custo irrelevante sem atrasar visivelmente o lock.
constexpr int lockCheckIntervalMs = 5000;

// How long an account check may take before the interface says it is
// waiting. Reading a local file answers in a millisecond, and a sheet shown
// and taken away in that time reads as a glitch; a check that has to run
// `op` takes seconds, and then silence reads as a dead keypress.
constexpr int accountCheckNoticeMs = 200;

const auto windowGeometrySetting = QStringLiteral("window/geometry");
// The Bitwarden server last logged into, which the next login sheet offers.
const auto lastServerSetting = QStringLiteral("bitwarden/lastServer");
const auto registeredKeepassSetting = QStringLiteral("registeredDatabases/kdbx");
const auto registeredPassSetting = QStringLiteral("registeredDatabases/pass");

QString localPathFromInput(const QString &pathOrUrl) {
    QString trimmed = pathOrUrl.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QUrl url(trimmed);
    if (url.isValid() && url.isLocalFile())
        trimmed = url.toLocalFile();
    if (trimmed.startsWith(QStringLiteral("~/")))
        trimmed = QDir::homePath() + trimmed.mid(1);
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

QVector<DbRef> loadRegisteredDatabases() {
    QSettings settings;
    QVector<DbRef> refs;
    for (const QString &path : settings.value(registeredKeepassSetting).toStringList()) {
        if (!path.isEmpty())
            refs.append({path, VaultKind::Keepass});
    }
    for (const QString &path : settings.value(registeredPassSetting).toStringList()) {
        if (!path.isEmpty())
            refs.append({path, VaultKind::Pass});
    }
    return refs;
}

void registerLocalDatabase(const QString &path, VaultKind kind) {
    if (path.isEmpty())
        return;
    const QString key = kind == VaultKind::Pass ? registeredPassSetting : registeredKeepassSetting;
    QSettings settings;
    QStringList paths = settings.value(key).toStringList();
    if (!paths.contains(path)) {
        paths.append(path);
        settings.setValue(key, paths);
    }
}

// Outcome of a backend call run in the background.
struct TaskResult {
    bool ok = false;
    QString error;
};

struct OpenResult {
    Vault *vault = nullptr;
    QString error;
};

QString sshSettingsKey(const Vault *vault, const SshKeyConfig &key) {
    const QByteArray id = QByteArray::number(int(vault->kind())) + '\0'
        + vault->path().toUtf8() + '\0' + key.id.toUtf8();
    return QStringLiteral("sshAgent/")
        + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex());
}

SshKeyOptions sshOptions(const Vault *vault, const SshKeyConfig &key) {
    QSettings settings;
    settings.beginGroup(sshSettingsKey(vault, key));
    return {settings.value(QStringLiteral("addOnUnlock"), key.defaults.addOnUnlock).toBool(),
            settings.value(QStringLiteral("confirm"), key.defaults.confirm).toBool(),
            settings.value(QStringLiteral("lifetimeSeconds"), key.defaults.lifetimeSeconds).toInt()};
}

QVariantMap describe(const DbRef &ref) {
    // `label` is what the list shows: the file's own path for a database on
    // disk, which says which one it is, and the account's name for the
    // backends whose path is an internal identifier ("1password:my").
    const bool account = ref.kind == VaultKind::Bitwarden || ref.kind == VaultKind::OnePassword;
    return {{QStringLiteral("path"), ref.path},
            {QStringLiteral("name"), Vault::displayName(ref)},
            {QStringLiteral("label"), account ? Vault::displayName(ref) : ref.path},
            {QStringLiteral("kind"), Vault::kindLabel(ref.kind)}};
}

}

AppController::AppController(const AppConfig &config, QObject *parent)
    : QObject(parent), m_config(config), m_history(config.recencyEnabled),
      m_siteIcons(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                  + QStringLiteral("/omapass/icons")),
      m_entryIcons(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                   + QStringLiteral("/omapass/icons")),
      m_browser(this, QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                          + QStringLiteral("/omapass/browser")) {
    m_siteIcons.setDuckDuckGo(m_config.siteIconsDuckDuckGo);
    // An icon that arrives goes into every row of its site, wherever the
    // list is filtered to.
    connect(&m_siteIcons, &SiteIcons::iconReady, this, [this](const QString &origin) {
        for (int index = 0; index < m_allRows.size(); ++index) {
            QVariantMap row = m_allRows.at(index).toMap();
            if (row.value(QStringLiteral("origin")).toString() != origin)
                continue;
            applyIcon(&row, row.value(QStringLiteral("path")).toString(),
                      row.value(QStringLiteral("url")).toString());
            m_allRows[index] = row;
            updateShownRow(index);
        }
    });
    m_messageTimer.setInterval(1000);
    connect(&m_messageTimer, &QTimer::timeout, this, [this]() {
        if (m_clipboardCountdown > 0) {
            m_clipboardCountdown -= 1;
            if (m_clipboardCountdown > 0) {
                emit messageChanged();
                return;
            }
        }
        clearMessage();
    });

    connect(&m_bitwardenLogin, &BitwardenLogin::promptShown, this, [this](BwPrompt prompt) {
        setBusy(false);
        setUnlockError(QString());
        switch (prompt) {
        case BwPrompt::TwoFactorMethod:
            setLoginStep(QStringLiteral("method"));
            break;
        case BwPrompt::TwoFactorCode:
            setLoginStep(QStringLiteral("code"));
            break;
        case BwPrompt::NewDeviceCode:
            setLoginStep(QStringLiteral("deviceCode"));
            break;
        case BwPrompt::None:
            break;
        }
    });
    connect(&m_bitwardenLogin, &BitwardenLogin::succeeded, this, [this](BitwardenVault *vault) {
        setBusy(false);
        setLoginStep(QString());
        QSettings().setValue(lastServerSetting, BwAccount::serverOf(vault->path()));

        // The login already pulled and opened the vault: it goes straight
        // in, and into the list for next time.
        refreshDatabases();
        emit databaseCreated();
        adoptVault({vault->path(), VaultKind::Bitwarden}, vault);
    });
    connect(&m_bitwardenLogin, &BitwardenLogin::failed, this,
            [this](const QString &error, BwLoginError kind) {
        setBusy(false);

        switch (kind) {
        case BwLoginError::WrongPassword:
            setUnlockError(I18n::t(QStringLiteral("backend.wrong_password")));
            break;
        case BwLoginError::InvalidEmail:
            setUnlockError(I18n::t(QStringLiteral("bitwarden.invalid_email")));
            break;
        case BwLoginError::InvalidCode:
            // The login is still waiting on the code: another try goes to
            // the same step, without typing the password again.
            setUnlockError(I18n::t(QStringLiteral("bitwarden.invalid_code")));
            return;
        case BwLoginError::ServerUnreachable:
            setUnlockError(I18n::t(QStringLiteral("bitwarden.server_unreachable"),
                                   QStringLiteral("server"), m_loginServer));
            break;
        case BwLoginError::ServerCertificate:
            setUnlockError(I18n::t(QStringLiteral("bitwarden.server_certificate"),
                                   QStringLiteral("server"), m_loginServer));
            break;
        case BwLoginError::UnsupportedTwoStep:
            setUnlockError(I18n::t(QStringLiteral("bitwarden.unsupported_two_step")));
            break;
        default:
            setUnlockError(error.isEmpty() ? I18n::t(QStringLiteral("bitwarden.login_failed")) : error);
            break;
        }
        // Anything else starts over from the credentials, e-mail kept.
        setLoginStep(QStringLiteral("credentials"));
    });

    connect(&m_onePasswordLogin, &OnePasswordLogin::promptShown, this, [this](OpPrompt prompt) {
        setBusy(false);
        setUnlockError(QString());
        if (prompt == OpPrompt::TwoFactorCode)
            setLoginStep(QStringLiteral("opCode"));
    });
    connect(&m_onePasswordLogin, &OnePasswordLogin::succeeded, this,
            [this](const QString &account, const QString &sessionVariable, const Secret &session) {
        const bool adding = m_onePasswordAdding;
        if (adding) {
            refreshDatabases();
            emit databaseCreated();
        }

        // Whatever sheet asked for the password stays up, busy, until the
        // vault is open: swapping to another one in between would ask for
        // something that was just given.
        const DbRef ref{OnePasswordVault::refPath(account), VaultKind::OnePassword};
        runInBackground(
            [account, sessionVariable, session]() {
                OpenResult result;
                result.vault = OnePasswordVault::openWithSession(account, session, sessionVariable,
                                                                 &result.error);
                return result;
            },
            [this, ref, adding](const OpenResult &result) {
                if (!result.vault) {
                    setUnlockError(result.error);
                    setLoginStep(adding ? QStringLiteral("opCredentials") : QString());
                    return;
                }
                setLoginStep(QString());
                adoptVault(ref, result.vault);
            });
    });
    connect(&m_onePasswordLogin, &OnePasswordLogin::failed, this,
            [this](const QString &error, OpError kind) {
        setBusy(false);

        switch (kind) {
        case OpError::WrongPassword:
            setUnlockError(I18n::t(QStringLiteral("backend.wrong_password")));
            break;
        case OpError::WrongSecretKey:
            setUnlockError(I18n::t(QStringLiteral("onepassword.wrong_secret_key")));
            break;
        case OpError::WrongCode:
            setUnlockError(I18n::t(QStringLiteral("onepassword.invalid_code")));
            break;
        default:
            setUnlockError(error.isEmpty() ? I18n::t(QStringLiteral("onepassword.login_failed")) : error);
            break;
        }
        // The process is over either way: adding an account starts over from
        // the credentials, opening one goes back to the unlock sheet, where
        // the error is shown.
        setLoginStep(m_onePasswordAdding ? QStringLiteral("opCredentials") : QString());
    });

    // The wait sheet for an account check appears only if the check is still
    // running when this fires, which keeps the usual instant answer silent.
    m_accountCheckDelay.setSingleShot(true);
    m_accountCheckDelay.setInterval(accountCheckNoticeMs);
    connect(&m_accountCheckDelay, &QTimer::timeout, this, [this]() {
        if (m_accountCheckPending)
            setCheckingAccount(true);
    });

    // Starts the first scan; the list fills in when it answers, and the
    // window is on screen long before that.
    refreshDatabases();

    m_lastActivity.start();
    qApp->installEventFilter(this);

    m_lockTimer.setInterval(lockCheckIntervalMs);
    m_metaFlush.setSingleShot(true);
    m_metaFlush.setInterval(150);
    connect(&m_metaFlush, &QTimer::timeout, this, [this]() {
        const QStringList arrived = std::exchange(m_metaArrived, QStringList());
        if (!m_query.isEmpty()) {
            // A login or URL may now match what is typed: the shown set can
            // change, so it is worked out again.
            rebuildRows();
            return;
        }
        // Nothing is filtered out, so a row's place in the list is its place
        // in the entries: only the cells change and the cursor stays.
        for (const QString &path : arrived) {
            const int index = m_rowOf.value(path, -1);
            if (index < 0)
                continue;
            const EntryMeta meta = m_lazyMeta.value(path);
            QVariantMap row = m_allRows.at(index).toMap();
            row.insert(QStringLiteral("username"), meta.username);
            row.insert(QStringLiteral("url"), meta.url);
            applyIcon(&row, path, meta.url);
            m_allRows[index] = row;
            m_entryHaystacks[index] = path + QLatin1Char(' ') + row.value(QStringLiteral("typeLabel")).toString()
                                      + QLatin1Char(' ') + meta.username + QLatin1Char(' ') + meta.url
                                      + QLatin1Char(' ') + meta.extraUrls.join(QLatin1Char(' '));
            m_rowsModel.replaceRow(index, row);
        }
    });
    connect(&m_browser, &BrowserIntegration::message, this, &AppController::showMessage);
    // Answering from the start, when it was left on: the browser may well
    // ask before the user gets to the window.
    m_browser.setEnabled(m_config.browserIntegration, true);
    connect(&m_lockTimer, &QTimer::timeout, this, [this]() {
        if (m_config.lockMinutes && m_lastActivity.hasExpired(qint64(*m_config.lockMinutes) * 60 * 1000))
            lockAll();
    });
    applyLockSettings();
}

// The timer only runs while auto-lock is on, so turning it off in the
// settings stops it rather than leaving it ticking for nothing.
void AppController::applyLockSettings() {
    if (m_config.lockMinutes) {
        m_lastActivity.restart();
        m_lockTimer.start();
    } else {
        m_lockTimer.stop();
    }
}

AppController::~AppController() {
    // A sync or login waiting on the network ends now rather than when the
    // server gets round to answering; what it was doing is lost either way.
    BwApi::abortAll();
    const QString sshError = clearSshKeys();
    if (!sshError.isEmpty())
        qWarning().noquote() << sshError;
    m_bitwardenLogin.cancel();
    m_onePasswordLogin.cancel();
    m_task.waitForFinished();
    m_syncTask.waitForFinished();
    m_localScanTask.waitForFinished();
    m_accountScanTask.waitForFinished();
    m_accountCheckTask.waitForFinished();
    closeParked();
    closeVault();
    m_browser.vaultChanged();
}

template <typename Work, typename Done>
void AppController::runInBackground(Work work, Done done) {
    using Result = decltype(work());

    setBusy(true);
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, done]() {
        const Result result = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        done(result);
    });

    const QFuture<Result> future = QtConcurrent::run(std::move(work));
    m_task = QFuture<void>(future);
    watcher->setFuture(future);
}

bool AppController::requireIdle() {
    if (!m_busy)
        return true;
    showMessage(I18n::t(QStringLiteral("app.working")), true);
    return false;
}

bool AppController::vaultReadyForChanges() {
    if (m_vault.isNull() || m_busy)
        return false;
    if (m_syncing) {
        showMessage(I18n::t(QStringLiteral("bitwarden.wait_sync")), true);
        return false;
    }
    return true;
}

void AppController::setSyncing(bool syncing) {
    if (m_syncing == syncing)
        return;
    m_syncing = syncing;
    emit syncingChanged();
    if (!m_busy && !m_syncing && (m_lockAllPending || !m_lockPendingPath.isEmpty()))
        QTimer::singleShot(0, this, &AppController::finishPendingLock);
}

void AppController::startBackgroundSync() {
    auto *vault = dynamic_cast<BitwardenVault *>(m_vault.data());
    if (!vault)
        return;

    setSyncing(true);
    auto *watcher = new QFutureWatcher<TaskResult>(this);
    connect(watcher, &QFutureWatcher<TaskResult>::finished, this, [this, watcher]() {
        const TaskResult result = watcher->result();
        watcher->deleteLater();
        setSyncing(false);

        // Locking waits for the sync, so the vault is still the one synced.
        if (result.ok) {
            refreshEntries();
            showVaultWarning();
        } else
            showMessage(I18n::t(QStringLiteral("bitwarden.sync_error"), QStringLiteral("err"), result.error),
                        true);
    });

    const QFuture<TaskResult> future = QtConcurrent::run([vault]() {
        TaskResult result;
        result.ok = vault->sync(&result.error);
        return result;
    });
    m_syncTask = QFuture<void>(future);
    watcher->setFuture(future);
}

bool AppController::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        m_lastActivity.restart();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

QVariantList AppController::openDatabases() const {
    QVariantList result;
    if (m_vault)
        result.append(describe({m_vault->path(), m_vault->kind()}));
    for (const auto &item : m_parked)
        result.append(describe({item.vault->path(), item.vault->kind()}));
    return result;
}

bool AppController::isOpen(const QString &path) const {
    return (m_vault && m_vault->path() == path) ||
        std::any_of(m_parked.cbegin(), m_parked.cend(), [&](const auto &v) { return v.vault->path() == path; });
}

void AppController::chooseAnotherDatabase() {
    if (!vaultReadyForChanges())
        return;
    m_stage = QStringLiteral("databases");
    emit stageChanged();
    refreshDatabases();
}

void AppController::switchDatabase(const QString &path) {
    if (m_busy || m_syncing || m_lockAllPending || !m_lockPendingPath.isEmpty() || !isOpen(path))
        return;
    cancelAccountCheck();
    if (!m_vault || m_vault->path() != path) {
        const auto it = std::find_if(m_parked.begin(), m_parked.end(),
                                     [&](const auto &v) { return v.vault->path() == path; });
        auto next = std::move(*it);
        m_parked.erase(it);
        parkVault();
        m_vault = std::move(next.vault);
        m_sshAgent = std::move(next.ssh);
    }
    displayVault();
}

void AppController::closeParked() {
    m_browser.waitForReads();
    for (auto &v : m_parked) {
        const QString error = v.ssh.clear();
        if (!error.isEmpty())
            qWarning().noquote() << error;
        v.vault->close();
    }
    m_parked.clear();
}

void AppController::finishPendingLock() {
    if (m_busy || m_syncing)
        return;
    if (m_lockAllPending) {
        lockAll();
        return;
    }
    const QString path = std::exchange(m_lockPendingPath, {});
    if (path.isEmpty())
        return;
    if (m_vault && m_vault->path() == path) {
        lock();
        return;
    }
    // Unlocking another vault may have finished while the lock was queued.
    for (auto it = m_parked.begin(); it != m_parked.end(); ++it) {
        if (it->vault->path() != path)
            continue;
        m_browser.waitForReads();
        const QString error = it->ssh.clear();
        it->vault->close();
        m_parked.erase(it);
        m_browser.vaultChanged();
        emit stageChanged();
        if (!error.isEmpty())
            showMessage(error, true);
        break;
    }
}

void AppController::lockAll() {
    if (!m_vault)
        return;
    if (m_busy || m_syncing) {
        m_lockAllPending = true;
        return;
    }
    m_lockAllPending = false;
    cancelAccountCheck();
    m_hasPendingDatabase = false;
    m_bitwardenLogin.cancel();
    m_onePasswordLogin.cancel();
    setLoginStep({});
    emit pendingDatabaseChanged();
    closeParked();
    lock();
}

void AppController::lock() {
    if (!m_vault)
        return;
    if (m_busy || m_syncing) {
        m_lockPendingPath = m_vault->path();
        return;
    }
    if (m_lockAllPending) {
        lockAll();
        return;
    }
    m_lockPendingPath.clear();
    const QString sshError = closeVault();
    if (!m_parked.isEmpty()) {
        switchDatabase(m_parked.constLast().vault->path());
    } else {
        m_browser.vaultChanged();
        refreshPinState();
        m_query.clear();
        refreshEntries();
        m_stage = QStringLiteral("databases");
        emit stageChanged();
        refreshDatabases();
    }
    if (!sshError.isEmpty())
        showMessage(sshError, true);
}

QVariantList AppController::databases() const {
    QVariantList list;
    list.reserve(m_filteredDatabases.size());
    for (const DbRef &ref : m_filteredDatabases)
        list.append(describe(ref));
    return list;
}

void AppController::setDatabaseQuery(const QString &query) {
    if (m_databaseQuery == query)
        return;

    m_databaseQuery = query;
    applyDatabaseFilter();
}

// Also what a partial scan result goes through: what the user has typed so
// far keeps applying while the rest of the databases are still arriving.
void AppController::applyDatabaseFilter() {
    QStringList paths;
    for (const DbRef &ref : std::as_const(m_databases))
        paths.append(ref.path);
    const QStringList kept = filterItems(paths, m_databaseQuery);

    m_filteredDatabases.clear();
    for (const DbRef &ref : std::as_const(m_databases)) {
        if (kept.contains(ref.path))
            m_filteredDatabases.append(ref);
    }

    emit databasesChanged();
}

QVariantMap AppController::pendingDatabase() const {
    if (!m_hasPendingDatabase)
        return {};
    return describe(m_pendingDatabase);
}

QString AppController::vaultLabel() const {
    if (m_vault.isNull())
        return QString();
    return Vault::displayName({m_vault->path(), m_vault->kind()});
}

bool AppController::bitwardenBackend() const {
    return !m_vault.isNull() && m_vault->kind() == VaultKind::Bitwarden;
}

DbRef AppController::pinTarget() const {
    if (m_hasPendingDatabase)
        return m_pendingDatabase;
    if (m_vault)
        return {m_vault->path(), m_vault->kind()};
    return DbRef();
}

// Looking a PIN up costs one quick secret-tool call, so it is refreshed
// whenever the database in play changes rather than kept in sync by hand.
void AppController::refreshPinState() {
    const DbRef target = pinTarget();
    const bool configured = !target.path.isEmpty() && Pin::hasPin(target.path);

    m_pinAvailable = m_hasPendingDatabase && configured;

    if (m_pinConfigured != configured) {
        m_pinConfigured = configured;
        emit pinConfiguredChanged();
    }
}

bool AppController::onePasswordBackend() const {
    return !m_vault.isNull() && m_vault->kind() == VaultKind::OnePassword;
}

bool AppController::passBackend() const {
    return !m_vault.isNull() && m_vault->kind() == VaultKind::Pass;
}

// Off this thread, always. Finding databases walks the search path and asks
// the account backends, and on a large home or a slow `op` that is seconds
// the interface used to spend before its first frame. The two halves run side
// by side and each puts its results on the list as soon as it has them, so
// the local files never wait for an account that needs the network.
void AppController::refreshDatabases() {
    const QString path = m_config.searchPath;
    const bool autoSearch = m_config.autoSearch;
    const QVector<DbRef> registered = loadRegisteredDatabases();
    const quint64 generation = ++m_scanGeneration;
    m_scansOutstanding = 2;
    m_databaseQuery.clear();
    setScanning(true);

    // Deliberately not runInBackground(): a scan must not mark the app busy,
    // since everything else on the list stays usable while it runs.
    const auto start = [this, generation](QVector<DbRef> *slot, QFuture<void> *task,
                                          auto work) {
        auto *watcher = new QFutureWatcher<QVector<DbRef>>(this);
        connect(watcher, &QFutureWatcher<QVector<DbRef>>::finished, this,
                [this, watcher, generation, slot]() {
            const QVector<DbRef> found = watcher->result();
            watcher->deleteLater();
            finishScanPart(generation, slot, found);
        });
        const QFuture<QVector<DbRef>> future = QtConcurrent::run(work);
        *task = QFuture<void>(future);
        watcher->setFuture(future);
    };

    start(&m_localDatabases, &m_localScanTask,
          [path, autoSearch, registered]() { return Vault::findLocalDatabases(path, autoSearch, registered); });
    start(&m_accountDatabases, &m_accountScanTask,
          []() { return Vault::findAccountDatabases(); });
}

void AppController::finishScanPart(quint64 generation, QVector<DbRef> *slot,
                                   const QVector<DbRef> &found) {
    // A newer scan is already on its way; this answer is out of date.
    if (generation != m_scanGeneration)
        return;

    *slot = found;
    --m_scansOutstanding;

    // The list first, the flag second: anyone watching `scanning` go
    // false reads the list in the same breath, and between these two
    // lines it would still look empty — which is how the interface came
    // to offer to create a database with databases sitting behind it.
    adoptDatabases();
    if (m_scansOutstanding > 0)
        return;
    setScanning(false);

    const bool first = m_firstScan;
    m_firstScan = false;
    // With exactly one database there is nothing to choose: go straight
    // to its unlock prompt, as the startup used to do synchronously.
    if (first && m_databases.size() == 1 && !m_hasPendingDatabase
        && m_stage == QStringLiteral("databases"))
        selectDatabase(0);
}

void AppController::setScanning(bool scanning) {
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void AppController::adoptDatabases() {
    m_databases = m_localDatabases + m_accountDatabases;

    // Most-used first, so the database you actually open is the one under the
    // cursor when the window appears.
    QStringList paths;
    for (const DbRef &ref : std::as_const(m_databases))
        paths.append(ref.path);
    m_history.sortItems(paths);

    QVector<DbRef> sorted;
    sorted.reserve(m_databases.size());
    for (const QString &path : std::as_const(paths)) {
        for (const DbRef &ref : std::as_const(m_databases)) {
            if (ref.path == path) {
                sorted.append(ref);
                break;
            }
        }
    }
    m_databases = sorted;

    applyDatabaseFilter();
}

void AppController::askForPassword(const DbRef &ref) {
    m_pendingDatabase = ref;
    m_hasPendingDatabase = true;
    refreshPinState();
    emit pendingDatabaseChanged();
}

void AppController::setCheckingAccount(bool checking) {
    if (m_checkingAccount == checking)
        return;
    m_checkingAccount = checking;
    emit checkingAccountChanged();
}

// What an account backend answered about itself, and what to do with it.
namespace {

struct AccountCheck {
    bool usable = false;   // still logged in / still configured
    bool gone = false;     // dropped from the backend behind omapass' back
    QString email;         // Bitwarden only: who to put on the login sheet
    QString server;        // …and on which server
};

}

// The checks below read a file in the good case, but 1Password's can fall
// back to running `op`, which takes seconds. Blocking the interface on that turns a keypress into a freeze, so
// it happens here, off the thread, with a sheet saying so if it drags.
void AppController::checkAccountThenAsk(const DbRef &ref) {
    const quint64 generation = ++m_accountCheckGeneration;
    m_accountCheckPending = true;
    m_checkingAccountName = Vault::displayName(ref);
    m_accountCheckDelay.start();

    auto *watcher = new QFutureWatcher<AccountCheck>(this);
    connect(watcher, &QFutureWatcher<AccountCheck>::finished, this,
            [this, watcher, ref, generation]() {
        const AccountCheck check = watcher->result();
        watcher->deleteLater();

        // Another database was picked while this was in flight, or the wait
        // was given up on.
        if (generation != m_accountCheckGeneration || !m_accountCheckPending)
            return;

        m_accountCheckPending = false;
        m_accountCheckDelay.stop();
        setCheckingAccount(false);

        if (check.usable) {
            askForPassword(ref);
            return;
        }

        if (check.gone) {
            // Dropped from a terminal while omapass had it listed.
            showMessage(I18n::t(QStringLiteral("onepassword.account_gone")), true);
            refreshDatabases();
            return;
        }

        // Logged out behind omapass' back: the master password alone cannot
        // unlock it, so the login sheet takes over.
        m_loginEmail = check.email;
        m_loginServer = check.server;
        setLoginStep(QStringLiteral("credentials"));
    });

    const QFuture<AccountCheck> future = QtConcurrent::run([ref]() {
        AccountCheck check;
        if (ref.kind == VaultKind::OnePassword) {
            // A 1Password account can have been dropped behind omapass' back
            // (`op account forget` in a terminal), and then the master
            // password alone cannot open it.
            check.usable = OnePasswordVault::hasAccount(OnePasswordVault::accountOf(ref.path));
            check.gone = !check.usable;
            return check;
        }

        check.usable = BitwardenVault::hasSession(ref.path);
        check.email = BwAccount::emailOf(ref.path);
        check.server = BitwardenVault::serverLabel(ref.path);
        return check;
    });
    m_accountCheckTask = QFuture<void>(future);
    watcher->setFuture(future);
}

void AppController::selectDatabase(int index) {
    if (m_busy || m_syncing || index < 0 || index >= m_filteredDatabases.size())
        return;

    const DbRef ref = m_filteredDatabases.at(index);
    if (isOpen(ref.path)) {
        switchDatabase(ref.path);
        return;
    }

    if (ref.kind != VaultKind::Bitwarden && ref.kind != VaultKind::OnePassword) {
        setUnlockError(QString());
        askForPassword(ref);
        return;
    }

    if (m_checkingAccount)
        return;

    setUnlockError(QString());
    checkAccountThenAsk(ref);
}

// Giving up on the wait: the check itself cannot be called off (it is a
// process already running), but its answer stops being acted on, so the
// list comes straight back.
void AppController::cancelAccountCheck() {
    if (!m_accountCheckPending)
        return;
    m_accountCheckPending = false;
    m_accountCheckDelay.stop();
    setCheckingAccount(false);
}

void AppController::cancelUnlock() {
    if (m_busy)
        return;
    // A sign-in waiting on a two-step code would otherwise sit there.
    m_onePasswordLogin.cancel();
    m_hasPendingDatabase = false;
    refreshPinState();
    setUnlockError(QString());
    emit pendingDatabaseChanged();
}

void AppController::unlock(const QString &password) {
    if (m_busy || !m_hasPendingDatabase)
        return;

    setUnlockError(QString());

    // `op signin` can stop to ask for a two-step code, and that needs the
    // process kept alive while the interface asks for it — which the
    // background open, a single blocking call, cannot do.
    if (m_pendingDatabase.kind == VaultKind::OnePassword) {
        m_onePasswordAdding = false;
        setBusy(true);
        m_onePasswordLogin.startSignIn(OnePasswordVault::accountOf(m_pendingDatabase.path),
                                       Secret(password));
        return;
    }

    const Secret secret(password);
    const DbRef ref = m_pendingDatabase;
    runInBackground(
        [ref, secret]() {
            OpenResult result;
            result.vault = Vault::open(ref, secret, &result.error);
            return result;
        },
        [this, ref](const OpenResult &result) {
            if (result.vault)
                adoptVault(ref, result.vault);
            else
                setUnlockError(result.error);
        });
}

// The PIN never reaches a backend: it decrypts the master password kept in the
// keyring, and the usual unlock goes ahead with that.
void AppController::unlockWithPin(const QString &pin) {
    if (m_busy || !m_hasPendingDatabase || !m_pinAvailable)
        return;

    setUnlockError(QString());

    const DbRef ref = m_pendingDatabase;
    const QString target = ref.path;
    struct PinUnlock {
        Pin::Result result = Pin::Result::Missing;
        Secret password;
    };

    runInBackground(
        [target, pin]() {
            PinUnlock unlock;
            unlock.result = Pin::recover(target, pin, &unlock.password);
            return unlock;
        },
        [this, ref, target](const PinUnlock &unlock) {
            switch (unlock.result) {
            case Pin::Result::Ok:
                break;
            case Pin::Result::WrongPin: {
                const int left = Pin::registerFailure(target);
                refreshPinState();
                emit pendingDatabaseChanged();
                setUnlockError(left > 0
                    ? I18n::t(QStringLiteral("pin.wrong"), QStringLiteral("left"),
                              QString::number(left))
                    : I18n::t(QStringLiteral("pin.blocked")));
                return;
            }
            case Pin::Result::Missing:
            case Pin::Result::Unavailable:
                Pin::clear(target);
                refreshPinState();
                emit pendingDatabaseChanged();
                setUnlockError(I18n::t(QStringLiteral("pin.missing")));
                return;
            }

            Pin::resetAttempts(target);
            const Secret password = unlock.password;
            runInBackground(
                [ref, password]() {
                    OpenResult result;
                    result.vault = Vault::open(ref, password, &result.error);
                    return result;
                },
                [this, ref, target](const OpenResult &result) {
                    if (result.vault) {
                        adoptVault(ref, result.vault);
                        return;
                    }
                    // The stored password no longer opens the database (a
                    // changed master password, say): the PIN goes with it.
                    Pin::clear(target);
                    refreshPinState();
                    emit pendingDatabaseChanged();
                    setUnlockError(I18n::t(QStringLiteral("pin.stale")));
                });
        });
}

void AppController::openVault(const DbRef &ref, const Secret &secret) {
    QString error;
    Vault *vault = Vault::open(ref, secret, &error);
    if (!vault) {
        setUnlockError(error);
        return;
    }

    adoptVault(ref, vault);
}

void AppController::adoptVault(const DbRef &ref, Vault *vault) {
    if (isOpen(ref.path)) {
        delete vault;
        switchDatabase(ref.path);
        return;
    }
    parkVault();
    m_vault.reset(vault);
    m_history.recordUse(ref.path);
    displayVault();
    startSshKeys();
    if (vault->kind() == VaultKind::Bitwarden)
        startBackgroundSync();
}

void AppController::parkVault() {
    if (!m_vault)
        return;
    if (m_sshTask) {
        m_sshTask->waitForFinished();
        delete m_sshTask;
        m_sshTask = nullptr;
    }
    m_parked.append({m_vault, std::move(m_sshAgent), entriesFor(m_vault.data()), metaFor(m_vault.data())});
    m_sshAgent = {};
    resetVaultView();
    m_vault.reset();
}

void AppController::displayVault() {
    m_hasPendingDatabase = false;
    refreshPinState();
    emit pendingDatabaseChanged();
    loadIconChoice();
    clearMessage();
    m_query.clear();
    refreshEntries();
    m_browser.vaultChanged();

    m_stage = QStringLiteral("entries");
    emit stageChanged();
    showVaultWarning();
}

void AppController::showVaultWarning() {
    if (m_vault.isNull())
        return;
    const QString warning = m_vault->warning();
    if (!warning.isEmpty())
        showMessage(warning, true);
}

void AppController::refreshEntries() {
    m_allEntries.clear();
    m_groups.clear();
    m_listedMeta.clear();
    if (!m_vault.isNull()) {
        m_vault->list(&m_allEntries, &m_groups);
        m_listedMeta = m_vault->entryMeta();
        m_entryIcons.load(m_vault.data(), m_allEntries);
    } else {
        m_entryIcons.load(nullptr, {});
        stopMetaLoad();
        m_lazyMeta.clear();
    }

    if (m_vault)
        m_history.sortItems(m_allEntries, m_vault->path());
    rebuildRows();
    startMetaLoad();
}

// Rows and what the search reads for each, from the entries and whatever
// meta is known by now.
void AppController::rebuildRows() {
    const QString emptySuffix = Vault::emptyGroupSuffix();

    m_allRows.clear();
    m_entryHaystacks.clear();
    m_rowOf.clear();
    for (const QString &path : std::as_const(m_allEntries)) {
        QString type;
        QString name;
        QString group;
        QString username;
        QString url;
        QStringList extraUrls;

        if (isEmptyGroup(path)) {
            type = QStringLiteral("group");
            group = groupNameOf(path);
            name = emptySuffix.mid(1);
        } else {
            const EntryMeta entry = m_lazyMeta.contains(path) ? m_lazyMeta.value(path)
                                                              : m_listedMeta.value(path);
            type = entry.type;
            group = parentGroup(path);
            name = path.section(QLatin1Char('/'), -1);
            username = entry.username;
            url = entry.url;
            extraUrls = entry.extraUrls;
        }

        const QString typeLabel = type == QLatin1String("ssh") ? I18n::t(QStringLiteral("ui.type_ssh"))
                                : type == QLatin1String("login") ? I18n::t(QStringLiteral("ui.type_login"))
                                                                  : I18n::t(QStringLiteral("ui.type_group"));
        QVariantMap row;
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("name"), name);
        row.insert(QStringLiteral("group"), group);
        row.insert(QStringLiteral("type"), type);
        row.insert(QStringLiteral("typeLabel"), typeLabel);
        row.insert(QStringLiteral("username"), username);
        row.insert(QStringLiteral("url"), url);
        applyIcon(&row, path, url);

        m_rowOf.insert(path, m_allRows.size());
        m_allRows.append(row);
        // The path carries name and group; the rest is what the columns add.
        m_entryHaystacks.append(path + QLatin1Char(' ') + typeLabel + QLatin1Char(' ') + username
                                + QLatin1Char(' ') + url + QLatin1Char(' ') + extraUrls.join(QLatin1Char(' ')));
    }
    applyEntryFilter();
}

void AppController::applyIcon(QVariantMap *row, const QString &path, const QString &url) {
    // The user's choice wins, and shows whether or not site icons are on.
    const QString chosen = m_entryIcons.urlFor(path);
    if (!chosen.isEmpty()) {
        row->insert(QStringLiteral("origin"), QString());
        row->insert(QStringLiteral("icon"), chosen);
        return;
    }

    const QString origin = m_iconsDownload ? SiteIcons::originOf(url) : QString();
    row->insert(QStringLiteral("origin"), origin);
    if (origin.isEmpty()) {
        row->insert(QStringLiteral("icon"), QString());
        return;
    }
    if (m_siteIcons.hasIcon(origin)) {
        row->insert(QStringLiteral("icon"),
                    QStringLiteral("image://siteicon/") + SiteIcons::keyOf(origin));
    } else {
        row->insert(QStringLiteral("icon"), QString());
        m_siteIcons.request(origin);
    }
}

void AppController::loadIconChoice() {
    const IconChoice choice =
        IconChoice::load(m_vault.data(), m_config.siteIcons, m_config.siteIconsDuckDuckGo);
    m_iconsDownload = choice.download;
    m_iconsDuckDuckGo = choice.duckDuckGo;
    m_siteIcons.setDuckDuckGo(m_iconsDuckDuckGo);
}

void AppController::updateShownRow(int index) {
    const int shown = m_shownRows.indexOf(index);
    if (shown >= 0)
        m_rowsModel.replaceRow(shown, m_allRows.at(index).toMap());
}

void AppController::applyEntryFilter() {
    m_shownRows = filterIndexes(m_entryHaystacks, m_query);

    QVariantList rows;
    m_filteredEntries.clear();
    for (int index : std::as_const(m_shownRows)) {
        m_filteredEntries.append(m_allEntries.at(index));
        rows.append(m_allRows.at(index));
    }
    m_rowsModel.setRows(rows);
    emit entriesChanged();
}

// Reads login and URL, a few entries at a time, for the entries a lazy
// backend has not given yet. Not marked busy: the list works meanwhile.
void AppController::startMetaLoad() {
    stopMetaLoad();
    if (m_vault.isNull() || !m_vault->loadsMetaLazily())
        return;

    QStringList missing;
    for (const QString &path : std::as_const(m_allEntries)) {
        if (!isEmptyGroup(path) && !m_lazyMeta.contains(path))
            missing.append(path);
    }
    if (missing.isEmpty())
        return;

    if (m_metaPool.maxThreadCount() != 4)
        m_metaPool.setMaxThreadCount(4);

    // Closing the vault waits for the pool, so it outlives the workers.
    const Vault *vault = m_vault.data();
    m_metaWatcher = new QFutureWatcher<LazyMeta>(this);
    connect(m_metaWatcher, &QFutureWatcher<LazyMeta>::resultReadyAt, this,
            [this, watcher = m_metaWatcher](int index) {
        const LazyMeta result = watcher->resultAt(index);
        if (!result.ok)
            return;
        m_lazyMeta.insert(result.path, result.meta);
        m_metaArrived.append(result.path);
        if (!m_metaFlush.isActive())
            m_metaFlush.start();
    });
    m_metaWatcher->setFuture(QtConcurrent::mapped(&m_metaPool, missing, [vault](const QString &path) {
        LazyMeta result;
        result.path = path;
        result.ok = vault->fetchEntryMeta(path, &result.meta);
        return result;
    }));
}

void AppController::stopMetaLoad() {
    if (!m_metaWatcher)
        return;
    // Cancelling stops what has not started; what is running ends by itself
    // and its result is dropped with the watcher.
    m_metaWatcher->disconnect(this);
    m_metaWatcher->cancel();
    m_metaWatcher->deleteLater();
    m_metaWatcher = nullptr;
}

void AppController::setQuery(const QString &query) {
    if (m_query == query)
        return;

    m_query = query;
    applyEntryFilter();
}

bool AppController::isEmptyGroup(const QString &entry) const {
    return entry.endsWith(Vault::emptyGroupSuffix());
}

QString AppController::groupNameOf(const QString &entry) const {
    const QString suffix = Vault::emptyGroupSuffix();
    return entry.endsWith(suffix) ? entry.chopped(suffix.size()) : entry;
}

QVariantMap AppController::entryMenuFields(const QString &entry) const {
    EntryData data;
    QString error;
    if (!m_vault || entry.isEmpty() || isEmptyGroup(entry) || !m_vault->fetchEntry(entry, &data, &error))
        return {};
    // A native Bitwarden SSH item exposes its private key through fetchPassword.
    const bool privateKey = m_listedMeta.value(entry).type == QLatin1String("ssh");
    return {{QStringLiteral("hasPassword"), !privateKey && !data.password.isEmpty()},
            {QStringLiteral("hasUsername"), !data.username.trimmed().isEmpty()},
            {QStringLiteral("hasUrl"), !data.url.trimmed().isEmpty()},
            {QStringLiteral("hasOtp"), !data.otp.isEmpty() && Totp::parse(data.otp.toString()).has_value()}};
}

QVariantMap AppController::sshKeyOptions(const QString &entry) const {
    const auto key = m_listedMeta.value(entry).ssh;
    if (!SshAgent::supported() || !m_vault || !key)
        return {};
    const auto options = sshOptions(m_vault.data(), *key);
    const QString id = sshSettingsKey(m_vault.data(), *key);
    bool inAgent = false;
    if (m_sshAgent.tracks(id)) {
        inAgent = m_sshAgent.isLoaded(id);
    } else {
        Secret privateKey;
        QString error;
        if (m_vault->fetchSshKey(entry, key->id, &privateKey, &error))
            inAgent = m_sshAgent.isLoaded(id, privateKey);
    }
    return {{QStringLiteral("addOnUnlock"), options.addOnUnlock},
            {QStringLiteral("confirm"), options.confirm},
            {QStringLiteral("lifetimeSeconds"), options.lifetimeSeconds},
            {QStringLiteral("inAgent"), inAgent}};
}

bool AppController::saveSshKeyOptions(const QString &entry, bool addOnUnlock, bool confirm,
                                      int lifetimeSeconds) {
    const auto key = m_listedMeta.value(entry).ssh;
    if (!m_vault || !key || lifetimeSeconds < 0)
        return false;
    QSettings settings;
    settings.beginGroup(sshSettingsKey(m_vault.data(), *key));
    settings.setValue(QStringLiteral("addOnUnlock"), addOnUnlock);
    settings.setValue(QStringLiteral("confirm"), confirm);
    settings.setValue(QStringLiteral("lifetimeSeconds"), lifetimeSeconds);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        showMessage(I18n::t(QStringLiteral("ssh.save_failed")), true);
        return false;
    }
    showMessage(I18n::t(QStringLiteral("ssh.saved")), false);
    return true;
}

void AppController::runSshTask(std::function<QString(const QPromise<QString> &)> work,
                               const QString &success) {
    if (m_sshTask) {
        showMessage(I18n::t(QStringLiteral("app.working")), true);
        return;
    }
    auto *watcher = new QFutureWatcher<QString>(this);
    m_sshTask = watcher;
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, success]() {
        m_sshTask = nullptr;
        const QString error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty() || !success.isEmpty())
            showMessage(error.isEmpty() ? success : error, !error.isEmpty());
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)](QPromise<QString> &promise) {
        promise.addResult(work(promise));
    }));
}

QString AppController::addSshIdentity(const Vault *vault, const QString &entry,
                                      const SshKeyConfig &key, const SshKeyOptions &options) {
    Secret privateKey;
    QString error;
    if (!vault->fetchSshKey(entry, key.id, &privateKey, &error))
        return error.isEmpty() ? I18n::t(QStringLiteral("ssh.invalid_key")) : error;
    return m_sshAgent.add(sshSettingsKey(vault, key), privateKey, options);
}

void AppController::startSshKeys() {
    if (!SshAgent::supported())
        return;
    const Vault *vault = m_vault.data();
    QList<std::tuple<QString, SshKeyConfig, SshKeyOptions>> keys;
    for (auto it = m_listedMeta.cbegin(); it != m_listedMeta.cend(); ++it) {
        if (!it->ssh)
            continue;
        const SshKeyOptions options = sshOptions(vault, *it->ssh);
        if (options.addOnUnlock)
            keys.append({it.key(), *it->ssh, options});
    }
    if (keys.isEmpty())
        return;
    runSshTask([this, vault, keys](const QPromise<QString> &promise) {
        QString error;
        for (const auto &[entry, key, options] : keys) {
            if (promise.isCanceled())
                break;
            const QString failure = addSshIdentity(vault, entry, key, options);
            if (!failure.isEmpty())
                error = failure;
        }
        return error;
    });
}

void AppController::addSshKey(const QString &entry) {
    const auto key = m_listedMeta.value(entry).ssh;
    const Vault *vault = m_vault.data();
    if (!SshAgent::supported() || !vault || !key)
        return;
    const auto options = sshOptions(vault, *key);
    runSshTask([this, vault, entry, key, options](const QPromise<QString> &) {
        return addSshIdentity(vault, entry, *key, options);
    }, I18n::t(QStringLiteral("ssh.added")));
}

void AppController::removeSshKey(const QString &entry) {
    const auto key = m_listedMeta.value(entry).ssh;
    const Vault *vault = m_vault.data();
    if (!vault || !key)
        return;
    const QString id = sshSettingsKey(vault, *key);
    runSshTask([this, vault, entry, key, id](const QPromise<QString> &) {
        if (m_sshAgent.tracks(id))
            return m_sshAgent.remove(id);

        // An explicit removal can target the selected identity even when
        // this instance did not load it, e.g. after an ungraceful exit.
        Secret privateKey;
        QString error;
        if (!vault->fetchSshKey(entry, key->id, &privateKey, &error))
            return error.isEmpty() ? I18n::t(QStringLiteral("ssh.invalid_key")) : error;
        return m_sshAgent.removeKey(privateKey);
    }, I18n::t(QStringLiteral("ssh.removed")));
}

void AppController::copyPassword(const QString &entry) {
    if (m_vault.isNull())
        return;

    if (isEmptyGroup(entry)) {
        showMessage(I18n::t(QStringLiteral("app.empty_group")), true);
        return;
    }

    m_history.recordUse(entry, m_vault->path());

    Secret password;
    QString error;
    if (!m_vault->fetchPassword(entry, &password, &error)) {
        showMessage(I18n::t(QStringLiteral("app.copy_password_error")), true);
        return;
    }

    if (!Clipboard::copy(password.toString())) {
        showMessage(I18n::t(QStringLiteral("app.copy_password_error")), true);
        return;
    }

    showClipboardMessage(I18n::t(QStringLiteral("app.copied_entry"), QStringLiteral("entry"), entry));
    Clipboard::scheduleClear(password);
}

void AppController::copyText(const QString &text) {
    if (text.isEmpty())
        return;

    if (Clipboard::copy(text))
        showMessage(I18n::t(QStringLiteral("app.copied_to_clipboard")), false);
    else
        showMessage(I18n::t(QStringLiteral("app.copy_error")), true);
}

bool AppController::fetchForCopy(const QString &entry, EntryData *data) {
    if (m_vault.isNull())
        return false;
    if (isEmptyGroup(entry)) {
        showMessage(I18n::t(QStringLiteral("app.empty_group")), true);
        return false;
    }
    QString error;
    if (!m_vault->fetchEntry(entry, data, &error)) {
        showMessage(error, true);
        return false;
    }
    m_history.recordUse(entry, m_vault->path());
    return true;
}

void AppController::copyUsername(const QString &entry) {
    EntryData data;
    if (!fetchForCopy(entry, &data))
        return;
    if (data.username.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.no_username")), true);
        return;
    }
    if (Clipboard::copy(data.username))
        showMessage(I18n::t(QStringLiteral("app.copied_username"), QStringLiteral("entry"), entry), false);
    else
        showMessage(I18n::t(QStringLiteral("app.copy_error")), true);
}

void AppController::copyUrl(const QString &entry) {
    EntryData data;
    if (!fetchForCopy(entry, &data))
        return;
    if (data.url.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.no_url")), true);
        return;
    }
    if (Clipboard::copy(data.url))
        showMessage(I18n::t(QStringLiteral("app.copied_url"), QStringLiteral("entry"), entry), false);
    else
        showMessage(I18n::t(QStringLiteral("app.copy_error")), true);
}

void AppController::copyOtp(const QString &entry) {
    EntryData data;
    if (!fetchForCopy(entry, &data))
        return;
    if (data.otp.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.no_otp")), true);
        return;
    }

    QString error;
    const std::optional<TotpParams> params = Totp::parse(data.otp.toString(), &error);
    if (!params) {
        showMessage(I18n::t(QStringLiteral("app.otp_unreadable"), {{QStringLiteral("entry"), entry},
                                                                   {QStringLiteral("err"), error}}),
                    true);
        return;
    }
    copyOtpCode(Totp::code(*params, QDateTime::currentSecsSinceEpoch()), entry);
}

void AppController::copyOtpCode(const QString &code, const QString &entry) {
    if (!Clipboard::copy(code)) {
        showMessage(I18n::t(QStringLiteral("app.copy_error")), true);
        return;
    }
    showClipboardMessage(I18n::t(QStringLiteral("app.copied_otp"), QStringLiteral("entry"), entry));
    Clipboard::scheduleClear(Secret(code));
}

void AppController::openUrl(const QString &entry) {
    EntryData data;
    if (!fetchForCopy(entry, &data))
        return;
    if (data.url.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.no_url")), true);
        return;
    }
    openAddress(data.url);
}

void AppController::openAddress(const QString &address) {
    // Saved without a scheme ("exemplo.com") is how people type addresses;
    // the browser needs one.
    QUrl url = QUrl::fromUserInput(address.trimmed());
    if (address.trimmed().isEmpty() || !url.isValid() || url.scheme().isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.open_url_error"), QStringLiteral("url"), address), true);
        return;
    }
    if (QDesktopServices::openUrl(url))
        showMessage(I18n::t(QStringLiteral("app.opening_url"), QStringLiteral("url"), url.toString()), false);
    else
        showMessage(I18n::t(QStringLiteral("app.open_url_error"), QStringLiteral("url"), address), true);
}

QVariantMap AppController::entryDetails(const QString &entry) {
    if (m_vault.isNull() || isEmptyGroup(entry)) {
        showMessage(I18n::t(QStringLiteral("app.empty_group")), true);
        return {};
    }

    const QString fallback = entry.section(QLatin1Char('/'), -1);

    EntryData data;
    QString error;
    m_vault->fetchEntry(entry, &data, &error);

    // A value that does not parse is still shown as missing rather than as
    // a wrong code; copying it says why.
    m_shownOtpEntry = entry;
    m_shownOtp = data.otp.isEmpty() ? std::nullopt : Totp::parse(data.otp.toString());

    return {{QStringLiteral("title"), m_vault->titleFor(entry, fallback)},
            {QStringLiteral("username"), data.username},
            {QStringLiteral("url"), data.url},
            {QStringLiteral("extraUrls"), data.extraUrls},
            {QStringLiteral("notes"), data.notes},
            {QStringLiteral("hasOtp"), m_shownOtp.has_value()}};
}

QVariantMap AppController::otpNow() const {
    if (!m_shownOtp)
        return {};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return {{QStringLiteral("code"), Totp::code(*m_shownOtp, now)},
            {QStringLiteral("secondsLeft"), Totp::secondsLeft(*m_shownOtp, now)},
            {QStringLiteral("period"), m_shownOtp->period}};
}

void AppController::copyShownOtp() {
    if (!m_shownOtp)
        return;
    copyOtpCode(Totp::code(*m_shownOtp, QDateTime::currentSecsSinceEpoch()), m_shownOtpEntry);
}

void AppController::forgetShownOtp() {
    m_shownOtp.reset();
    m_shownOtpEntry.clear();
}

// The password comes back in the clear because an editable field has to hold
// it; it is the one place omapass cannot keep a secret inside `Secret` alone.
QVariantMap AppController::entryFields(const QString &entry) {
    QVariantMap fields;
    fields.insert(QStringLiteral("originalPath"), entry);

    const int slash = entry.lastIndexOf(QLatin1Char('/'));
    fields.insert(QStringLiteral("group"), slash < 0 ? QString() : entry.left(slash));
    fields.insert(QStringLiteral("title"), slash < 0 ? entry : entry.mid(slash + 1));

    if (m_vault.isNull())
        return fields;

    EntryData data;
    QString error;
    if (!m_vault->fetchEntry(entry, &data, &error)) {
        showMessage(I18n::t(QStringLiteral("app.read_entry_error"), QStringLiteral("err"), error), true);
        return fields;
    }

    const auto ssh = m_listedMeta.value(entry).ssh;
    if (ssh) {
        const bool attachment = m_vault->kind() == VaultKind::Keepass;
        fields.insert(QStringLiteral("type"), QStringLiteral("ssh"));
        fields.insert(QStringLiteral("sshAttachment"), attachment);
        if (data.sshPublicKey.isEmpty()) {
            Secret key;
            if (!attachment)
                key = data.password;
            else
                m_vault->fetchSshKey(entry, ssh->id, &key, &error);
            if (!key.isEmpty())
                data.sshPublicKey = SshAgent::publicIdentity(key);
        }
        fields.insert(QStringLiteral("sshPublicKey"), data.sshPublicKey);
    }
    fields.insert(QStringLiteral("username"), data.username);
    fields.insert(QStringLiteral("password"), data.password.toString());
    fields.insert(QStringLiteral("url"), data.url);
    fields.insert(QStringLiteral("extraUrls"), data.extraUrls);
    fields.insert(QStringLiteral("notes"), data.notes);
    fields.insert(QStringLiteral("extra"), data.extra);
    fields.insert(QStringLiteral("otp"), data.otp.toString());
    return fields;
}

bool AppController::saveEntry(const QVariantMap &fields) {
    if (!vaultReadyForChanges())
        return false;

    QString group = fields.value(QStringLiteral("group")).toString().trimmed();
    while (group.endsWith(QLatin1Char('/')))
        group.chop(1);

    const QString title = fields.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.title_empty")), true);
        return false;
    }

    const QString path = group.isEmpty() ? title : group + QLatin1Char('/') + title;
    const bool isEdit = fields.value(QStringLiteral("isEdit")).toBool();

    EntryData data;
    data.username = fields.value(QStringLiteral("username")).toString().trimmed();
    data.password = Secret(fields.value(QStringLiteral("password")).toString());
    if (fields.value(QStringLiteral("type")).toString() == QLatin1String("ssh")) {
        if (passBackend() || onePasswordBackend())
            return false;
        const QString publicKey = fields.value(QStringLiteral("sshPublicKey")).toString().trimmed();
        Secret privateKey = data.password;
        const bool attachment = isEdit && m_vault->kind() == VaultKind::Keepass;
        bool validate = true;
        if (isEdit) {
            const QString original = fields.value(QStringLiteral("originalPath")).toString();
            const auto ssh = m_listedMeta.value(original).ssh;
            EntryData stored;
            QString error;
            if (!ssh || !m_vault->fetchEntry(original, &stored, &error)) {
                showMessage(I18n::t(QStringLiteral("app.read_entry_error"), QStringLiteral("err"), error), true);
                return false;
            }
            validate = publicKey != stored.sshPublicKey || (!attachment && privateKey.bytes() != stored.password.bytes());
            if (attachment && validate && !m_vault->fetchSshKey(original, ssh->id, &privateKey, &error)) {
                showMessage(I18n::t(QStringLiteral("ssh.invalid_key")), true);
                return false;
            }
        }
        if (validate) {
            privateKey = Secret(privateKey.toString().trimmed() + QLatin1Char('\n'));
            const QString identity = SshAgent::publicIdentity(privateKey);
            if (identity.isEmpty()) {
                showMessage(I18n::t(QStringLiteral("ssh.invalid_key")), true);
                return false;
            }
            if (!publicKey.isEmpty() && (publicKey.contains(QLatin1Char('\n')) || publicKey.contains(QLatin1Char('\r'))
                || publicKey.simplified().section(QLatin1Char(' '), 0, 1) != identity)) {
                showMessage(I18n::t(QStringLiteral("ssh.key_mismatch")), true);
                return false;
            }
            data.sshPublicKey = publicKey.isEmpty() ? identity : publicKey.simplified();
            if (!attachment)
                data.password = privateKey;
        }
    }
    data.url = fields.value(QStringLiteral("url")).toString().trimmed();
    // Typed as one line, separated by spaces (or one per line): an address
    // has none of its own. The main URL is not repeated among them, and
    // with no main URL the first one takes its place.
    static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
    const QStringList typed = fields.value(QStringLiteral("extraUrls")).toString()
                                  .split(separators, Qt::SkipEmptyParts);
    for (const QString &url : typed) {
        if (url != data.url && !data.extraUrls.contains(url))
            data.extraUrls.append(url);
    }
    if (data.url.isEmpty() && !data.extraUrls.isEmpty())
        data.url = data.extraUrls.takeFirst();
    data.notes = fields.value(QStringLiteral("notes")).toString();
    data.extra = fields.value(QStringLiteral("extra")).toStringList();
    // Named after the entry, as an authenticator app would show it.
    {
        QString error;
        const std::optional<QString> otp =
            Totp::forSaving(fields.value(QStringLiteral("otp")).toString(),
                            fields.value(QStringLiteral("originalOtp")).toString(), title, &error);
        if (!otp) {
            showMessage(error, true);
            return false;
        }
        data.otp = Secret(*otp);
    }

    // Locking waits while busy, so the vault outlives the task.
    const Vault *vault = m_vault.data();
    const QString originalPath = fields.value(QStringLiteral("originalPath")).toString();
    runInBackground(
        [vault, isEdit, originalPath, path, group, data]() {
            TaskResult result;
            result.ok = isEdit ? vault->editEntry(originalPath, path, group, data, &result.error)
                               : vault->addEntry(path, group, data, &result.error);
            return result;
        },
        [this, isEdit, path, originalPath](const TaskResult &result) {
            // Whatever was read for these two is stale after a save.
            m_lazyMeta.remove(path);
            m_lazyMeta.remove(originalPath);
            // An icon kept on this computer follows the entry to its new
            // path; one in the database moved along with it.
            if (result.ok && isEdit && !m_vault.isNull() && !m_vault->storesIcons())
                m_entryIcons.moveLocal(m_vault.data(), originalPath, path);
            if (result.ok && isEdit)
                m_browser.entryMoved(originalPath, path);
            if (result.ok) {
                if (!isEdit)
                    m_history.recordUse(path, m_vault->path());
                showMessage(I18n::t(isEdit ? QStringLiteral("app.entry_edited")
                                           : QStringLiteral("app.entry_added")),
                            false);
            } else {
                showMessage(I18n::t(isEdit ? QStringLiteral("app.edit_error")
                                           : QStringLiteral("app.add_error")),
                            true);
            }
            refreshEntries();
        });
    return true;
}

void AppController::deleteEntry(const QString &entry) {
    if (!vaultReadyForChanges())
        return;

    const bool emptyGroup = isEmptyGroup(entry);
    const QString group = groupNameOf(entry);
    const Vault *vault = m_vault.data();
    runInBackground(
        [vault, emptyGroup, group, entry]() {
            TaskResult result;
            result.ok = emptyGroup ? vault->removeGroup(group, &result.error)
                                   : vault->removeEntry(entry, &result.error);
            return result;
        },
        [this, emptyGroup, entry](const TaskResult &result) {
            m_lazyMeta.remove(entry);
            if (result.ok && !emptyGroup && !m_vault.isNull() && !m_vault->storesIcons())
                m_entryIcons.setLocal(m_vault.data(), entry, QByteArray());
            if (result.ok && !emptyGroup)
                m_browser.entryRemoved(entry);
            if (result.ok) {
                showMessage(I18n::t(emptyGroup ? QStringLiteral("app.group_deleted")
                                               : QStringLiteral("app.entry_deleted")),
                            false);
                refreshEntries();
            } else {
                showMessage(I18n::t(QStringLiteral("app.delete_error"), QStringLiteral("err"),
                                    result.error),
                            true);
            }
        });
}

void AppController::chooseEntryIcon(const QString &entry, const QString &fileUrl) {
    const QUrl url(fileUrl);
    const QByteArray png = EntryIcons::fromFile(url.isLocalFile() ? url.toLocalFile() : fileUrl);
    if (png.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.icon_unreadable")), true);
        return;
    }
    storeEntryIcon(entry, png);
}

void AppController::clearEntryIcon(const QString &entry) {
    if (!m_entryIcons.has(entry)) {
        showMessage(I18n::t(QStringLiteral("app.no_icon")), true);
        return;
    }
    storeEntryIcon(entry, QByteArray());
}

void AppController::storeEntryIcon(const QString &entry, const QByteArray &png) {
    if (!vaultReadyForChanges())
        return;
    if (isEmptyGroup(entry)) {
        showMessage(I18n::t(QStringLiteral("app.empty_group")), true);
        return;
    }

    const QString done = I18n::t(png.isEmpty() ? QStringLiteral("app.icon_cleared")
                                               : QStringLiteral("app.icon_set"),
                                 QStringLiteral("entry"), entry);
    if (!m_vault->storesIcons()) {
        if (m_entryIcons.setLocal(m_vault.data(), entry, png)) {
            showMessage(done, false);
            rebuildRows();
        } else {
            showMessage(I18n::t(QStringLiteral("app.icon_error"), QStringLiteral("err"),
                                QDir::toNativeSeparators(QStandardPaths::writableLocation(
                                    QStandardPaths::GenericDataLocation) + QStringLiteral("/omapass/icons"))),
                        true);
        }
        return;
    }

    // In the database: written and saved like any edit.
    const Vault *vault = m_vault.data();
    runInBackground(
        [vault, entry, png]() {
            TaskResult result;
            result.ok = vault->setEntryIcon(entry, png, &result.error);
            return result;
        },
        [this, done](const TaskResult &result) {
            if (result.ok)
                showMessage(done, false);
            else
                showMessage(I18n::t(QStringLiteral("app.icon_error"), QStringLiteral("err"), result.error),
                            true);
            refreshEntries();
        });
}

void AppController::renameGroup(const QString &entry, const QString &newName) {
    if (!vaultReadyForChanges())
        return;

    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        showMessage(I18n::t(QStringLiteral("app.name_empty")), true);
        return;
    }

    const Vault *vault = m_vault.data();
    const QString group = groupNameOf(entry);
    runInBackground(
        [vault, group, trimmed]() {
            TaskResult result;
            result.ok = vault->renameGroup(group, trimmed, &result.error);
            return result;
        },
        [this](const TaskResult &result) {
            if (result.ok)
                showMessage(I18n::t(QStringLiteral("app.group_renamed")), false);
            else
                showMessage(I18n::t(QStringLiteral("app.rename_group_error"), QStringLiteral("err"),
                                    result.error),
                            true);
            refreshEntries();
        });
}

QStringList AppController::matchingGroups(const QString &prefix) const {
    return filterItems(m_groups, prefix);
}

void AppController::createKeepassDatabase(const QString &name, const QString &password) {
    if (m_busy || name.trimmed().isEmpty())
        return;

    setBusy(true);
    const Secret secret(password);
    const QString trimmedName = name.trimmed();
    QTimer::singleShot(0, this, [this, trimmedName, secret]() {
        QString path;
        QString error;
        if (!Vault::createKeepassDatabase(trimmedName, secret, &path, &error)) {
            setUnlockError(error);
            setBusy(false);
            return;
        }

        refreshDatabases();
        emit databaseCreated();
        openVault({path, VaultKind::Keepass}, secret);
        setBusy(false);
    });
}

QVariantList AppController::gpgKeys() const {
    QVariantList keys;
    const auto secretKeys = PassStore::listSecretKeys();
    for (const auto &key : secretKeys) {
        keys.append(QVariantMap{{QStringLiteral("id"), key.first},
                                {QStringLiteral("identity"), key.second}});
    }
    return keys;
}

QString AppController::defaultPassStoreDirectory() const {
    return QDir::homePath() + QStringLiteral("/.password-store");
}

// Completions for the directory field: the subdirectories matching the
// segment being typed (typing "/home/user/.pas" suggests folders under
// "/home/user/" starting with "pas"), hidden ones excluded.
QStringList AppController::directorySuggestions(const QString &path) const {
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString parent = slash < 0 ? QString() : path.left(slash + 1);
    const QString partial = slash < 0 ? path : path.mid(slash + 1);

    const QDir dir(parent.isEmpty() ? QStringLiteral(".") : parent);
    QStringList suggestions;
    const auto names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : names) {
        if (name.startsWith(QLatin1Char('.')))
            continue;
        if (name.startsWith(partial, Qt::CaseInsensitive))
            suggestions.append(parent + name);
    }
    return suggestions;
}

void AppController::createPassStore(const QString &directory, const QString &keyId) {
    if (m_busy)
        return;

    const QString dir = directory.trimmed();
    if (dir.isEmpty()) {
        setUnlockError(I18n::t(QStringLiteral("db_app.dir_empty")));
        return;
    }
    if (keyId.isEmpty()) {
        setUnlockError(I18n::t(QStringLiteral("db_app.no_gpg_key_selected")));
        return;
    }

    setBusy(true);
    QTimer::singleShot(0, this, [this, dir, keyId]() {
        QString error;
        if (!PassStore::initStore(dir, keyId, &error)) {
            setUnlockError(error);
            setBusy(false);
            return;
        }

        registerLocalDatabase(QDir::cleanPath(QFileInfo(dir).absoluteFilePath()), VaultKind::Pass);
        refreshDatabases();
        emit databaseCreated();

        // A brand-new store still needs its GPG passphrase, so it goes
        // through the same unlock prompt as any other database.
        m_pendingDatabase = {dir, VaultKind::Pass};
        m_hasPendingDatabase = true;
        setUnlockError(QString());
        emit pendingDatabaseChanged();
        setBusy(false);
    });
}

void AppController::openExistingKeepassDatabase(const QString &fileUrlOrPath) {
    const QString path = localPathFromInput(fileUrlOrPath);
    if (path.isEmpty())
        return;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        setUnlockError(I18n::t(QStringLiteral("db_app.existing_kdbx_invalid")));
        return;
    }

    registerLocalDatabase(path, VaultKind::Keepass);
    setUnlockError(QString());
    refreshDatabases();
    emit databaseCreated();
    askForPassword({path, VaultKind::Keepass});
}

void AppController::openExistingPassStore(const QString &dirUrlOrPath) {
    const QString path = localPathFromInput(dirUrlOrPath);
    if (path.isEmpty())
        return;

    if (!PassStore::isStore(path)) {
        setUnlockError(I18n::t(QStringLiteral("pass.store_not_initialized"),
                               QStringLiteral("path"), path));
        return;
    }

    registerLocalDatabase(path, VaultKind::Pass);
    setUnlockError(QString());
    refreshDatabases();
    emit databaseCreated();
    askForPassword({path, VaultKind::Pass});
}

bool AppController::openDatabasePath(const QString &pathOrUrl) {
    const QString path = localPathFromInput(pathOrUrl);
    if (path.isEmpty())
        return false;

    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        registerLocalDatabase(path, VaultKind::Keepass);
        setUnlockError(QString());
        refreshDatabases();
        askForPassword({path, VaultKind::Keepass});
        return true;
    }
    if (PassStore::isStore(path)) {
        registerLocalDatabase(path, VaultKind::Pass);
        setUnlockError(QString());
        refreshDatabases();
        askForPassword({path, VaultKind::Pass});
        return true;
    }
    return false;
}

void AppController::enableAutoSearch() {
    if (!Config::writeValues({{QStringLiteral("general.auto_search"), QStringLiteral("true")}})) {
        showMessage(I18n::t(QStringLiteral("settings.write_error")), true);
        return;
    }
    if (!m_config.autoSearch) {
        m_config.autoSearch = true;
        emit autoSearchChanged();
    }
    refreshDatabases();
}

bool AppController::onePasswordAvailable() const {
    return OnePasswordVault::isAvailable();
}

void AppController::setLoginStep(const QString &step) {
    m_loginStep = step;
    emit loginChanged();
}

void AppController::addBitwardenAccount() {
    if (!requireIdle())
        return;

    setUnlockError(QString());
    m_loginEmail.clear();
    m_loginServer = bwServerLabel(QSettings().value(lastServerSetting).toString());
    setLoginStep(QStringLiteral("credentials"));
}

void AppController::bitwardenLogin(const QString &server, const QString &email,
                                   const QString &password) {
    if (m_busy)
        return;

    const std::optional<QString> target = normalizeBwServer(server);
    if (!target) {
        setUnlockError(I18n::t(QStringLiteral("bitwarden.invalid_server")));
        return;
    }

    const QString trimmed = email.trimmed();
    if (trimmed.isEmpty() || !trimmed.contains(QLatin1Char('@'))) {
        setUnlockError(I18n::t(QStringLiteral("bitwarden.invalid_email")));
        return;
    }

    m_loginServer = bwServerLabel(*target);
    m_loginEmail = trimmed;
    emit loginChanged();

    setBusy(true);
    setUnlockError(QString());
    m_bitwardenLogin.start(*target, m_loginEmail, Secret(password));
}

void AppController::chooseBitwardenMethod(int method) {
    if (m_busy || !m_bitwardenLogin.methods().contains(method))
        return;

    // E-mail sends the code first, which takes a request; the others only
    // change what is asked.
    if (method == 1)
        setBusy(true);
    setUnlockError(QString());
    m_bitwardenLogin.chooseMethod(method);
}

QVariantList AppController::loginMethods() const {
    QVariantList methods;
    const QList<int> available = m_bitwardenLogin.methods();
    for (int method : available)
        methods.append(method);
    return methods;
}

void AppController::sendBitwardenCode(const QString &code) {
    if (m_busy || code.trimmed().isEmpty())
        return;

    setBusy(true);
    setUnlockError(QString());
    m_bitwardenLogin.sendCode(code);
}

void AppController::cancelBitwardenLogin() {
    m_bitwardenLogin.cancel();
    setBusy(false);
    setUnlockError(QString());
    setLoginStep(QString());
}

bool AppController::isBitwardenDatabase(int index) const {
    return index >= 0 && index < m_filteredDatabases.size()
        && !isOpen(m_filteredDatabases.at(index).path)
        && m_filteredDatabases.at(index).kind == VaultKind::Bitwarden;
}

void AppController::logoutBitwarden(int index) {
    if (!requireIdle() || !isBitwardenDatabase(index))
        return;

    const QString path = m_filteredDatabases.at(index).path;
    runInBackground(
        [path]() {
            TaskResult result;
            result.ok = BitwardenVault::logout(path, &result.error);
            // An account nobody is signed into has no business leaving its
            // master password behind in the keyring.
            if (result.ok)
                Pin::clear(path);
            return result;
        },
        [this](const TaskResult &result) {
            refreshPinState();
            refreshDatabases();
            showMessage(result.ok ? I18n::t(QStringLiteral("bitwarden.logged_out")) : result.error,
                        !result.ok);
        });
}

void AppController::addOnePasswordAccount() {
    if (!requireIdle())
        return;

    // Always the login sheet: every account `op` already has is in the list
    // on its own, so getting here means adding one more.
    setUnlockError(QString());
    m_onePasswordAdding = true;
    m_loginEmail.clear();
    m_loginAddress.clear();
    setLoginStep(QStringLiteral("opCredentials"));
}

void AppController::onePasswordLogin(const QString &address, const QString &email,
                                     const QString &secretKey, const QString &password,
                                     const QString &shorthand) {
    if (m_busy)
        return;

    const QString trimmedAddress = address.trimmed();
    const QString trimmedEmail = email.trimmed();
    if (trimmedAddress.isEmpty() || trimmedEmail.isEmpty()) {
        setUnlockError(I18n::t(QStringLiteral("onepassword.login_failed")));
        return;
    }

    // Without a nickname of their own, the account is filed under the part
    // of the e-mail before the @ — `op` would otherwise name it after the
    // address ("my"), which tells two accounts apart by nothing.
    QString nickname = shorthand.trimmed();
    if (nickname.isEmpty())
        nickname = opShorthandFor(trimmedEmail, trimmedAddress);

    QStringList taken;
    const QVector<OpAccount> known = OnePasswordVault::accounts();
    for (const OpAccount &account : known)
        taken.append(account.key());
    nickname = opUniqueShorthand(nickname, taken);

    m_loginAddress = trimmedAddress;
    m_loginEmail = trimmedEmail;
    emit loginChanged();

    m_onePasswordAdding = true;
    setBusy(true);
    setUnlockError(QString());
    m_onePasswordLogin.start(trimmedAddress, trimmedEmail, Secret(secretKey), Secret(password),
                             nickname);
}

void AppController::sendOnePasswordCode(const QString &code) {
    if (m_busy || code.trimmed().isEmpty())
        return;

    setBusy(true);
    setUnlockError(QString());
    m_onePasswordLogin.sendCode(code);
}

void AppController::cancelOnePasswordLogin() {
    m_onePasswordLogin.cancel();
    setBusy(false);
    setUnlockError(QString());
    setLoginStep(QString());
}

bool AppController::isOnePasswordDatabase(int index) const {
    return index >= 0 && index < m_filteredDatabases.size()
        && !isOpen(m_filteredDatabases.at(index).path)
        && m_filteredDatabases.at(index).kind == VaultKind::OnePassword;
}

void AppController::logoutOnePassword(int index) {
    if (!requireIdle() || !isOnePasswordDatabase(index))
        return;

    const DbRef ref = m_filteredDatabases.at(index);
    const QString account = OnePasswordVault::accountOf(ref.path);
    const QString path = ref.path;
    runInBackground(
        [account, path]() {
            TaskResult result;
            result.ok = OnePasswordVault::logout(account, &result.error);
            if (result.ok)
                Pin::clear(path);
            return result;
        },
        [this](const TaskResult &result) {
            refreshPinState();
            refreshDatabases();
            showMessage(result.ok ? I18n::t(QStringLiteral("onepassword.logged_out")) : result.error,
                        !result.ok);
        });
}

void AppController::enablePin(const QString &password, const QString &pin, bool allowText) {
    const DbRef target = pinTarget();
    if (m_busy || target.path.isEmpty())
        return;

    const QString invalid = Pin::validate(pin, pin, allowText);
    if (!invalid.isEmpty()) {
        showMessage(invalid, true);
        return;
    }

    const Secret secret(password);
    struct PinSetup {
        bool ok = false;
        QString error;
    };

    runInBackground(
        [target, pin, secret]() {
            PinSetup setup;
            // Checked before being stored, so a typo does not end up behind
            // the PIN — without opening the database, which for an account
            // backend would cost (and invalidate) a session.
            if (!Vault::verifySecret(target, secret, &setup.error))
                return setup;
            setup.ok = Pin::store(target.path, pin, secret, &setup.error);
            return setup;
        },
        [this](const PinSetup &setup) {
            refreshPinState();
            showMessage(setup.ok ? I18n::t(QStringLiteral("pin.enabled")) : setup.error, !setup.ok);
        });
}

void AppController::disablePin() {
    const DbRef target = pinTarget();
    if (target.path.isEmpty())
        return;

    Pin::clear(target.path);
    refreshPinState();
    showMessage(I18n::t(QStringLiteral("pin.disabled")), false);
}

QString AppController::validatePin(const QString &pin, const QString &confirm,
                                   bool allowText) const {
    return Pin::validate(pin, confirm, allowText);
}

QString AppController::pinWeakWarning(const QString &pin) const {
    return Pin::weakWarning(pin);
}

QString AppController::clearSshKeys() {
    if (m_sshTask) {
        m_sshTask->cancel();
        m_sshTask->waitForFinished();
        delete m_sshTask;
        m_sshTask = nullptr;
    }
    return m_sshAgent.clear();
}

QString AppController::closeVault() {
    if (m_vault.isNull())
        return {};
    const QString sshError = clearSshKeys();
    resetVaultView();
    m_vault->close();
    m_vault.reset();
    return sshError;
}

void AppController::resetVaultView() {
    stopMetaLoad();
    m_metaPool.waitForDone();
    m_lazyMeta.clear();
    m_metaArrived.clear();
    forgetShownOtp();
    m_browser.waitForReads();
}

QString AppController::message() const {
    if (m_messageText.isEmpty())
        return QString();

    if (m_clipboardCountdown < 0)
        return m_messageText;

    return I18n::t(QStringLiteral("ui.clipboard_clear_countdown"),
                   {{QStringLiteral("msg"), m_messageText},
                    {QStringLiteral("secs"), m_clipboardCountdown}});
}

void AppController::showMessage(const QString &text, bool isError) {
    m_messageText = text;
    m_messageIsError = isError;
    m_clipboardCountdown = -1;
    emit messageChanged();

    m_messageTimer.stop();
    QTimer::singleShot(3000, this, [this, text]() {
        // Only clears the message it was scheduled for; a newer one owns the
        // status line from the moment it is set.
        if (m_messageText == text && m_clipboardCountdown < 0)
            clearMessage();
    });
}

void AppController::showClipboardMessage(const QString &text) {
    m_messageText = text;
    m_messageIsError = false;
    m_clipboardCountdown = Clipboard::clearSecs;
    emit messageChanged();
    m_messageTimer.start();
}

void AppController::clearMessage() {
    m_messageTimer.stop();
    if (m_messageText.isEmpty())
        return;

    m_messageText.clear();
    m_clipboardCountdown = -1;
    emit messageChanged();
}

void AppController::setBusy(bool busy) {
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
    if (!m_busy && !m_syncing && (m_lockAllPending || !m_lockPendingPath.isEmpty()))
        QTimer::singleShot(0, this, &AppController::finishPendingLock);
}

void AppController::setUnlockError(const QString &error) {
    if (m_unlockError == error)
        return;

    m_unlockError = error;
    emit unlockErrorChanged();
}

QVariantMap AppController::settings() const {
    return {{QStringLiteral("autoSearch"), m_config.autoSearch},
            {QStringLiteral("path"), m_config.searchPath},
            {QStringLiteral("recency"), m_config.recencyEnabled},
            {QStringLiteral("lockEnabled"), m_config.lockMinutes.has_value()},
            {QStringLiteral("lockMinutes"), m_config.lockMinutes.value_or(10)},
            {QStringLiteral("wordlist"), m_config.wordlist},
            // With a vault open, its own choice; otherwise the default for
            // vaults that have none.
            {QStringLiteral("siteIcons"), m_vault.isNull() ? m_config.siteIcons : m_iconsDownload},
            {QStringLiteral("siteIconsDuckDuckGo"),
             m_vault.isNull() ? m_config.siteIconsDuckDuckGo : m_iconsDuckDuckGo},
            {QStringLiteral("iconsVault"),
             m_vault.isNull() ? QString() : Vault::displayName({m_vault->path(), m_vault->kind()})},
            {QStringLiteral("browserIntegration"), m_config.browserIntegration},
            {QStringLiteral("browserSupported"), BrowserIntegration::supported()},
            {QStringLiteral("sshAgentSetup"), SshAgent::isSetupInstalled()},
            {QStringLiteral("sshAgentSetupSupported"), SshAgent::setupSupported()},
            {QStringLiteral("sshAskpass"), SshAskpass::isInstalled()},
            {QStringLiteral("sshAskpassSupported"), SshAskpass::integrationSupported()},
            {QStringLiteral("wordlistInUse"), Generator::wordlistPath()},
            {QStringLiteral("configPath"), Config::configDir() + QStringLiteral("/config.toml")}};
}

// Saved to config.toml and applied right away — the sheet only offers the
// settings that can take effect without a restart.
void AppController::saveSettings(const QVariantMap &values) {
    const bool autoSearch = values.value(QStringLiteral("autoSearch")).toBool();
    QString path = values.value(QStringLiteral("path")).toString().trimmed();
    if (path.isEmpty())
        path = m_config.searchPath;
    if (path.startsWith(QStringLiteral("~/")))
        path = QDir::homePath() + path.mid(1);

    const bool recency = values.value(QStringLiteral("recency")).toBool();
    const bool lockEnabled = values.value(QStringLiteral("lockEnabled")).toBool();
    const int lockMinutes = qMax(1, values.value(QStringLiteral("lockMinutes")).toInt());
    QString wordlist = values.value(QStringLiteral("wordlist")).toString().trimmed();
    if (wordlist.isEmpty())
        wordlist = QStringLiteral("auto");
    const bool siteIcons = values.value(QStringLiteral("siteIcons")).toBool();
    const bool duckDuckGo = siteIcons && values.value(QStringLiteral("siteIconsDuckDuckGo")).toBool();
    const bool browser = BrowserIntegration::supported()
        && values.value(QStringLiteral("browserIntegration")).toBool();
    const bool sshAgentSetup = SshAgent::setupSupported()
        && values.value(QStringLiteral("sshAgentSetup")).toBool();
    const bool sshAskpass = SshAskpass::integrationSupported()
        && values.value(QStringLiteral("sshAskpass")).toBool();

    QMap<QString, QString> toml;
    toml.insert(QStringLiteral("general.auto_search"), autoSearch ? QStringLiteral("true") : QStringLiteral("false"));
    toml.insert(QStringLiteral("general.path"), Config::tomlString(path));
    toml.insert(QStringLiteral("general.recency"), recency ? QStringLiteral("true") : QStringLiteral("false"));
    toml.insert(QStringLiteral("general.lock_minutes"),
                lockEnabled ? QString::number(lockMinutes) : QStringLiteral("false"));
    toml.insert(QStringLiteral("generator.wordlist"), Config::tomlString(wordlist));
    // The icon boxes are the open vault's when there is one, and leave the
    // default alone.
    const bool perVault = !m_vault.isNull();
    const bool defaultIcons = perVault ? m_config.siteIcons : siteIcons;
    const bool defaultDuckDuckGo = perVault ? m_config.siteIconsDuckDuckGo : duckDuckGo;
    toml.insert(QStringLiteral("icons.download"), defaultIcons ? QStringLiteral("true") : QStringLiteral("false"));
    toml.insert(QStringLiteral("icons.duckduckgo"),
                defaultDuckDuckGo ? QStringLiteral("true") : QStringLiteral("false"));
    toml.insert(QStringLiteral("browser.enabled"), browser ? QStringLiteral("true") : QStringLiteral("false"));

    if (!Config::writeValues(toml)) {
        showMessage(I18n::t(QStringLiteral("settings.write_error")), true);
        return;
    }

    const bool autoSearchUpdated = autoSearch != m_config.autoSearch;
    const bool pathChanged = path != m_config.searchPath;
    const bool recencyChanged = recency != m_config.recencyEnabled;

    m_config.autoSearch = autoSearch;
    m_config.searchPath = path;
    if (autoSearchUpdated)
        emit autoSearchChanged();
    m_config.recencyEnabled = recency;
    m_config.lockMinutes = lockEnabled ? std::optional<int>(lockMinutes) : std::nullopt;
    m_config.wordlist = wordlist;
    m_config.siteIcons = defaultIcons;
    m_config.siteIconsDuckDuckGo = defaultDuckDuckGo;
    // Written to the vault only when it changed, so saving the other
    // settings never rewrites a database. Turning DuckDuckGo on is a reason
    // to ask again for the sites that had nothing of their own.
    QString iconError;
    if (perVault && (siteIcons != m_iconsDownload || duckDuckGo != m_iconsDuckDuckGo)) {
        QString error;
        if (!IconChoice::save(m_vault.data(), siteIcons, duckDuckGo, &error))
            iconError = I18n::t(QStringLiteral("settings.site_icons_save_error"), QStringLiteral("err"), error);
    }
    if (perVault && iconError.isEmpty()
        && (siteIcons != m_iconsDownload || duckDuckGo != m_iconsDuckDuckGo)) {
        m_iconsDownload = siteIcons;
        m_iconsDuckDuckGo = duckDuckGo;
        m_siteIcons.setDuckDuckGo(duckDuckGo);
        rebuildRows();
    }

    Generator::configure(m_config.wordlist, m_config.language);
    applyLockSettings();
    if (recencyChanged)
        m_history = History(recency);
    if (autoSearchUpdated || (autoSearch && pathChanged) || recencyChanged)
        refreshDatabases();

    if (iconError.isEmpty())
        showMessage(I18n::t(QStringLiteral("settings.saved")), false);
    else
        showMessage(iconError, true);

    if (SshAgent::setupSupported() && sshAgentSetup != SshAgent::isSetupInstalled()) {
        QString agentError;
        const bool ok = sshAgentSetup
            ? SshAgent::installSetup(nullptr, &agentError)
            : SshAgent::uninstallSetup(nullptr, &agentError);
        if (!ok)
            showMessage(agentError, true);
        else
            showMessage(I18n::t(sshAgentSetup ? QStringLiteral("ssh.agent_setup_installed")
                                              : QStringLiteral("ssh.agent_setup_uninstalled")), false);
    }

    if (SshAskpass::integrationSupported() && sshAskpass != SshAskpass::isInstalled()) {
        QString askpassError;
        const bool ok = sshAskpass
            ? SshAskpass::install(nullptr, &askpassError)
            : SshAskpass::uninstall(nullptr, &askpassError);
        if (!ok)
            showMessage(askpassError, true);
        else
            showMessage(I18n::t(sshAskpass ? QStringLiteral("ssh.askpass_installed")
                                           : QStringLiteral("ssh.askpass_uninstalled")), false);
    }
    // After the line above, so what turning it on says (the browsers it was
    // set up for, or why not) is what stays on the status line.
    const bool browserChanged = browser != m_config.browserIntegration;
    m_config.browserIntegration = browser;
    if (browserChanged)
        m_browser.setEnabled(browser);
}

QString AppController::importWordlist(const QString &fileUrl) {
    const QString source = QUrl(fileUrl).isLocalFile() ? QUrl(fileUrl).toLocalFile() : fileUrl;

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showMessage(I18n::t(QStringLiteral("settings.wordlist_unreadable")), true);
        return QString();
    }

    // keepassxc-cli refuses anything under 1296 words (6^4); saying so here
    // beats a passphrase mode that silently stops working.
    int words = 0;
    QTextStream in(&file);
    while (!in.atEnd()) {
        if (!in.readLine().trimmed().isEmpty())
            ++words;
    }
    if (words < Generator::minimumWordlistSize) {
        showMessage(I18n::t(QStringLiteral("settings.wordlist_too_small"),
                            {{QStringLiteral("words"), words},
                             {QStringLiteral("min"), Generator::minimumWordlistSize}}),
                    true);
        return QString();
    }

    const QString directory = Config::configDir() + QStringLiteral("/wordlists");
    if (!QDir().mkpath(directory)) {
        showMessage(I18n::t(QStringLiteral("settings.wordlist_copy_error")), true);
        return QString();
    }

    const QString target = directory + QLatin1Char('/') + QFileInfo(source).fileName();
    if (QFileInfo(source).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            showMessage(I18n::t(QStringLiteral("settings.wordlist_copy_error")), true);
            return QString();
        }
    }

    showMessage(I18n::t(QStringLiteral("settings.wordlist_imported"), QStringLiteral("words"),
                        QString::number(words)),
                false);
    return target;
}

namespace {

const auto generatorSetting = QStringLiteral("generator/");

GeneratorOptions optionsFromMap(const QVariantMap &map) {
    const GeneratorOptions defaults;
    GeneratorOptions options;
    options.passphrase = map.value(QStringLiteral("passphrase"), defaults.passphrase).toBool();
    options.length = map.value(QStringLiteral("length"), defaults.length).toInt();
    options.lower = map.value(QStringLiteral("lower"), defaults.lower).toBool();
    options.upper = map.value(QStringLiteral("upper"), defaults.upper).toBool();
    options.numbers = map.value(QStringLiteral("numbers"), defaults.numbers).toBool();
    options.special = map.value(QStringLiteral("special"), defaults.special).toBool();
    options.excludeSimilar = map.value(QStringLiteral("excludeSimilar"), defaults.excludeSimilar).toBool();
    options.exclude = map.value(QStringLiteral("exclude")).toString();
    options.custom = map.value(QStringLiteral("custom")).toString();
    options.words = map.value(QStringLiteral("words"), defaults.words).toInt();
    options.separator = map.value(QStringLiteral("separator"), defaults.separator).toString();
    return options;
}

QVariantMap optionsToMap(const GeneratorOptions &options) {
    return {{QStringLiteral("passphrase"), options.passphrase},
            {QStringLiteral("length"), options.length},
            {QStringLiteral("lower"), options.lower},
            {QStringLiteral("upper"), options.upper},
            {QStringLiteral("numbers"), options.numbers},
            {QStringLiteral("special"), options.special},
            {QStringLiteral("excludeSimilar"), options.excludeSimilar},
            {QStringLiteral("exclude"), options.exclude},
            {QStringLiteral("custom"), options.custom},
            {QStringLiteral("words"), options.words},
            {QStringLiteral("separator"), options.separator}};
}

}

// The sheet's last settings, so generating a second password does not mean
// setting everything up again.
QVariantMap AppController::generatorOptions() const {
    QSettings settings;
    QVariantMap map = optionsToMap(GeneratorOptions());
    for (auto it = map.begin(); it != map.end(); ++it)
        *it = settings.value(generatorSetting + it.key(), *it);

    QVariantMap result = optionsToMap(Generator::normalize(optionsFromMap(map)));
    result.insert(QStringLiteral("passphraseAvailable"), !Generator::wordlistPath().isEmpty());
    return result;
}

// Generating takes about ten milliseconds, so it happens right here rather
// than on a worker thread: the sheet regenerates on every change.
QVariantMap AppController::generate(const QVariantMap &options) {
    // Only with a vault open: generating is one step of putting a password
    // somewhere, not a standalone tool.
    if (m_vault.isNull())
        return {};

    const GeneratorOptions wanted = Generator::normalize(optionsFromMap(options));

    QSettings settings;
    const QVariantMap map = optionsToMap(wanted);
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        settings.setValue(generatorSetting + it.key(), it.value());

    Secret password;
    QString error;
    if (!Generator::generate(wanted, &password, &error)) {
        showMessage(error, true);
        return {};
    }

    QVariantMap result = map;
    result.insert(QStringLiteral("password"), password.toString());
    return result;
}

// Copies with the same treatment an entry's password gets: marked sensitive
// for the clipboard manager and wiped after the countdown.
void AppController::copySecret(const QString &password) {
    if (password.isEmpty())
        return;

    if (!Clipboard::copy(password)) {
        showMessage(I18n::t(QStringLiteral("app.copy_password_error")), true);
        return;
    }

    showClipboardMessage(I18n::t(QStringLiteral("generator.copied")));
    Clipboard::scheduleClear(Secret(password));
}

QVariantMap AppController::windowGeometry() const {
    QSettings settings;
    const QRect geometry = settings.value(windowGeometrySetting).toRect();

    // Positions are legitimately negative on monitors left of or above the
    // primary one, so validity travels separately instead of being encoded
    // as -1.
    return {{QStringLiteral("valid"), geometry.isValid()},
            {QStringLiteral("x"), geometry.x()},
            {QStringLiteral("y"), geometry.y()},
            {QStringLiteral("width"), geometry.width()},
            {QStringLiteral("height"), geometry.height()},
            {QStringLiteral("maximized"),
             settings.value(QStringLiteral("window/maximized"), false).toBool()}};
}

void AppController::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    settings.setValue(windowGeometrySetting, QRect(x, y, width, height));
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

// ---------------------------------------------------------------- browser

QList<const Vault *> AppController::browserVaults() const {
    QList<const Vault *> vaults;
    if (m_vault)
        vaults.append(m_vault.data());
    for (const auto &item : m_parked)
        vaults.append(item.vault.data());
    return vaults;
}

QStringList AppController::entriesFor(const Vault *vault) const {
    for (const auto &item : m_parked)
        if (item.vault.data() == vault)
            return item.entries;
    if (!vault || vault != m_vault.data())
        return {};
    QStringList entries;
    entries.reserve(m_allEntries.size());
    for (const QString &entry : m_allEntries) {
        if (!isEmptyGroup(entry))
            entries.append(entry);
    }
    return entries;
}

QHash<QString, EntryMeta> AppController::metaFor(const Vault *vault) const {
    for (const auto &item : m_parked)
        if (item.vault.data() == vault)
            return item.meta;
    if (!vault || vault != m_vault.data())
        return {};
    QHash<QString, EntryMeta> meta = m_listedMeta;
    for (auto it = m_lazyMeta.cbegin(); it != m_lazyMeta.cend(); ++it)
        meta.insert(it.key(), it.value());
    return meta;
}

// With the generator sheet's last settings, as KeePassXC uses its own.
Secret AppController::browserGeneratePassword() {
    Secret password;
    QString error;
    if (!Generator::generate(optionsFromMap(generatorOptions()), &password, &error))
        showMessage(error, true);
    return password;
}

bool AppController::browserChange(const Vault *vault, std::function<bool(const Vault *, QString *)> work,
                                     std::function<void(bool)> done) {
    if (!vault || !browserVaults().contains(vault) || m_busy || m_syncing || m_lockAllPending
        || !m_lockPendingPath.isEmpty() || m_hasPendingDatabase || !m_loginStep.isEmpty())
        return false;
    runInBackground(
        [vault, work]() {
            TaskResult result;
            result.ok = work(vault, &result.error);
            return result;
        },
        [this, vault, done](const TaskResult &result) {
            showMessage(result.ok ? I18n::t(QStringLiteral("browser.login_saved"))
                                  : I18n::t(QStringLiteral("browser.login_save_error"), QStringLiteral("err"),
                                            result.error),
                        !result.ok);
            if (vault == m_vault.data()) {
                refreshEntries();
            } else if (result.ok) {
                for (auto &item : m_parked) {
                    if (vault != item.vault.data())
                        continue;
                    QStringList groups;
                    vault->list(&item.entries, &groups);
                    item.entries.removeIf([this](const QString &entry) { return isEmptyGroup(entry); });
                    item.meta = vault->entryMeta();
                    break;
                }
            }
            done(result.ok);
        });
    return true;
}
