#include "browserstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>

#include "vault.h"

namespace {

QString sha256Hex(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString madeUuid(const QString &kind, const QString &path) {
    return sha256Hex(kind.toUtf8() + ':' + path.toUtf8()).left(32);
}

const QString createdPrefix = QStringLiteral("_CREATED_");

}

QString BrowserStore::keyPrefix() {
    return QStringLiteral("KPXC_BROWSER_");
}

QString BrowserStore::settingsKey() {
    return QStringLiteral("KeePassXC-Browser Settings");
}

QString BrowserStore::encodeAccess(const Access &access) {
    QStringList allow = access.allow;
    QStringList deny = access.deny;
    allow.sort();
    deny.sort();
    const QJsonObject object{{QStringLiteral("Allow"), QJsonArray::fromStringList(allow)},
                             {QStringLiteral("Deny"), QJsonArray::fromStringList(deny)},
                             {QStringLiteral("Realm"), QString()}};
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

BrowserStore::Access BrowserStore::decodeAccess(const QString &json) {
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    Access access;
    for (const QJsonValue &host : object.value(QStringLiteral("Allow")).toArray())
        access.allow.append(host.toString());
    for (const QJsonValue &host : object.value(QStringLiteral("Deny")).toArray())
        access.deny.append(host.toString());
    return access;
}

BrowserStore::BrowserStore(const Vault *vault, const QString &localDir)
    : m_vault(vault), m_localDir(localDir) {}

bool BrowserStore::inDatabase() const {
    return m_vault->storesBrowserData();
}

QString BrowserStore::localFile() const {
    const QByteArray id = QByteArray::number(int(m_vault->kind())) + ':' + m_vault->path().toUtf8();
    return m_localDir + QLatin1Char('/') + sha256Hex(id).left(32) + QStringLiteral(".json");
}

QJsonObject BrowserStore::readLocal() const {
    QFile file(localFile());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool BrowserStore::writeLocal(const QJsonObject &data, QString *error) const {
    if (!QDir().mkpath(m_localDir)) {
        *error = m_localDir;
        return false;
    }
    QSaveFile file(localFile());
    const QByteArray bytes = QJsonDocument(data).toJson();
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    // The keys identify the browser to the vault: for this user's eyes only.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QString BrowserStore::databaseHash() const {
    const QString root = m_vault->databaseUuid();
    if (!root.isEmpty())
        return sha256Hex(root.toUtf8());
    return sha256Hex("omapass:" + QByteArray::number(int(m_vault->kind())) + ':' + m_vault->path().toUtf8());
}

QString BrowserStore::key(const QString &id) const {
    if (id.isEmpty())
        return QString();
    if (inDatabase())
        return m_vault->customData().value(keyPrefix() + id);
    return readLocal().value(QStringLiteral("keys")).toObject().value(id).toString();
}

QStringList BrowserStore::associations() const {
    QStringList ids;
    if (inDatabase()) {
        const QHash<QString, QString> data = m_vault->customData();
        for (auto it = data.cbegin(); it != data.cend(); ++it) {
            if (it.key().startsWith(keyPrefix()))
                ids.append(it.key().mid(keyPrefix().size()));
        }
    } else {
        ids = readLocal().value(QStringLiteral("keys")).toObject().keys();
    }
    ids.sort();
    return ids;
}

bool BrowserStore::setKey(const QString &id, const QString &key, QString *error) {
    if (inDatabase()) {
        const QString created =
            key.isEmpty() ? QString() : QLocale::system().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
        return m_vault->setCustomData(keyPrefix() + id, key, error)
            && m_vault->setCustomData(createdPrefix + id, created, error);
    }
    QJsonObject data = readLocal();
    QJsonObject keys = data.value(QStringLiteral("keys")).toObject();
    if (key.isEmpty())
        keys.remove(id);
    else
        keys.insert(id, key);
    data.insert(QStringLiteral("keys"), keys);
    return writeLocal(data, error);
}

QHash<QString, BrowserStore::Access> BrowserStore::access() const {
    QHash<QString, Access> out;
    if (inDatabase()) {
        const QHash<QString, QString> settings = m_vault->entryCustomData(settingsKey());
        for (auto it = settings.cbegin(); it != settings.cend(); ++it)
            out.insert(it.key(), decodeAccess(it.value()));
        return out;
    }
    const QJsonObject entries = readLocal().value(QStringLiteral("access")).toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
        out.insert(it.key(), decodeAccess(QString::fromUtf8(
                                 QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact))));
    return out;
}

bool BrowserStore::setAccess(const QString &entryPath, const Access &access, QString *error) {
    const bool empty = access.allow.isEmpty() && access.deny.isEmpty();
    if (inDatabase())
        return m_vault->setEntryCustomData(entryPath, settingsKey(), empty ? QString() : encodeAccess(access),
                                           error);
    QJsonObject data = readLocal();
    QJsonObject entries = data.value(QStringLiteral("access")).toObject();
    if (empty)
        entries.remove(entryPath);
    else
        entries.insert(entryPath, QJsonDocument::fromJson(encodeAccess(access).toUtf8()).object());
    data.insert(QStringLiteral("access"), entries);
    return writeLocal(data, error);
}

void BrowserStore::moveEntry(const QString &from, const QString &to) {
    if (inDatabase() || from == to)
        return;
    QJsonObject data = readLocal();
    QJsonObject entries = data.value(QStringLiteral("access")).toObject();
    if (!entries.contains(from))
        return;
    entries.insert(to, entries.value(from));
    entries.remove(from);
    data.insert(QStringLiteral("access"), entries);
    QString error;
    writeLocal(data, &error);
}

void BrowserStore::removeEntry(const QString &entryPath) {
    if (inDatabase())
        return;
    QString error;
    setAccess(entryPath, Access(), &error);
}

QHash<QString, QString> BrowserStore::entryUuids(const QStringList &entries) const {
    const QHash<QString, QString> real = m_vault->entryUuids();
    QHash<QString, QString> out;
    for (const QString &entry : entries)
        out.insert(entry, real.value(entry, madeUuid(QStringLiteral("entry"), entry)));
    return out;
}

QHash<QString, QString> BrowserStore::groupUuids(const QStringList &groups) const {
    const QHash<QString, QString> real = m_vault->groupUuids();
    QHash<QString, QString> out;
    for (const QString &group : groups)
        out.insert(group, real.value(group, madeUuid(QStringLiteral("group"), group)));
    return out;
}
