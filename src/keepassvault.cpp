#include "keepassvault.h"

#include <QFileInfo>
#include <QMutexLocker>

#include <algorithm>

#include "i18n.h"
#include "totp.h"

namespace {

QString lastSegment(const QString &path) {
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

}  // namespace

QString KeepassVault::openError(KdbxStatus status, const QString &detail) {
    switch (status) {
    case KdbxStatus::WrongPassword:
        return I18n::t(QStringLiteral("backend.wrong_password"));
    case KdbxStatus::Unreadable:
        return I18n::t(QStringLiteral("keepass.unreadable"), QStringLiteral("err"), detail);
    case KdbxStatus::Unsupported:
        return I18n::t(QStringLiteral("keepass.unsupported"), QStringLiteral("detail"), detail);
    case KdbxStatus::Corrupt:
    case KdbxStatus::Ok:
        break;
    }
    return I18n::t(QStringLiteral("keepass.corrupt"));
}

KeepassVault::KeepassVault(const QString &path, const Secret &password, std::unique_ptr<KdbxDatabase> db)
    : Vault(VaultKind::Keepass, path), m_password(password), m_db(std::move(db)) {
    stamp();
}

KeepassVault *KeepassVault::unlock(const QString &path, const Secret &password, QString *error) {
    KdbxStatus status = KdbxStatus::Corrupt;
    QString detail;
    std::unique_ptr<KdbxDatabase> db = KdbxDatabase::open(path, password, &status, &detail);
    if (!db) {
        *error = openError(status, detail);
        return nullptr;
    }
    return new KeepassVault(path, password, std::move(db));
}

void KeepassVault::stamp() const {
    const QFileInfo info(path());
    m_modified = info.lastModified();
    m_size = info.size();
}

bool KeepassVault::refresh(QString *error) const {
    const QFileInfo info(path());
    if (info.lastModified() == m_modified && info.size() == m_size)
        return true;

    KdbxStatus status = KdbxStatus::Corrupt;
    QString detail;
    std::unique_ptr<KdbxDatabase> db = KdbxDatabase::open(path(), m_password, &status, &detail);
    if (!db) {
        *error = openError(status, detail);
        return false;
    }
    m_db = std::move(db);
    stamp();
    return true;
}

// Reads memory back from the file, throwing away whatever an operation did
// before it failed, so that the next successful one does not save it.
void KeepassVault::rollback() const {
    KdbxStatus status = KdbxStatus::Corrupt;
    if (std::unique_ptr<KdbxDatabase> db = KdbxDatabase::open(path(), m_password, &status)) {
        m_db = std::move(db);
        stamp();
    }
}

// Writes the in-memory database. When that fails the file is left as it was,
// and memory is read back from it so the two do not drift.
bool KeepassVault::commit(QString *error) const {
    const QString failure = m_db->save(path());
    if (failure.isEmpty()) {
        stamp();
        return true;
    }
    *error = failure;
    rollback();
    return false;
}

// The outcome of an operation on the in-memory database: saved when it
// worked, rolled back when it did not.
bool KeepassVault::finish(const QString &failure, QString *error) const {
    if (failure.isEmpty())
        return commit(error);
    *error = failure;
    rollback();
    return false;
}

void KeepassVault::list(QStringList *entries, QStringList *groups) const {
    entries->clear();
    groups->clear();

    const QMutexLocker locker(&m_mutex);
    QString error;
    if (!refresh(&error))
        return;
    m_db->list(entries, groups);
    appendEmptyGroups(*groups, entries);
}

QHash<QString, EntryMeta> KeepassVault::entryMeta() const {
    QHash<QString, EntryMeta> meta;
    const QMutexLocker locker(&m_mutex);
    QString error;
    if (!refresh(&error))
        return meta;
    const auto all = m_db->allEntries();
    const auto ssh = m_db->sshKeys();
    for (const auto &item : all) {
        EntryMeta entry;
        entry.username = item.second.username;
        entry.url = item.second.url;
        entry.extraUrls = item.second.extraUrls;
        entry.hasOtp = !item.second.otp.isEmpty() && Totp::parse(item.second.otp.toString()).has_value();
        const auto key = ssh.constFind(item.first);
        if (key != ssh.cend())
            entry.ssh = key.value();
        meta.insert(item.first, entry);
    }
    return meta;
}

bool KeepassVault::fetchSshKey(const QString &entryPath, const QString &id, Secret *key,
                                QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    const auto settings = m_db->sshKey(entryPath, key);
    return settings && settings->id == id && !key->isEmpty();
}

QString KeepassVault::titleFor(const QString &entryPath, const QString &fallback) const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    KdbxEntry entry;
    if (!refresh(&error) || !m_db->entry(entryPath, &entry) || entry.title.isEmpty())
        return fallback;
    return entry.title;
}

bool KeepassVault::fetchPassword(const QString &entryPath, Secret *password, QString *error) const {
    EntryData data;
    if (!fetchEntry(entryPath, &data, error))
        return false;
    *password = data.password;
    return true;
}

bool KeepassVault::fetchEntry(const QString &entryPath, EntryData *data, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;

    KdbxEntry entry;
    if (!m_db->entry(entryPath, &entry)) {
        *error = I18n::t(QStringLiteral("keepass.entry_not_found"), QStringLiteral("entry"), entryPath);
        return false;
    }
    data->username = entry.username;
    data->sshPublicKey = entry.sshPublicKey;
    data->password = entry.password;
    data->url = entry.url;
    data->extraUrls = entry.extraUrls;
    data->notes = entry.notes;
    data->extra.clear();
    data->otp = entry.otp;
    return true;
}

bool KeepassVault::addEntry(const QString &entryPath, const QString &group, const EntryData &data,
                            QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;

    KdbxEntry entry;
    entry.title = lastSegment(entryPath);
    entry.username = data.username;
    entry.sshPublicKey = data.sshPublicKey;
    entry.password = data.password;
    entry.url = data.url;
    entry.extraUrls = data.extraUrls;
    entry.notes = data.notes;
    entry.otp = data.otp;

    if (!data.sshPublicKey.isEmpty()) {
        entry.sshAttachment = data.password;
        entry.password.clear();
    }
    return finish(m_db->addEntry(group, entry), error);
}

bool KeepassVault::editEntry(const QString &oldPath, const QString &newPath, const QString &group,
                             const EntryData &data, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;

    KdbxEntry entry;
    entry.title = lastSegment(newPath);
    entry.username = data.username;
    entry.sshPublicKey = data.sshPublicKey;
    entry.password = data.password;
    entry.url = data.url;
    entry.extraUrls = data.extraUrls;
    entry.notes = data.notes;
    entry.otp = data.otp;

    return finish(m_db->editEntry(oldPath, group, entry), error);
}

bool KeepassVault::removeEntry(const QString &entryPath, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    return finish(m_db->removeEntry(entryPath), error);
}

bool KeepassVault::renameGroup(const QString &oldGroup, const QString &newName, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    return finish(m_db->renameGroup(oldGroup, newName), error);
}

QHash<QString, QByteArray> KeepassVault::entryIcons() const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    if (!refresh(&error))
        return {};
    return m_db->entryIcons();
}

bool KeepassVault::setEntryIcon(const QString &entryPath, const QByteArray &png, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    return finish(m_db->setEntryIcon(entryPath, png), error);
}

// The reads below answer from what is in memory when the file cannot be
// read again: the browser asks often, and a sync client replacing the file
// mid-read is no reason to tell it the database is gone.
QString KeepassVault::databaseUuid() const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    refresh(&error);
    return m_db->rootUuid();
}

QHash<QString, QString> KeepassVault::entryUuids() const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    refresh(&error);
    return m_db->entryUuids();
}

QHash<QString, QString> KeepassVault::groupUuids() const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    refresh(&error);
    return m_db->groupUuids();
}

bool KeepassVault::storesBrowserData() const {
    const QMutexLocker locker(&m_mutex);
    return m_db->hasEntryCustomData();
}

QHash<QString, QString> KeepassVault::customData() const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    refresh(&error);
    return m_db->customData();
}

bool KeepassVault::setCustomData(const QString &key, const QString &value, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    if (m_db->customData().value(key) == value)
        return true;
    return finish(m_db->setCustomData(key, value), error);
}

QHash<QString, QString> KeepassVault::entryCustomData(const QString &key) const {
    const QMutexLocker locker(&m_mutex);
    QString error;
    refresh(&error);
    return m_db->entryCustomData(key);
}

bool KeepassVault::setEntryCustomData(const QString &entryPath, const QString &key, const QString &value,
                                      QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    if (m_db->entryCustomData(key).value(entryPath) == value)
        return true;
    return finish(m_db->setEntryCustomData(entryPath, key, value), error);
}

bool KeepassVault::removeGroup(const QString &group, QString *error) const {
    const QMutexLocker locker(&m_mutex);
    if (!refresh(error))
        return false;
    return finish(m_db->removeGroup(group), error);
}
