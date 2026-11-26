#include "bwaccount.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

const auto refPrefix = QStringLiteral("bitwarden:");

// Marks a field that should decrypt and does not.
struct Undecryptable {};

QJsonValue decryptField(const QJsonValue &value, const BwKey &key) {
    if (value.isNull() || value.isUndefined())
        return QJsonValue::Null;
    if (!value.isString())
        throw Undecryptable();
    const std::optional<QString> text = BwCrypto::decryptString(value.toString(), key);
    if (!text)
        throw Undecryptable();
    return *text;
}

QJsonObject decryptLogin(const QJsonObject &cipher, const BwKey &key) {
    const QJsonObject login = cipher.value(QStringLiteral("login")).toObject();

    QJsonArray uris;
    const QJsonArray encryptedUris = login.value(QStringLiteral("uris")).toArray();
    for (const QJsonValue &value : encryptedUris) {
        const QJsonObject uri = value.toObject();
        uris.append(QJsonObject{{QStringLiteral("match"), uri.value(QStringLiteral("match"))},
                                {QStringLiteral("uri"), decryptField(uri.value(QStringLiteral("uri")), key)}});
    }

    return QJsonObject{
        {QStringLiteral("username"), decryptField(login.value(QStringLiteral("username")), key)},
        {QStringLiteral("password"), decryptField(login.value(QStringLiteral("password")), key)},
        {QStringLiteral("totp"), decryptField(login.value(QStringLiteral("totp")), key)},
        {QStringLiteral("uris"), uris},
    };
}

QJsonObject decryptCard(const QJsonObject &cipher, const BwKey &key) {
    const QJsonObject card = cipher.value(QStringLiteral("card")).toObject();
    QJsonObject out;
    for (const char *field : {"cardholderName", "brand", "number", "expMonth", "expYear", "code"})
        out.insert(QLatin1String(field), decryptField(card.value(QLatin1String(field)), key));
    return out;
}

QJsonObject decryptIdentity(const QJsonObject &cipher, const BwKey &key) {
    const QJsonObject id = cipher.value(QStringLiteral("identity")).toObject();
    QJsonObject out;
    for (const char *field : {"title", "firstName", "middleName", "lastName", "address1",
                              "address2", "address3", "city", "state", "postalCode", "country",
                              "company", "email", "phone", "ssn", "username", "passportNumber",
                              "licenseNumber"}) {
        out.insert(QLatin1String(field), decryptField(id.value(QLatin1String(field)), key));
    }
    return out;
}

// A field to send: encrypted, or null when there is nothing in it.
QJsonValue encryptedField(const QString &text, const BwKey &key) {
    if (text.isEmpty())
        return QJsonValue::Null;
    QByteArray bytes = text.toUtf8();
    const std::optional<QString> encrypted = BwCrypto::encrypt(bytes, key);
    bytes.fill('\0');
    return encrypted ? QJsonValue(*encrypted) : QJsonValue(QJsonValue::Null);
}

// How many old passwords an item keeps, as in the official clients.
constexpr int passwordHistoryLength = 5;

QString fileFor(const QString &refPath) {
    const QByteArray hash = QCryptographicHash::hash(refPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    return BwAccount::directory() + QLatin1Char('/') + QString::fromLatin1(hash.left(32))
        + QStringLiteral(".json");
}

QJsonObject kdfToJson(const BwKdf &kdf) {
    return {{QStringLiteral("type"), kdf.type},
            {QStringLiteral("iterations"), kdf.iterations},
            {QStringLiteral("memory"), kdf.memory},
            {QStringLiteral("parallelism"), kdf.parallelism}};
}

BwKdf kdfFromJson(const QJsonObject &object) {
    BwKdf kdf;
    kdf.type = object.value(QStringLiteral("type")).toInt(-1);
    kdf.iterations = object.value(QStringLiteral("iterations")).toInt();
    kdf.memory = object.value(QStringLiteral("memory")).toInt();
    kdf.parallelism = object.value(QStringLiteral("parallelism")).toInt();
    return kdf;
}

}

QString BwAccount::directory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/omapass/bitwarden");
}

QString BwAccount::refPath(const QString &email, const QString &server) {
    return server.isEmpty() ? refPrefix + email : refPrefix + email + QLatin1Char('|') + server;
}

QString BwAccount::emailOf(const QString &path) {
    const QString rest = path.startsWith(refPrefix) ? path.mid(refPrefix.size()) : path;
    return rest.section(QLatin1Char('|'), 0, 0);
}

QString BwAccount::serverOf(const QString &path) {
    const QString rest = path.startsWith(refPrefix) ? path.mid(refPrefix.size()) : path;
    return rest.section(QLatin1Char('|'), 1);
}

QStringList BwAccount::refPaths() {
    QStringList paths;
    const QDir dir(directory());
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        // Only the two plain fields are wanted; the rest stays encrypted
        // either way.
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QString email = root.value(QStringLiteral("email")).toString();
        if (!email.isEmpty())
            paths.append(refPath(email, root.value(QStringLiteral("server")).toString()));
    }
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

std::optional<BwAccountState> BwAccount::load(const QString &path) {
    QFile file(fileFor(path));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return std::nullopt;

    const QJsonObject root = document.object();
    BwAccountState state;
    state.server = root.value(QStringLiteral("server")).toString();
    state.email = root.value(QStringLiteral("email")).toString();
    state.userId = root.value(QStringLiteral("userId")).toString();
    state.salt = root.value(QStringLiteral("salt")).toString();
    state.kdf = kdfFromJson(root.value(QStringLiteral("kdf")).toObject());
    state.userKey = root.value(QStringLiteral("userKey")).toString();
    state.privateKey = root.value(QStringLiteral("privateKey")).toString();
    state.refreshToken = root.value(QStringLiteral("refreshToken")).toString();
    state.organizationKeys = root.value(QStringLiteral("organizationKeys")).toObject();
    state.organizationNames = root.value(QStringLiteral("organizationNames")).toObject();
    state.folders = root.value(QStringLiteral("folders")).toArray();
    state.collections = root.value(QStringLiteral("collections")).toArray();
    state.sends = root.value(QStringLiteral("sends")).toArray();
    state.ciphers = root.value(QStringLiteral("ciphers")).toArray();

    if (state.email.isEmpty() || refPath(state.email, state.server) != path)
        return std::nullopt;
    return state;
}

bool BwAccount::save(const BwAccountState &state, QString *error) {
    QDir dir;
    if (!dir.mkpath(directory())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(directory());
        return false;
    }
    QFile::setPermissions(directory(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);

    const QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("server"), state.server},
        {QStringLiteral("email"), state.email},
        {QStringLiteral("userId"), state.userId},
        {QStringLiteral("salt"), state.salt},
        {QStringLiteral("kdf"), kdfToJson(state.kdf)},
        {QStringLiteral("userKey"), state.userKey},
        {QStringLiteral("privateKey"), state.privateKey},
        {QStringLiteral("refreshToken"), state.refreshToken},
        {QStringLiteral("organizationKeys"), state.organizationKeys},
        {QStringLiteral("organizationNames"), state.organizationNames},
        {QStringLiteral("folders"), state.folders},
        {QStringLiteral("collections"), state.collections},
        {QStringLiteral("sends"), state.sends},
        {QStringLiteral("ciphers"), state.ciphers},
    };

    // Written aside and renamed over the old one: a crash midway never
    // leaves half a vault behind.
    QSaveFile file(fileFor(refPath(state.email, state.server)));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void BwAccount::remove(const QString &path) {
    QFile::remove(fileFor(path));
}

BwAccount::Unlock BwAccount::unlock(const BwAccountState &state, const Secret &password, BwKeys *keys) {
    if (state.salt.isEmpty() || !state.userKey.startsWith(QLatin1String("2.")))
        return Unlock::Unsupported;
    if ((state.kdf.type == 0 || state.kdf.type == 1) && !BwCrypto::kdfIsSafe(state.kdf))
        return Unlock::UnsafeKdf;

    const std::optional<BwKey> masterKey = BwCrypto::deriveMasterKey(password, state.salt, state.kdf);
    if (!masterKey)
        return Unlock::Unsupported;

    // The user key is an authenticated type 2 string: failing to open it
    // with a correctly derived key means the password is wrong.
    const std::optional<BwKey> userKey = BwCrypto::unwrapKey(state.userKey, *masterKey);
    if (!userKey)
        return Unlock::WrongPassword;

    keys->user = *userKey;
    keys->organizations = organizationKeys(state, *userKey);
    return Unlock::Ok;
}

QHash<QString, BwKey> BwAccount::organizationKeys(const BwAccountState &state, const BwKey &user) {
    QHash<QString, BwKey> keys;
    if (state.organizationKeys.isEmpty())
        return keys;

    const std::optional<BwBytes> privateKey = BwCrypto::decrypt(state.privateKey, user);
    if (!privateKey)
        return keys;

    for (auto it = state.organizationKeys.constBegin(); it != state.organizationKeys.constEnd(); ++it) {
        const std::optional<BwBytes> bytes = BwCrypto::rsaDecrypt(it.value().toString(), *privateKey);
        const std::optional<BwKey> key = bytes ? BwCrypto::keyFromBytes(*bytes) : std::nullopt;
        if (key)
            keys.insert(it.key(), *key);
    }
    return keys;
}

void BwAccount::applySync(BwAccountState *state, const QJsonObject &sync) {
    const QJsonObject profile = sync.value(QStringLiteral("profile")).toObject();

    const QString userId = profile.value(QStringLiteral("id")).toString();
    if (!userId.isEmpty())
        state->userId = userId;

    const QString userKey = profile.value(QStringLiteral("key")).toString();
    if (!userKey.isEmpty())
        state->userKey = userKey;

    QString privateKey = profile.value(QStringLiteral("privateKey")).toString();
    if (privateKey.isEmpty()) {
        privateKey = profile.value(QStringLiteral("accountKeys")).toObject()
                         .value(QStringLiteral("publicKeyEncryptionKeyPair")).toObject()
                         .value(QStringLiteral("wrappedPrivateKey")).toString();
    }
    if (!privateKey.isEmpty())
        state->privateKey = privateKey;

    // Newer servers say how the master password opens the account; older
    // ones leave it to prelogin, which is what the state already has.
    const QJsonObject unlock = sync.value(QStringLiteral("userDecryption")).toObject()
                                   .value(QStringLiteral("masterPasswordUnlock")).toObject();
    if (!unlock.isEmpty()) {
        const QJsonObject kdf = unlock.value(QStringLiteral("kdf")).toObject();
        if (kdf.contains(QStringLiteral("kdfType"))) {
            BwKdf synced;
            synced.type = kdf.value(QStringLiteral("kdfType")).toInt();
            synced.iterations = kdf.value(QStringLiteral("iterations")).toInt();
            synced.memory = kdf.value(QStringLiteral("memory")).toInt();
            synced.parallelism = kdf.value(QStringLiteral("parallelism")).toInt();
            // A server that turns the KDF down to nothing does not get it
            // saved: the next unlock keeps the parameters that worked.
            if (BwCrypto::kdfIsSafe(synced))
                state->kdf = synced;
        }
        const QString salt = unlock.value(QStringLiteral("salt")).toString();
        if (!salt.isEmpty())
            state->salt = salt;
        const QString wrapped = unlock.value(QStringLiteral("masterKeyEncryptedUserKey")).toString();
        if (!wrapped.isEmpty())
            state->userKey = wrapped;
    }

    QJsonObject organizations;
    QJsonObject organizationNames;
    const QJsonArray profileOrganizations = profile.value(QStringLiteral("organizations")).toArray();
    for (const QJsonValue &value : profileOrganizations) {
        const QJsonObject organization = value.toObject();
        const QString id = organization.value(QStringLiteral("id")).toString();
        const QString key = organization.value(QStringLiteral("key")).toString();
        const QString name = organization.value(QStringLiteral("name")).toString();
        if (!id.isEmpty() && !key.isEmpty()) {
            organizations.insert(id, key);
            if (!name.isEmpty())
                organizationNames.insert(id, name);
        }
    }
    state->organizationKeys = organizations;
    state->organizationNames = organizationNames;

    state->folders = sync.value(QStringLiteral("folders")).toArray();
    state->collections = sync.value(QStringLiteral("collections")).toArray();
    state->sends = sync.value(QStringLiteral("sends")).toArray();
    state->ciphers = sync.value(QStringLiteral("ciphers")).toArray();
}

std::optional<BwKey> BwAccount::cipherKey(const QJsonObject &cipher, const BwKeys &keys) {
    const QString organizationId = cipher.value(QStringLiteral("organizationId")).toString();
    BwKey baseKey = keys.user;
    if (!organizationId.isEmpty()) {
        if (!keys.organizations.contains(organizationId))
            return std::nullopt;
        baseKey = keys.organizations.value(organizationId);
    }

    // Newer items carry a key of their own, wrapped with the user's or the
    // organisation's key; older ones use that key directly.
    const QJsonValue wrapped = cipher.value(QStringLiteral("key"));
    if (wrapped.isString() && !wrapped.toString().isEmpty())
        return BwCrypto::unwrapKey(wrapped.toString(), baseKey);
    return baseKey;
}

std::optional<QJsonObject> BwAccount::decryptCipher(const QJsonObject &cipher, const BwKeys &keys,
                                                    bool includeDeleted) {
    const int type = cipher.value(QStringLiteral("type")).toInt();
    if (type < 1 || type > 5)
        return std::nullopt;
    const QJsonValue deleted = cipher.value(QStringLiteral("deletedDate"));
    if (!includeDeleted && !deleted.isNull() && !deleted.isUndefined())
        return std::nullopt;

    const std::optional<BwKey> key = cipherKey(cipher, keys);
    if (!key)
        return std::nullopt;

    try {
        QJsonObject item;
        item.insert(QStringLiteral("object"), QStringLiteral("item"));
        item.insert(QStringLiteral("id"), cipher.value(QStringLiteral("id")));
        item.insert(QStringLiteral("type"), type);
        item.insert(QStringLiteral("organizationId"), cipher.value(QStringLiteral("organizationId")));
        item.insert(QStringLiteral("folderId"), cipher.value(QStringLiteral("folderId")));
        item.insert(QStringLiteral("collectionIds"), cipher.value(QStringLiteral("collectionIds")));
        item.insert(QStringLiteral("revisionDate"), cipher.value(QStringLiteral("revisionDate")));
        item.insert(QStringLiteral("deletedDate"), deleted.isUndefined() ? QJsonValue(QJsonValue::Null) : deleted);
        item.insert(QStringLiteral("viewPassword"), cipher.value(QStringLiteral("viewPassword")).toBool(true));
        item.insert(QStringLiteral("name"), decryptField(cipher.value(QStringLiteral("name")), *key));
        item.insert(QStringLiteral("notes"), decryptField(cipher.value(QStringLiteral("notes")), *key));
        const QJsonArray encFields = cipher.value(QStringLiteral("fields")).toArray();
        if (!encFields.isEmpty()) {
            QJsonArray decFields;
            for (const QJsonValue &fv : encFields) {
                const QJsonObject fo = fv.toObject();
                const int fType = fo.value(QStringLiteral("type")).toInt();
                QJsonObject df;
                df.insert(QStringLiteral("type"), fType);
                df.insert(QStringLiteral("name"), decryptField(fo.value(QStringLiteral("name")), *key));
                df.insert(QStringLiteral("value"), decryptField(fo.value(QStringLiteral("value")), *key));
                if (fo.contains(QStringLiteral("linkedId")))
                    df.insert(QStringLiteral("linkedId"), fo.value(QStringLiteral("linkedId")));
                decFields.append(df);
            }
            item.insert(QStringLiteral("fields"), decFields);
        }
        if (type == 2) {
            QJsonObject note = cipher.value(QStringLiteral("secureNote")).toObject();
            if (!note.contains(QStringLiteral("type")))
                note.insert(QStringLiteral("type"), 0);
            item.insert(QStringLiteral("secureNote"), note);
        } else if (type == 3) {
            item.insert(QStringLiteral("card"), decryptCard(cipher, *key));
        } else if (type == 4) {
            item.insert(QStringLiteral("identity"), decryptIdentity(cipher, *key));
        } else if (type == 5) {
            const QJsonObject ssh = cipher.value(QStringLiteral("sshKey")).toObject();
            item.insert(QStringLiteral("sshKey"),
                        QJsonObject{{QStringLiteral("privateKey"), decryptField(ssh.value(QStringLiteral("privateKey")), *key)},
                                    {QStringLiteral("publicKey"), decryptField(ssh.value(QStringLiteral("publicKey")), *key)},
                                    {QStringLiteral("keyFingerprint"), decryptField(ssh.value(QStringLiteral("keyFingerprint")), *key)}});
        } else {
            item.insert(QStringLiteral("login"), decryptLogin(cipher, *key));
        }
        return item;
    } catch (const Undecryptable &) {
        return std::nullopt;
    }
}

QString BwAccount::decryptFolder(const QJsonObject &folder, const BwKey &user) {
    const std::optional<QString> name =
        BwCrypto::decryptString(folder.value(QStringLiteral("name")).toString(), user);
    if (!name)
        return QString();
    QString text = *name;
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    return text;
}

QString BwAccount::decryptCollection(const QJsonObject &collection, const BwKeys &keys) {
    const QString orgId = collection.value(QStringLiteral("organizationId")).toString();
    if (orgId.isEmpty() || !keys.organizations.contains(orgId))
        return QString();
    const std::optional<QString> name =
        BwCrypto::decryptString(collection.value(QStringLiteral("name")).toString(),
                                keys.organizations.value(orgId));
    return name ? name->trimmed() : QString();
}

std::optional<QJsonObject> BwAccount::decryptSend(const QJsonObject &send, const BwKey &user,
                                                  const QString &server) {
    const QString id = send.value(QStringLiteral("id")).toString();
    const QString accessId = send.value(QStringLiteral("accessId")).toString();
    const QString wrappedKey = send.value(QStringLiteral("key")).toString();
    if (id.isEmpty() || accessId.isEmpty() || wrappedKey.isEmpty())
        return std::nullopt;

    const std::optional<BwBytes> seed = BwCrypto::decrypt(wrappedKey, user);
    if (!seed || seed->size() != 16)
        return std::nullopt;
    const std::optional<BwKey> sendKey = BwCrypto::deriveSendKey(*seed);
    if (!sendKey)
        return std::nullopt;

    const std::optional<QString> name =
        BwCrypto::decryptString(send.value(QStringLiteral("name")).toString(), *sendKey);
    if (!name)
        return std::nullopt;

    QJsonObject out;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("accessId"), accessId);
    out.insert(QStringLiteral("type"), send.value(QStringLiteral("type")).toInt());
    out.insert(QStringLiteral("name"), *name);
    out.insert(QStringLiteral("url"), BwCrypto::sendUrl(server, accessId, *seed));
    out.insert(QStringLiteral("accessCount"), send.value(QStringLiteral("accessCount")).toInt());
    out.insert(QStringLiteral("maxAccessCount"), send.value(QStringLiteral("maxAccessCount")).toInt());
    out.insert(QStringLiteral("deletionDate"), send.value(QStringLiteral("deletionDate")).toString());
    out.insert(QStringLiteral("disabled"), send.value(QStringLiteral("disabled")).toBool());
    return out;
}

QJsonObject BwAccount::cipherRequest(const QJsonObject &encrypted, const QString &previousPassword,
                                     const QString &name, const QString &folderId,
                                     const EntryData &data, const BwKey &key,
                                     const QString &userId) {
    QJsonObject request = encrypted;
    if (request.isEmpty()) {
        request = QJsonObject{
            {QStringLiteral("type"), 1},
            {QStringLiteral("organizationId"), QJsonValue::Null},
            {QStringLiteral("favorite"), false},
            {QStringLiteral("reprompt"), 0},
            {QStringLiteral("fields"), QJsonValue::Null},
            {QStringLiteral("passwordHistory"), QJsonValue::Null},
            {QStringLiteral("login"), QJsonObject{{QStringLiteral("uris"), QJsonArray()},
                                                  {QStringLiteral("totp"), QJsonValue::Null}}},
        };
    } else {
        // The server checks this against its own copy, so an edit made from
        // a stale one is refused rather than silently undoing another.
        request.insert(QStringLiteral("lastKnownRevisionDate"), encrypted.value(QStringLiteral("revisionDate")));
        // Strip response-only fields from /sync or /ciphers/{id} so the server
        // never prefers stale legacy `data` or rejects read-only metadata.
        for (const char *keyName : {"data", "object", "id", "creationDate", "revisionDate",
                                    "deletedDate", "archivedDate", "edit", "viewPassword",
                                    "organizationUseTotp", "attachments", "permissions",
                                    "collectionIds"}) {
            request.remove(QLatin1String(keyName));
        }
    }

    // Servers check that what they are given was encrypted by the account
    // sending it, so a client with stale keys cannot overwrite an item.
    if (!userId.isEmpty())
        request.insert(QStringLiteral("encryptedFor"), userId);
    request.insert(QStringLiteral("name"), encryptedField(name, key));
    request.insert(QStringLiteral("notes"), encryptedField(data.notes, key));
    request.insert(QStringLiteral("folderId"), folderId.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(folderId));
    if (data.customFieldsLoaded) {
        QJsonArray encFields;
        for (const CustomField &f : data.customFields) {
            if (f.name.trimmed().isEmpty() && f.value.isEmpty())
                continue;
            QJsonObject fo;
            fo.insert(QStringLiteral("type"), f.type);
            fo.insert(QStringLiteral("name"), encryptedField(f.name, key));
            fo.insert(QStringLiteral("value"), encryptedField(f.value.toString(), key));
            fo.insert(QStringLiteral("linkedId"), QJsonValue::Null);
            encFields.append(fo);
        }
        request.insert(QStringLiteral("fields"), encFields.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                                     : QJsonValue(encFields));
    }

    if ((data.isNote && (encrypted.isEmpty() || request.value(QStringLiteral("type")).toInt() == 2))
        || request.value(QStringLiteral("type")).toInt() == 2) {
        request.remove(QStringLiteral("login"));
        request.remove(QStringLiteral("card"));
        request.remove(QStringLiteral("identity"));
        request.remove(QStringLiteral("sshKey"));
        request.insert(QStringLiteral("type"), 2);
        QJsonObject note = request.value(QStringLiteral("secureNote")).toObject();
        if (!note.contains(QStringLiteral("type")))
            note.insert(QStringLiteral("type"), 0);
        request.insert(QStringLiteral("secureNote"), note);
        return request;
    }

    if ((data.isCard && (encrypted.isEmpty() || request.value(QStringLiteral("type")).toInt() == 3))
        || request.value(QStringLiteral("type")).toInt() == 3) {
        request.remove(QStringLiteral("login"));
        request.remove(QStringLiteral("secureNote"));
        request.remove(QStringLiteral("identity"));
        request.remove(QStringLiteral("sshKey"));
        request.insert(QStringLiteral("type"), 3);
        QJsonObject card = request.value(QStringLiteral("card")).toObject();
        card.insert(QStringLiteral("cardholderName"), encryptedField(data.card.cardholderName, key));
        card.insert(QStringLiteral("brand"), encryptedField(data.card.brand, key));
        card.insert(QStringLiteral("number"), encryptedField(data.card.number.toString(), key));
        card.insert(QStringLiteral("expMonth"), encryptedField(data.card.expMonth, key));
        card.insert(QStringLiteral("expYear"), encryptedField(data.card.expYear, key));
        card.insert(QStringLiteral("code"), encryptedField(data.card.code.toString(), key));
        request.insert(QStringLiteral("card"), card);
        return request;
    }

    if ((data.isIdentity && (encrypted.isEmpty() || request.value(QStringLiteral("type")).toInt() == 4))
        || request.value(QStringLiteral("type")).toInt() == 4) {
        request.remove(QStringLiteral("login"));
        request.remove(QStringLiteral("secureNote"));
        request.remove(QStringLiteral("card"));
        request.remove(QStringLiteral("sshKey"));
        request.insert(QStringLiteral("type"), 4);
        QJsonObject id = request.value(QStringLiteral("identity")).toObject();
        id.insert(QStringLiteral("title"), encryptedField(data.identity.title, key));
        id.insert(QStringLiteral("firstName"), encryptedField(data.identity.firstName, key));
        id.insert(QStringLiteral("middleName"), encryptedField(data.identity.middleName, key));
        id.insert(QStringLiteral("lastName"), encryptedField(data.identity.lastName, key));
        id.insert(QStringLiteral("company"), encryptedField(data.identity.company, key));
        id.insert(QStringLiteral("email"), encryptedField(data.identity.email, key));
        id.insert(QStringLiteral("phone"), encryptedField(data.identity.phone, key));
        id.insert(QStringLiteral("address1"), encryptedField(data.identity.address1, key));
        id.insert(QStringLiteral("address2"), encryptedField(data.identity.address2, key));
        id.insert(QStringLiteral("address3"), encryptedField(data.identity.address3, key));
        id.insert(QStringLiteral("city"), encryptedField(data.identity.city, key));
        id.insert(QStringLiteral("state"), encryptedField(data.identity.state, key));
        id.insert(QStringLiteral("postalCode"), encryptedField(data.identity.postalCode, key));
        id.insert(QStringLiteral("country"), encryptedField(data.identity.country, key));
        id.insert(QStringLiteral("ssn"), encryptedField(data.identity.ssn.toString(), key));
        id.insert(QStringLiteral("passportNumber"), encryptedField(data.identity.passportNumber.toString(), key));
        id.insert(QStringLiteral("licenseNumber"), encryptedField(data.identity.licenseNumber.toString(), key));
        id.insert(QStringLiteral("username"), encryptedField(data.identity.username, key));
        request.insert(QStringLiteral("identity"), id);
        return request;
    }

    if (!data.sshPublicKey.isEmpty() && (encrypted.isEmpty() || request.value(QStringLiteral("type")).toInt() == 5)) {
        request.remove(QStringLiteral("login"));
        request.insert(QStringLiteral("type"), 5);
        const QByteArray blob = QByteArray::fromBase64(data.sshPublicKey.section(' ', 1, 1).toLatin1());
        const QByteArray digest = QCryptographicHash::hash(blob, QCryptographicHash::Sha256);
        const QString fingerprint = QStringLiteral("SHA256:")
            + QString::fromLatin1(digest.toBase64(QByteArray::OmitTrailingEquals));
        QJsonObject ssh = request.value(QStringLiteral("sshKey")).toObject();
        ssh.insert(QStringLiteral("publicKey"), encryptedField(data.sshPublicKey, key));
        ssh.insert(QStringLiteral("keyFingerprint"), encryptedField(fingerprint, key));
        request.insert(QStringLiteral("sshKey"), ssh);
    }
    if (request.value(QStringLiteral("type")).toInt() == 5) {
        // An SSH key has no login: the "password" is the private key.
        QJsonObject ssh = request.value(QStringLiteral("sshKey")).toObject();
        ssh.insert(QStringLiteral("privateKey"), encryptedField(data.password.toString(), key));
        request.insert(QStringLiteral("sshKey"), ssh);
        return request;
    }

    QJsonObject login = request.value(QStringLiteral("login")).toObject();
    // Legacy single-URI field returned by /sync; `uris` is the source of truth.
    login.remove(QStringLiteral("uri"));
    login.insert(QStringLiteral("username"), encryptedField(data.username, key));
    // Written as given: Bitwarden reads an otpauth URI, a bare secret and
    // steam:// alike, so one that came from another client goes back as it was.
    login.insert(QStringLiteral("totp"), encryptedField(data.otp.toString(), key));

    QString password = data.password.toString();
    if (!encrypted.isEmpty() && password != previousPassword) {
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (!previousPassword.isEmpty()) {
            QJsonArray history = request.value(QStringLiteral("passwordHistory")).toArray();
            history.prepend(QJsonObject{{QStringLiteral("lastUsedDate"), now},
                                        {QStringLiteral("password"), encryptedField(previousPassword, key)}});
            while (history.size() > passwordHistoryLength)
                history.removeLast();
            request.insert(QStringLiteral("passwordHistory"), history);
        }
        login.insert(QStringLiteral("passwordRevisionDate"), now);
    }
    login.insert(QStringLiteral("password"), encryptedField(password, key));
    password.fill(QChar(0));

    // The form's URL first, then the additional ones, by position: each
    // keeps the match setting the URI in its place had. The checksum lets
    // clients detect a URI swapped between items, so it is rewritten along
    // with each.
    QStringList wanted;
    if (!data.url.isEmpty())
        wanted.append(data.url);
    wanted.append(data.extraUrls);
    const QJsonArray before = login.value(QStringLiteral("uris")).toArray();
    QJsonArray uris;
    for (int i = 0; i < wanted.size(); ++i) {
        QJsonObject uri = i < before.size() ? before.at(i).toObject()
                                            : QJsonObject{{QStringLiteral("match"), QJsonValue::Null}};
        uri.insert(QStringLiteral("uri"), encryptedField(wanted.at(i), key));
        const QByteArray checksum =
            QCryptographicHash::hash(wanted.at(i).toUtf8(), QCryptographicHash::Sha256).toBase64();
        uri.insert(QStringLiteral("uriChecksum"), encryptedField(QString::fromLatin1(checksum), key));
        uris.append(uri);
    }
    login.insert(QStringLiteral("uris"), uris);
    request.insert(QStringLiteral("login"), login);
    return request;
}

QJsonObject BwAccount::folderRequest(const QString &name, const BwKey &user) {
    return {{QStringLiteral("name"), encryptedField(name, user)}};
}

void BwAccount::decryptVault(const BwAccountState &state, const BwKeys &keys,
                             QHash<QString, QJsonObject> *items, QHash<QString, QString> *folders,
                             int *undecryptable) {
    items->clear();
    folders->clear();
    int failed = 0;

    for (const QJsonValue &value : state.folders) {
        const QJsonObject folder = value.toObject();
        const QString id = folder.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        const QString name = decryptFolder(folder, keys.user);
        if (name.isEmpty())
            ++failed;
        else
            folders->insert(id, name);
    }

    for (const QJsonValue &value : state.ciphers) {
        const QJsonObject cipher = value.toObject();
        if (const std::optional<QJsonObject> item = decryptCipher(cipher, keys)) {
            items->insert(item->value(QStringLiteral("id")).toString(), *item);
            continue;
        }
        // Other kinds and the trash are left out on purpose; only supported
        // cipher types (login, note, card, identity, SSH) count as lost.
        const int type = cipher.value(QStringLiteral("type")).toInt();
        const QJsonValue deleted = cipher.value(QStringLiteral("deletedDate"));
        if (type >= 1 && type <= 5 && (deleted.isNull() || deleted.isUndefined()))
            ++failed;
    }

    if (undecryptable)
        *undecryptable = failed;
}
