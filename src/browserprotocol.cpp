#include "browserprotocol.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

#include "browsercrypto.h"

namespace BrowserProtocol {

QString errorMessage(int code) {
    // The extension shows these as they are, in English, as KeePassXC sends
    // them.
    switch (code) {
    case DatabaseNotOpened:
        return QStringLiteral("Database not opened");
    case DatabaseHashNotReceived:
        return QStringLiteral("Database hash not available");
    case ClientPublicKeyNotReceived:
        return QStringLiteral("Client public key not received");
    case CannotDecryptMessage:
        return QStringLiteral("Cannot decrypt message");
    case ActionCancelledOrDenied:
        return QStringLiteral("Action cancelled or denied");
    case CannotEncryptMessage:
        return QStringLiteral("Message encryption failed.");
    case AssociationFailed:
        return QStringLiteral("KeePassXC association failed, try again");
    case EncryptionKeyUnrecognized:
        return QStringLiteral("Encryption key is not recognized");
    case IncorrectAction:
        return QStringLiteral("Incorrect action");
    case EmptyMessageReceived:
        return QStringLiteral("Empty message received");
    case NoUrlProvided:
        return QStringLiteral("No URL provided");
    case NoLoginsFound:
        return QStringLiteral("No logins found");
    case NoGroupsFound:
        return QStringLiteral("No groups found");
    case CannotCreateNewGroup:
        return QStringLiteral("Cannot create new group");
    case NoValidUuidProvided:
        return QStringLiteral("No valid UUID provided");
    default:
        return QStringLiteral("Unknown error");
    }
}

namespace {

QUrl asUrl(const QString &text) {
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][\\w+.-]*://"));
    const QString trimmed = text.trimmed();
    if (scheme.match(trimmed).hasMatch())
        return QUrl(trimmed);
    return QUrl(QStringLiteral("https://") + trimmed);
}

}

QString hostOf(const QString &url) {
    if (url.trimmed().isEmpty())
        return QString();
    return asUrl(url).host().toLower();
}

bool urlMatches(const QString &entryUrl, const QString &siteUrl) {
    const QUrl entry = asUrl(entryUrl);
    const QUrl site = asUrl(siteUrl);
    const QString entryHost = entry.host().toLower();
    const QString siteHost = site.host().toLower();
    if (entryHost.isEmpty() || siteHost.isEmpty())
        return false;
    if (entry.port() > 0 && entry.port() != site.port())
        return false;
    if (siteHost == entryHost)
        return true;
    // A subdomain of the entry's host, but never of a bare top-level name
    // ("com") or of an address.
    if (!entryHost.contains(QLatin1Char('.')) || !QHostAddress(entryHost).isNull())
        return false;
    return siteHost.endsWith(QLatin1Char('.') + entryHost);
}

}

using namespace BrowserProtocol;

namespace {

const QString trueString = QStringLiteral("true");

bool isUuid(const QString &uuid) {
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{32}$"));
    return hex.match(uuid).hasMatch();
}

QJsonObject errorReply(const QString &action, int code) {
    QJsonObject reply;
    reply.insert(QStringLiteral("action"), action);
    reply.insert(QStringLiteral("errorCode"), QString::number(code));
    reply.insert(QStringLiteral("error"), errorMessage(code));
    return reply;
}

QByteArray fromBase64(const QJsonValue &value) {
    return QByteArray::fromBase64(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
}

}

struct BrowserSession::State {
    BrowserHost *host = nullptr;
    QByteArray clientPublicKey;
    BrowserCrypto::KeyPair keys;
    bool associated = false;

    // The decrypted message of `request`, or empty when it has none or it
    // does not open.
    QJsonObject decode(const QJsonObject &request) const {
        if (clientPublicKey.isEmpty())
            return QJsonObject();
        QByteArray plain;
        if (!BrowserCrypto::open(fromBase64(request.value(QStringLiteral("message"))),
                                 fromBase64(request.value(QStringLiteral("nonce"))), clientPublicKey,
                                 keys.secretKey, &plain))
            return QJsonObject();
        return QJsonDocument::fromJson(plain).object();
    }

    QJsonObject encrypted(const QString &action, const QJsonObject &request,
                          const QJsonObject &params = QJsonObject()) const {
        const QByteArray nonce =
            BrowserCrypto::incremented(fromBase64(request.value(QStringLiteral("nonce"))));
        const QString nonceText = QString::fromLatin1(nonce.toBase64());

        QJsonObject message = params;
        message.insert(QStringLiteral("version"), QLatin1String(hostVersion));
        message.insert(QStringLiteral("success"), trueString);
        message.insert(QStringLiteral("nonce"), nonceText);

        const QByteArray sealed = BrowserCrypto::seal(QJsonDocument(message).toJson(QJsonDocument::Compact),
                                                      nonce, clientPublicKey, keys.secretKey);
        if (sealed.isEmpty())
            return errorReply(action, CannotEncryptMessage);

        QJsonObject reply;
        reply.insert(QStringLiteral("action"), action);
        reply.insert(QStringLiteral("message"), QString::fromLatin1(sealed.toBase64()));
        reply.insert(QStringLiteral("nonce"), nonceText);
        const QString requestId = request.value(QStringLiteral("requestID")).toString();
        if (!requestId.isEmpty())
            reply.insert(QStringLiteral("requestID"), requestId);
        return reply;
    }
};

BrowserSession::BrowserSession(BrowserHost *host) : d(std::make_shared<State>()) {
    d->host = host;
}

BrowserSession::~BrowserSession() = default;

QByteArray BrowserSession::hostPublicKey() const {
    return d->keys.publicKey;
}

void BrowserSession::handle(const QJsonObject &request, const Reply &reply) {
    if (request.isEmpty()) {
        reply(errorReply(QString(), EmptyMessageReceived));
        return;
    }
    const QString action = request.value(QStringLiteral("action")).toString();
    if (action.isEmpty()) {
        reply(errorReply(action, IncorrectAction));
        return;
    }

    State &s = *d;
    BrowserHost *host = s.host;
    // Answers given later go through here, and only while the session lives.
    const std::weak_ptr<State> alive = d;
    const auto later = [alive, reply](const std::function<QJsonObject(State &)> &build) {
        if (const auto state = alive.lock())
            reply(build(*state));
    };

    if (action == QLatin1String("change-public-keys")) {
        const QByteArray publicKey = fromBase64(request.value(QStringLiteral("publicKey")));
        const QByteArray nonce = fromBase64(request.value(QStringLiteral("nonce")));
        if (publicKey.size() != BrowserCrypto::keyBytes || nonce.size() != BrowserCrypto::nonceBytes) {
            reply(errorReply(action, ClientPublicKeyNotReceived));
            return;
        }
        s.associated = false;
        s.clientPublicKey = publicKey;
        s.keys = BrowserCrypto::generateKeyPair();

        QJsonObject answer;
        answer.insert(QStringLiteral("action"), action);
        answer.insert(QStringLiteral("version"), QLatin1String(hostVersion));
        answer.insert(QStringLiteral("publicKey"), QString::fromLatin1(s.keys.publicKey.toBase64()));
        answer.insert(QStringLiteral("nonce"),
                      QString::fromLatin1(BrowserCrypto::incremented(nonce).toBase64()));
        answer.insert(QStringLiteral("success"), trueString);
        reply(answer);
        return;
    }

    if (s.clientPublicKey.isEmpty()) {
        reply(errorReply(action, ClientPublicKeyNotReceived));
        return;
    }
    if (!host->unlocked()) {
        if (request.value(QStringLiteral("triggerUnlock")).toString() == trueString)
            host->showUnlock();
        reply(errorReply(action, DatabaseNotOpened));
        return;
    }

    const QJsonObject message = s.decode(request);
    // generate-password used to come without a message, and still may.
    if (message.isEmpty() && action != QLatin1String("generate-password")) {
        reply(errorReply(action, CannotDecryptMessage));
        return;
    }
    const auto field = [&](const char *name) { return message.value(QLatin1String(name)).toString(); };
    const QString hash = host->databaseHash();

    if (action == QLatin1String("get-databasehash")) {
        if (hash.isEmpty()) {
            reply(errorReply(action, DatabaseHashNotReceived));
            return;
        }
        reply(s.encrypted(action, request, {{QStringLiteral("hash"), hash}}));
        return;
    }

    if (action == QLatin1String("associate")) {
        const QString key = field("key");
        if (key.isEmpty() || fromBase64(key) != s.clientPublicKey) {
            reply(errorReply(action, AssociationFailed));
            return;
        }
        const QString idKey = field("idKey").isEmpty() ? key : field("idKey");
        host->associate(idKey, [later, action, request, hash](const QString &id) {
            later([=](State &state) {
                if (id.isEmpty())
                    return errorReply(action, ActionCancelledOrDenied);
                state.associated = true;
                return state.encrypted(action, request,
                                       {{QStringLiteral("hash"), hash}, {QStringLiteral("id"), id}});
            });
        });
        return;
    }

    if (action == QLatin1String("test-associate")) {
        const QString id = field("id");
        const QString key = field("key");
        if (id.isEmpty() || key.isEmpty()) {
            reply(errorReply(action, DatabaseNotOpened));
            return;
        }
        if (host->associationKey(id) != key) {
            reply(errorReply(action, AssociationFailed));
            return;
        }
        s.associated = true;
        reply(s.encrypted(action, request, {{QStringLiteral("hash"), hash}, {QStringLiteral("id"), id}}));
        return;
    }

    if (action == QLatin1String("lock-database")) {
        host->lock();
        reply(s.encrypted(action, request));
        return;
    }

    if (action == QLatin1String("generate-password")) {
        const Secret password = host->generatePassword();
        if (password.isEmpty()) {
            reply(errorReply(action, ActionCancelledOrDenied));
            return;
        }
        reply(s.encrypted(action, request, {{QStringLiteral("password"), password.toString()}}));
        return;
    }

    // Everything below hands out or changes what is in the vault.
    if (!s.associated) {
        reply(errorReply(action, AssociationFailed));
        return;
    }

    if (action == QLatin1String("get-logins")) {
        BrowserLoginQuery query;
        query.url = field("url");
        query.submitUrl = field("submitUrl");
        query.httpAuth = field("httpAuth") == trueString;
        if (query.url.isEmpty()) {
            reply(errorReply(action, NoUrlProvided));
            return;
        }
        const QString id = field("id");
        host->findLogins(query, [later, action, request, hash, id](const QVector<BrowserLogin> &logins) {
            later([=](State &state) {
                if (logins.isEmpty())
                    return errorReply(action, NoLoginsFound);
                QJsonArray entries;
                for (const BrowserLogin &login : logins) {
                    QJsonObject entry{{QStringLiteral("login"), login.login},
                                      {QStringLiteral("name"), login.name},
                                      {QStringLiteral("password"), login.password.toString()},
                                      {QStringLiteral("uuid"), login.uuid},
                                      {QStringLiteral("group"), login.group}};
                    if (!login.totp.isEmpty())
                        entry.insert(QStringLiteral("totp"), login.totp);
                    entries.append(entry);
                }
                return state.encrypted(action, request,
                                       {{QStringLiteral("count"), entries.size()},
                                        {QStringLiteral("entries"), entries},
                                        {QStringLiteral("hash"), hash},
                                        {QStringLiteral("id"), id}});
            });
        });
        return;
    }

    if (action == QLatin1String("get-totp")) {
        const QString uuid = field("uuid");
        if (!isUuid(uuid)) {
            reply(errorReply(action, NoValidUuidProvided));
            return;
        }
        host->totp(uuid, [later, action, request](const QString &code) {
            later([=](State &state) {
                return state.encrypted(action, request, {{QStringLiteral("totp"), code}});
            });
        });
        return;
    }

    if (action == QLatin1String("set-login")) {
        BrowserSaveRequest save;
        save.url = field("url");
        save.submitUrl = field("submitUrl");
        save.login = field("login");
        save.password = Secret(field("password"));
        save.group = field("group");
        save.groupUuid = field("groupUuid");
        save.uuid = field("uuid");
        if (save.url.isEmpty()) {
            reply(errorReply(action, NoUrlProvided));
            return;
        }
        if (!save.uuid.isEmpty() && !isUuid(save.uuid)) {
            reply(errorReply(action, NoValidUuidProvided));
            return;
        }
        host->saveLogin(save, [later, action, request, hash](bool saved) {
            later([=](State &state) {
                return state.encrypted(action, request,
                                       {{QStringLiteral("count"), QJsonValue::Null},
                                        {QStringLiteral("entries"), QJsonValue::Null},
                                        {QStringLiteral("error"), saved ? QStringLiteral("success")
                                                                        : QStringLiteral("error")},
                                        {QStringLiteral("hash"), hash}});
            });
        });
        return;
    }

    if (action == QLatin1String("get-database-groups")) {
        const QJsonArray groups = host->groups();
        if (groups.isEmpty()) {
            reply(errorReply(action, NoGroupsFound));
            return;
        }
        reply(s.encrypted(action, request,
                          {{QStringLiteral("groups"), QJsonObject{{QStringLiteral("groups"), groups}}}}));
        return;
    }

    if (action == QLatin1String("create-new-group")) {
        const QString path = field("groupName");
        if (path.trimmed().isEmpty()) {
            reply(errorReply(action, CannotCreateNewGroup));
            return;
        }
        host->createGroup(path, [later, action, request](const QString &name, const QString &uuid) {
            later([=](State &state) {
                if (name.isEmpty() || uuid.isEmpty())
                    return errorReply(action, CannotCreateNewGroup);
                return state.encrypted(action, request,
                                       {{QStringLiteral("name"), name}, {QStringLiteral("uuid"), uuid}});
            });
        });
        return;
    }

    reply(errorReply(action, IncorrectAction));
}
