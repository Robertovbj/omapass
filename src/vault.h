#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

#include "secret.h"
#include "sshagent.h"

// Abstraction over the supported password backends: KeePass databases (read
// in-process, see keepassvault.h), pass (through gpg, see passvault.h),
// Bitwarden (its server API, see bitwardenvault.h) and 1Password (through
// `op`, see onepasswordvault.h). The rest of the app only ever talks to
// Vault/DbRef, so no backend's command shapes leak into the interface.

enum class VaultKind { Keepass, Pass, Bitwarden, OnePassword };

// A database found by the search, before it is opened.
struct DbRef {
    QString path;
    VaultKind kind = VaultKind::Keepass;
};

// An entry's fields, as the union of what the formats carry. `extra` is only
// used by the pass backend, to preserve lines it does not model when an
// entry that already had them is edited.
struct EntryData {
    QString sshPublicKey;  // Set when creating a native SSH key entry.
    QString username;
    Secret password;
    QString url;
    // More addresses the same login is for: KeePassXC's KP2A_URL attributes,
    // Bitwarden's further URIs, 1Password's other URLs, pass's further
    // `url:` lines.
    QStringList extraUrls;
    QString notes;
    QStringList extra;
    // The one-time password as the backend stores it: usually an otpauth
    // URI, but a bare base32 secret or `steam://` from Bitwarden and
    // 1Password; Totp::parse reads all of them. Empty when there is none.
    Secret otp;
};

// What the entry list shows next to a path without reading each entry. `type`
// is "login" or "ssh"; username and url are empty where the backend cannot
// give them cheaply.
struct EntryMeta {
    QString type = QStringLiteral("login");
    QString username;
    QString url;
    QStringList extraUrls;
    std::optional<SshKeyConfig> ssh;
};

// Tags every group with no entries or subgroups below it, so it still shows
// up in the list. Shared by backends whose listing does not mark empty
// groups on its own.
void appendEmptyGroups(const QStringList &groups, QStringList *entries);

// The group a group lives in (`Work/Mail` → `Work`), empty at the root.
QString parentGroup(const QString &group);

// Interface every backend implements. The static half finds, creates and
// opens databases; an opened Vault is always one concrete backend.
class Vault {
public:
    virtual ~Vault() = default;

    // Suffix marking an empty group in the entry list (`Work/[empty]`), so it
    // stays navigable — and deletable — even with nothing inside. Translated,
    // but always produced and matched through this one function (the locale
    // cannot change mid-run), so display and detection never drift apart.
    static QString emptyGroupSuffix();
    static QString kindLabel(VaultKind kind);
    // How a database is named in the list: the file or directory name, or
    // the account for Bitwarden and 1Password.
    static QString displayName(const DbRef &ref);

    // Finds KeePassXC databases (.kdbx) and pass stores (any directory with a
    // .gpg-id) under `searchPath` — the same configurable directory for both,
    // so a store anywhere below it is found, not just ~/.password-store —
    // plus the Bitwarden and 1Password accounts added through omapass, if
    // any.
    static QVector<DbRef> findDatabases(const QString &searchPath);

    // The same search in its two halves, so a caller can show the first as
    // soon as it is known: files on disk (kdbx, pass) never wait for the
    // account backends, which run external programs (`op`) and can take
    // seconds or hang.
    static QVector<DbRef> findLocalDatabases(const QString &searchPath);
    static QVector<DbRef> findAccountDatabases();

    static bool createKeepassDatabase(const QString &name, const Secret &password,
                                      QString *createdPath, QString *error);

    // Opens and authenticates against `ref`, validating the password or
    // passphrase before handing back a usable Vault.
    static Vault *open(const DbRef &ref, const Secret &secret, QString *error);

    // Checks a password without opening the database, for storing it behind
    // a PIN: opening would cost a session the caller does not want (and, for
    // the account backends, would end the one already open). A 1Password
    // account has nothing local to check against, so it accepts what it is
    // given — a wrong password shows up at the first PIN unlock, which then
    // drops the PIN.
    static bool verifySecret(const DbRef &ref, const Secret &secret, QString *error);

    VaultKind kind() const { return m_kind; }
    QString path() const { return m_path; }

    // Lists entries and groups, tagging empty groups with the suffix above.
    virtual void list(QStringList *entries, QStringList *groups) const = 0;

    // Type, login and URL of the entries, keyed by entry path, for the
    // columns of the list. Only what the backend already holds: reading each
    // entry would cost a process per row. Entries missing here are plain
    // logins with nothing else to show.
    virtual QHash<QString, EntryMeta> entryMeta() const { return {}; }

    // For a backend that cannot list login and URL cheaply (1Password: one
    // process per entry): says so, and gives one entry's meta on request. The
    // list shows the rest at once and fills these in as they arrive.
    virtual bool loadsMetaLazily() const { return false; }
    virtual bool fetchEntryMeta(const QString &, EntryMeta *) const { return false; }

    // An entry's display title. Under KeePass this can differ from the last
    // path segment (Title is its own attribute); under pass the title is
    // always the filename.
    virtual QString titleFor(const QString &entryPath, const QString &fallback) const = 0;

    // Fetches only the password, so copying does not pay for the other
    // attributes.
    virtual bool fetchPassword(const QString &entryPath, Secret *password,
                               QString *error) const = 0;
    virtual bool fetchEntry(const QString &entryPath, EntryData *data, QString *error) const = 0;
    virtual bool fetchSshKey(const QString &, const QString &, Secret *, QString *) const { return false; }

    virtual bool addEntry(const QString &entryPath, const QString &group, const EntryData &data,
                          QString *error) const = 0;
    // Edits the entry at `oldPath`, moving it to `newPath` when the path
    // changed (group and/or title).
    virtual bool editEntry(const QString &oldPath, const QString &newPath, const QString &group,
                           const EntryData &data, QString *error) const = 0;
    virtual bool removeEntry(const QString &entryPath, QString *error) const = 0;

    // Renames the last segment of `oldGroup` to `newName`, keeping the parent
    // group and everything below it intact.
    virtual bool renameGroup(const QString &oldGroup, const QString &newName,
                             QString *error) const = 0;
    virtual bool removeGroup(const QString &group, QString *error) const = 0;

    // Icons the user chose for entries, kept in the database itself
    // (KeePass's custom icons). A backend without a place for them keeps
    // none, and the app stores them on this computer instead.
    virtual bool storesIcons() const { return false; }
    // Entry path → PNG, for the entries that have one.
    virtual QHash<QString, QByteArray> entryIcons() const { return {}; }
    // Sets one (an empty `png` removes it) and saves the database.
    virtual bool setEntryIcon(const QString &, const QByteArray &, QString *) const { return false; }

    // Ids the browser integration can hand out for entries and groups:
    // KeePass's UUIDs, as 32 hex digits, by path. Empty where the backend
    // has none, and the integration makes its own.
    virtual QString databaseUuid() const { return {}; }
    virtual QHash<QString, QString> entryUuids() const { return {}; }
    virtual QHash<QString, QString> groupUuids() const { return {}; }

    // The browser integration's data kept in the database itself, where
    // KeePassXC keeps it (KDBX 4): the database's custom data, and one key
    // of each entry's. A backend without a place for it keeps none, and the
    // app stores it on this computer instead. An empty value removes.
    virtual bool storesBrowserData() const { return false; }
    virtual QHash<QString, QString> customData() const { return {}; }
    virtual bool setCustomData(const QString &, const QString &, QString *) const { return false; }
    // Entry path → value, for the entries that have `key`.
    virtual QHash<QString, QString> entryCustomData(const QString &) const { return {}; }
    virtual bool setEntryCustomData(const QString &, const QString &, const QString &, QString *) const {
        return false;
    }

    // Something the user should hear about what was opened — entries that
    // could not be read, say — or empty when all is well.
    virtual QString warning() const { return {}; }

    // Releases whatever the backend holds outside this process when the vault
    // is locked. Nothing to do for the file-based backends.
    virtual void close() {}

protected:
    Vault(VaultKind kind, const QString &path) : m_kind(kind), m_path(path) {}

private:
    VaultKind m_kind;
    QString m_path;
};
