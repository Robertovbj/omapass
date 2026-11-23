#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringList>

class Vault;

// The icons the user chose for the entries of the open vault.
//
// KeePass keeps them in the database (its custom icons), so they travel with
// the file and KeePassXC shows them too. pass, Bitwarden and 1Password have
// no place for one, so they are kept on this computer, one PNG per entry
// under a folder per database. The file names are hashes of the database
// and the entry path: the folder does not spell out what is in the vault.
class EntryIcons {
public:
    explicit EntryIcons(const QString &localDir);

    // Reads the icons of `vault`'s `entries` (a null vault forgets them all).
    void load(const Vault *vault, const QStringList &entries);

    // `image://entryicon/<key>` for the entry, or empty when it has none.
    // The key changes with the icon, so the list never shows a stale one.
    QString urlFor(const QString &entryPath) const;
    bool has(const QString &entryPath) const { return m_keys.contains(entryPath); }

    // For the image provider; safe from its thread.
    QImage image(const QString &key) const;

    // The local half of choosing, moving and forgetting an icon, for
    // backends that do not store their own. `png` empty removes it.
    bool setLocal(const Vault *vault, const QString &entryPath, const QByteArray &png);
    void moveLocal(const Vault *vault, const QString &from, const QString &to);

    // A picked image file made into what is stored: a PNG of at most 32
    // pixels on a side. Empty when the file is not an image.
    static QByteArray fromFile(const QString &path);

private:
    QString folderFor(const Vault *vault) const;
    QString fileFor(const Vault *vault, const QString &entryPath) const;
    void insert(const QString &entryPath, const QByteArray &png);

    QString m_localDir;
    QHash<QString, QString> m_keys;        // entry path → key
    mutable QMutex m_mutex;                // guards m_images
    QHash<QString, QImage> m_images;       // key → image
};
