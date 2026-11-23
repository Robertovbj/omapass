#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

#include "secret.h"
#include "sshagent.h"

// A KeePass database (.kdbx, formats 3.1 and 4) read and written in-process,
// with Botan for the cryptography. The XML is kept as a tree and edited in
// place, so everything omapass does not model (history, custom fields,
// attachments, icons, custom data…) is written back as it was.
//
// Opened with a password only: key files and hardware keys are not
// supported. Paths are the group names joined with "/", the root group left
// out, and the entry title last — the spelling `keepassxc-cli` used.

// What the entry form shows.
struct KdbxEntry {
    Secret sshAttachment;  // Used when creating an entry with a KeeAgent attachment.
    QString title;
    QString username;
    Secret password;
    QString url;
    // KeePassXC's additional URLs: the attributes KP2A_URL, KP2A_URL_1, …
    // in that order.
    QStringList extraUrls;
    QString notes;
    // The one-time password as an otpauth URI: KeePassXC's `otp` attribute,
    // or KeePass 2's `TOTP Seed`/`TOTP Settings` pair converted to one.
    Secret otp;
};

enum class KdbxStatus {
    Ok,
    WrongPassword,   // the key did not open the database
    Unreadable,      // the file is missing or cannot be read
    Corrupt,         // not a KeePass database, or damaged
    Unsupported,     // a cipher, KDF or format version omapass does not do
};

class KdbxDatabase {
public:
    ~KdbxDatabase();
    KdbxDatabase(const KdbxDatabase &) = delete;
    KdbxDatabase &operator=(const KdbxDatabase &) = delete;

    // Opens `path`. On failure the result is null and `status` says why;
    // `detail` carries a short technical reason for the unsupported cases.
    static std::unique_ptr<KdbxDatabase> open(const QString &path, const Secret &password,
                                              KdbxStatus *status, QString *detail = nullptr);
    // A new, empty database: KDBX 4, AES-256, Argon2d, recycle bin on.
    // `legacy` makes a KDBX 3.1 one (AES-KDF), which exists to exercise that
    // format; nothing in the app asks for it.
    static std::unique_ptr<KdbxDatabase> create(const Secret &password, const QString &name,
                                                bool legacy = false);

    // Writes the database to `path`, in the format it was read in, replacing
    // the file only once the new one is complete. Empty on success, or the
    // error.
    QString save(const QString &path);

    // The tree flattened: every entry's path and every group's path.
    void list(QStringList *entries, QStringList *groups) const;

    bool entry(const QString &path, KdbxEntry *entry) const;
    QHash<QString, SshKeyConfig> sshKeys() const;
    std::optional<SshKeyConfig> sshKey(const QString &path, Secret *key) const;
    // Every entry with its path, for exporting. Entries in the recycle bin
    // are left out unless asked for.
    QList<QPair<QString, KdbxEntry>> allEntries(bool includeRecycled = true) const;

    // The mutating calls return an empty string on success, or the error.
    // `group` is the destination group path ("" is the root) and is created
    // when it does not exist. The title is the entry's own.
    QString addEntry(const QString &group, const KdbxEntry &entry);
    QString editEntry(const QString &oldPath, const QString &group, const KdbxEntry &entry);
    // To the recycle bin when the database has one on, deleted otherwise.
    QString removeEntry(const QString &path);
    QString renameGroup(const QString &oldPath, const QString &newName);

    // Custom icons, as KeePassXC keeps them: PNGs in the database's icon
    // pool, each entry pointing at one. Every entry that has one, by path.
    QHash<QString, QByteArray> entryIcons() const;
    // Points the entry at `png` (an identical icon already in the pool is
    // reused), or at none when `png` is empty. An icon no longer used by
    // anything — entry, group or history — leaves the pool. The entry
    // before the change goes to its history, as with any edit.
    QString setEntryIcon(const QString &path, const QByteArray &png);
    QString removeGroup(const QString &path);

    // UUIDs as KeePassXC spells them to the browser: 32 hex digits. The root
    // group's identifies the database; entries and groups are by path, the
    // recycle bin left out.
    QString rootUuid() const;
    QHash<QString, QString> entryUuids() const;
    QHash<QString, QString> groupUuids() const;

    // Custom data, the key/value pairs programs keep in a database for
    // themselves: the database's own (Meta), and each entry's (KDBX 4 only).
    // Setting an empty value removes the key. KeePassXC keeps its browser
    // associations and per-entry site permissions there.
    bool hasEntryCustomData() const;
    QHash<QString, QString> customData() const;
    QString setCustomData(const QString &key, const QString &value);
    // Entry path → value, for the entries that have `key`.
    QHash<QString, QString> entryCustomData(const QString &key) const;
    QString setEntryCustomData(const QString &path, const QString &key, const QString &value);

private:
    class Impl;
    explicit KdbxDatabase(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d;
};
