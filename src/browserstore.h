#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class Vault;

// What the browser integration keeps for a vault: the browsers associated
// with it and, per entry, the sites the user let have it or refused.
//
// For a KDBX 4 database that is the database itself, where KeePassXC keeps
// the same things (`KPXC_BROWSER_<name>` in its custom data, `KeePassXC-Browser
// Settings` in each entry's), so a browser associated there works here and
// back. Every other vault gets a file of its own on this computer.
class BrowserStore {
public:
    // The sites an entry was let out to, or kept from, by host.
    struct Access {
        QStringList allow;
        QStringList deny;
        bool operator==(const Access &other) const = default;
    };

    BrowserStore(const Vault *vault, const QString &localDir);

    // Identifies the vault to the extension: KeePassXC's hash (sha256 of the
    // root group's UUID) for KeePass, so both apps are the same database to
    // it; one of the vault's location otherwise.
    QString databaseHash() const;

    QString key(const QString &id) const;
    QStringList associations() const;
    bool setKey(const QString &id, const QString &key, QString *error);

    QHash<QString, Access> access() const;
    bool setAccess(const QString &entryPath, const Access &access, QString *error);
    // The local copy follows an entry renamed or removed in omapass; in the
    // database it moves with the entry by itself.
    void moveEntry(const QString &from, const QString &to);
    void removeEntry(const QString &entryPath);

    // An id for each of `entries` (or `groups`): KeePass's UUID where there
    // is one, else one made from the path.
    QHash<QString, QString> entryUuids(const QStringList &entries) const;
    QHash<QString, QString> groupUuids(const QStringList &groups) const;

    // KeePassXC's names, for the tests.
    static QString keyPrefix();
    static QString settingsKey();
    static QString encodeAccess(const Access &access);
    static Access decodeAccess(const QString &json);

private:
    bool inDatabase() const;
    QJsonObject readLocal() const;
    bool writeLocal(const QJsonObject &data, QString *error) const;
    QString localFile() const;

    const Vault *m_vault;
    QString m_localDir;
};
