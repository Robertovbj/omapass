#pragma once

#include <QDateTime>
#include <QMutex>

#include <memory>

#include "kdbx.h"
#include "vault.h"

// Backend for KeePass databases (.kdbx), read and written in-process (see
// kdbx.h). The database is opened once, with one key derivation, and kept
// decrypted in memory; every change is saved to the file straight away.
//
// The file is checked before each use: when another program (KeePassXC
// itself, a sync client) has replaced it, it is read again, so a change made
// here never writes over one made there.
class KeepassVault : public Vault {
public:
    // Opens the database, or returns null with the reason in `error` (a
    // wrong password is reported as such).
    static KeepassVault *unlock(const QString &path, const Secret &password, QString *error);

    void list(QStringList *entries, QStringList *groups) const override;
    QHash<QString, EntryMeta> entryMeta() const override;
    QString titleFor(const QString &entryPath, const QString &fallback) const override;
    bool fetchPassword(const QString &entryPath, Secret *password, QString *error) const override;
    bool fetchEntry(const QString &entryPath, EntryData *data, QString *error) const override;
    bool addEntry(const QString &entryPath, const QString &group, const EntryData &data,
                  QString *error) const override;
    bool editEntry(const QString &oldPath, const QString &newPath, const QString &group,
                   const EntryData &data, QString *error) const override;
    bool removeEntry(const QString &entryPath, QString *error) const override;
    bool renameGroup(const QString &oldGroup, const QString &newName, QString *error) const override;
    bool removeGroup(const QString &group, QString *error) const override;

    bool storesIcons() const override { return true; }
    QHash<QString, QByteArray> entryIcons() const override;
    bool setEntryIcon(const QString &entryPath, const QByteArray &png, QString *error) const override;

    QString databaseUuid() const override;
    QHash<QString, QString> entryUuids() const override;
    QHash<QString, QString> groupUuids() const override;
    bool storesBrowserData() const override;
    QHash<QString, QString> customData() const override;
    bool setCustomData(const QString &key, const QString &value, QString *error) const override;
    QHash<QString, QString> entryCustomData(const QString &key) const override;
    bool setEntryCustomData(const QString &entryPath, const QString &key, const QString &value,
                            QString *error) const override;

    // The message for a database that would not open.
    static QString openError(KdbxStatus status, const QString &detail);

private:
    KeepassVault(const QString &path, const Secret &password, std::unique_ptr<KdbxDatabase> db);

    // Called with the mutex held.
    bool refresh(QString *error) const;
    bool commit(QString *error) const;
    bool finish(const QString &failure, QString *error) const;
    void rollback() const;
    void stamp() const;

    Secret m_password;
    mutable QMutex m_mutex;
    mutable std::unique_ptr<KdbxDatabase> m_db;
    mutable QDateTime m_modified;
    mutable qint64 m_size = -1;
};
