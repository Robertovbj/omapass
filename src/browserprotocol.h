#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

#include "secret.h"

// The KeePassXC-Browser protocol, so the extension that already exists for
// KeePassXC fills and saves logins from omapass. One BrowserSession per
// browser client; what it asks of the app goes through BrowserHost, which
// answers through callbacks because answering can mean asking the user.
//
// Mirrors KeePassXC's src/browser/BrowserAction.cpp: the same actions, the
// same replies, the same error codes, so the extension cannot tell the two
// apart. Passkeys, auto-type, deleting entries and listing every entry are
// left out and answered as unknown actions.

namespace BrowserProtocol {

// The version reported to the extension. It turns features on by version;
// 2.7.6 has everything served here and stops short of passkeys (2.7.7).
inline constexpr char hostVersion[] = "2.7.6";

enum Error {
    DatabaseNotOpened = 1,
    DatabaseHashNotReceived = 2,
    ClientPublicKeyNotReceived = 3,
    CannotDecryptMessage = 4,
    ActionCancelledOrDenied = 6,
    CannotEncryptMessage = 7,
    AssociationFailed = 8,
    EncryptionKeyUnrecognized = 10,
    IncorrectAction = 12,
    EmptyMessageReceived = 13,
    NoUrlProvided = 14,
    NoLoginsFound = 15,
    NoGroupsFound = 16,
    CannotCreateNewGroup = 17,
    NoValidUuidProvided = 18,
};

QString errorMessage(int code);

// Whether an entry saved for `entryUrl` belongs on `siteUrl`: the same host,
// or a subdomain of it, and the same port when the entry names one. An entry
// URL without a scheme is read as a host ("example.com"). As KeePassXC
// matches by default, without its wildcard and scheme options.
bool urlMatches(const QString &entryUrl, const QString &siteUrl);

// A host given as a URL or as a bare host, lowercase; empty when it has none.
QString hostOf(const QString &url);

}

struct BrowserLogin {
    QString uuid;   // 32 hex digits, as KeePassXC writes an entry's UUID
    QString name;
    QString login;
    Secret password;
    QString group;
    QString databaseHash;
    QString totp;   // the current code, empty when there is none
};

struct BrowserLoginQuery {
    QString url;
    QString submitUrl;
    bool httpAuth = false;
    QStringList databases = {};
};

struct BrowserSaveRequest {
    QString databaseHash;
    QString url;
    QString submitUrl;
    QString login;
    Secret password;
    QString group;      // a group path, or empty
    QString groupUuid;  // as get-database-groups gave it, or empty
    QString uuid;       // the entry to update; empty adds a new one
};

class BrowserHost {
public:
    virtual ~BrowserHost() = default;

    // A vault is open and unlocked.
    virtual bool unlocked() const = 0;
    // The extension asked to unlock (`triggerUnlock`): bring the window up.
    virtual void showUnlock() = 0;
    // Identifies the open vault to the extension, which keeps one
    // association per hash. Hex.
    virtual QString databaseHash() const = 0;
    virtual QStringList databaseHashes() const { return {databaseHash()}; }
    virtual QString keyForDatabase(const QString &hash, const QString &id) const {
        return hash == databaseHash() ? associationKey(id) : QString();
    }

    // The identification key stored under `id`, or empty.
    virtual QString associationKey(const QString &id) const = 0;
    // Asks the user to name a new association for `idKey`; `done` gets the
    // name it was stored under, or empty when refused.
    virtual void associate(const QString &idKey, std::function<void(const QString &id)> done) = 0;

    // The logins for a site the user lets the browser have; may ask first.
    virtual void findLogins(const BrowserLoginQuery &query,
                            std::function<void(const QVector<BrowserLogin> &)> done) = 0;
    // The current one-time code of an entry, empty when it has none.
    virtual void totp(const QString &uuid, std::function<void(const QString &)> done) = 0;
    virtual void databaseTotp(const QString &hash, const QString &uuid, std::function<void(const QString &)> done) {
        if (hash == databaseHash())
            totp(uuid, done);
        else
            done({});
    }
    // Adds a login, or updates the one named by `uuid` (which may ask).
    virtual void saveLogin(const BrowserSaveRequest &request, std::function<void(bool saved)> done) = 0;

    // The group tree as get-database-groups gives it: one root object with
    // name, uuid and children.
    virtual QJsonArray groups() const = 0;
    // Finds or creates the group at `path`; `done` gets its name and uuid,
    // both empty when it was not created.
    virtual void createGroup(const QString &path,
                             std::function<void(const QString &name, const QString &uuid)> done) = 0;

    virtual Secret generatePassword() = 0;
    virtual void lock() = 0;
};

class BrowserSession {
public:
    using Reply = std::function<void(const QJsonObject &)>;

    explicit BrowserSession(BrowserHost *host);
    ~BrowserSession();

    // Answers `request` through `reply`, now or once the host has. A reply
    // the host gives after the session is gone is dropped.
    void handle(const QJsonObject &request, const Reply &reply);

    // The session's public key, for the tests.
    QByteArray hostPublicKey() const;

private:
    struct State;
    std::shared_ptr<State> d;
};
