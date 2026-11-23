// The browser integration from the extension's side: a test client that
// speaks the KeePassXC-Browser protocol as keepassxc-browser does (its own
// key pair, a random nonce per request, replies checked against the
// incremented one), against a host whose answers the test decides.

#include "support.h"

#include <QLocalSocket>

#include "browsercrypto.h"
#include "browsermanifests.h"
#include "browserprotocol.h"
#include "browserproxy.h"
#include "browserserver.h"

namespace {

QString b64(const QByteArray &bytes) {
    return QString::fromLatin1(bytes.toBase64());
}

QByteArray unb64(const QJsonValue &value) {
    return QByteArray::fromBase64(value.toString().toLatin1());
}

class FakeHost : public BrowserHost {
public:
    bool isUnlocked = true;
    int unlockRequests = 0;
    int locks = 0;
    QString hash = QStringLiteral("0123abcd");
    QHash<QString, QString> keys;
    // What the user answers when asked to name an association; empty refuses.
    QString associationName = QStringLiteral("laptop");
    QString lastIdKey;
    QVector<BrowserLogin> logins;
    BrowserLoginQuery lastQuery;
    BrowserSaveRequest lastSave;
    bool saveSucceeds = true;
    // Answers held back, for the tests where the user takes a while.
    bool deferAnswers = false;
    QList<std::function<void()>> pending;

    bool unlocked() const override { return isUnlocked; }
    void showUnlock() override { ++unlockRequests; }
    QString databaseHash() const override { return hash; }
    QString associationKey(const QString &id) const override { return keys.value(id); }

    void answer(std::function<void()> callback) {
        if (deferAnswers)
            pending.append(std::move(callback));
        else
            callback();
    }

    void associate(const QString &idKey, std::function<void(const QString &)> done) override {
        lastIdKey = idKey;
        answer([this, idKey, done]() {
            if (!associationName.isEmpty())
                keys.insert(associationName, idKey);
            done(associationName);
        });
    }
    void findLogins(const BrowserLoginQuery &query,
                    std::function<void(const QVector<BrowserLogin> &)> done) override {
        lastQuery = query;
        answer([this, done]() { done(logins); });
    }
    void totp(const QString &uuid, std::function<void(const QString &)> done) override {
        answer([uuid, done]() { done(uuid.startsWith(QLatin1Char('a')) ? QStringLiteral("123456") : QString()); });
    }
    void saveLogin(const BrowserSaveRequest &request, std::function<void(bool)> done) override {
        lastSave = request;
        answer([this, done]() { done(saveSucceeds); });
    }
    QJsonArray groups() const override {
        return QJsonArray{QJsonObject{{"name", "Root"}, {"uuid", QString(32, '0')},
                                      {"children", QJsonArray{QJsonObject{{"name", "Mail"},
                                                                          {"uuid", QString(32, '1')},
                                                                          {"children", QJsonArray()}}}}}};
    }
    void createGroup(const QString &path, std::function<void(const QString &, const QString &)> done) override {
        done(path.section(QLatin1Char('/'), -1), QString(32, 'f'));
    }
    Secret generatePassword() override { return Secret(QStringLiteral("gerada-123")); }
    void lock() override {
        ++locks;
        isUnlocked = false;
    }
};

// keepassxc-browser, as far as these tests need it.
struct Extension {
    BrowserCrypto::KeyPair keys = BrowserCrypto::generateKeyPair();
    QByteArray idKey = BrowserCrypto::generateKeyPair().publicKey;
    QByteArray hostKey;
    QString clientId = b64(BrowserCrypto::randomBytes(24));
    QByteArray lastNonce;

    static QJsonObject send(BrowserSession &session, const QJsonObject &request) {
        QJsonObject reply;
        bool replied = false;
        session.handle(request, [&](const QJsonObject &answer) {
            reply = answer;
            replied = true;
        });
        if (!replied)
            return QJsonObject{{"pending", true}};
        return reply;
    }

    QJsonObject exchange(BrowserSession &session) {
        lastNonce = BrowserCrypto::randomBytes(24);
        const QJsonObject reply = send(session, {{"action", "change-public-keys"},
                                                 {"publicKey", b64(keys.publicKey)},
                                                 {"nonce", b64(lastNonce)},
                                                 {"clientID", clientId}});
        hostKey = unb64(reply.value("publicKey"));
        return reply;
    }

    QJsonObject request(const QString &action, QJsonObject message = QJsonObject(),
                        QJsonObject extra = QJsonObject()) {
        message.insert("action", action);
        lastNonce = BrowserCrypto::randomBytes(24);
        const QByteArray sealed = BrowserCrypto::seal(QJsonDocument(message).toJson(QJsonDocument::Compact),
                                                      lastNonce, hostKey, keys.secretKey);
        QJsonObject request{{"action", action},
                            {"message", b64(sealed)},
                            {"nonce", b64(lastNonce)},
                            {"clientID", clientId}};
        for (auto it = extra.cbegin(); it != extra.cend(); ++it)
            request.insert(it.key(), it.value());
        return request;
    }

    // The decrypted message of a reply to the last request, or empty when it
    // does not open or does not carry the incremented nonce.
    QJsonObject open(const QJsonObject &reply) const {
        const QByteArray expected = BrowserCrypto::incremented(lastNonce);
        if (unb64(reply.value("nonce")) != expected)
            return QJsonObject();
        QByteArray plain;
        if (!BrowserCrypto::open(unb64(reply.value("message")), expected, hostKey, keys.secretKey, &plain))
            return QJsonObject();
        const QJsonObject message = QJsonDocument::fromJson(plain).object();
        if (unb64(message.value("nonce")) != expected)
            return QJsonObject();
        return message;
    }

    QJsonObject call(BrowserSession &session, const QString &action, const QJsonObject &message = QJsonObject(),
                     const QJsonObject &extra = QJsonObject()) {
        const QJsonObject reply = send(session, request(action, message, extra));
        if (reply.contains("errorCode"))
            return reply;
        return open(reply);
    }

    bool associate(BrowserSession &session) {
        const QJsonObject reply =
            call(session, "associate", {{"key", b64(keys.publicKey)}, {"idKey", b64(idKey)}});
        return reply.value("success").toString() == "true";
    }
};

int errorOf(const QJsonObject &reply) {
    return reply.value("errorCode").toString().toInt();
}

BrowserLogin login(const QString &uuid, const QString &name, const QString &user, const QString &password,
                   const QString &totp = QString()) {
    BrowserLogin out;
    out.uuid = uuid;
    out.name = name;
    out.login = user;
    out.password = Secret(password);
    out.group = QStringLiteral("Web");
    out.totp = totp;
    return out;
}

}

class TestBrowser : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ matching

    void browserMatchesUrls_data() {
        QTest::addColumn<QString>("entry");
        QTest::addColumn<QString>("site");
        QTest::addColumn<bool>("matches");

        QTest::newRow("same host") << "https://github.com/login" << "https://github.com/" << true;
        QTest::newRow("bare host") << "github.com" << "https://github.com/session" << true;
        QTest::newRow("case") << "GitHub.com" << "https://github.COM/" << true;
        QTest::newRow("subdomain of the entry") << "example.com" << "https://login.example.com/" << true;
        QTest::newRow("parent of the entry") << "login.example.com" << "https://example.com/" << false;
        QTest::newRow("look-alike") << "example.com" << "https://badexample.com/" << false;
        QTest::newRow("other host") << "example.com" << "https://example.org/" << false;
        QTest::newRow("port named") << "http://localhost:8080" << "http://localhost:8080/x" << true;
        QTest::newRow("other port") << "http://localhost:8080" << "http://localhost:9090/" << false;
        QTest::newRow("no port named") << "localhost" << "http://localhost:3000/" << true;
        QTest::newRow("top-level name") << "com" << "https://example.com/" << false;
        QTest::newRow("address") << "192.168.0.1" << "https://192.168.0.1/" << true;
        QTest::newRow("address suffix") << "0.0.1" << "https://192.168.0.1/" << false;
        QTest::newRow("empty entry") << "" << "https://example.com/" << false;
        QTest::newRow("not a url") << "minha senha" << "https://example.com/" << false;
    }

    void browserMatchesUrls() {
        QFETCH(QString, entry);
        QFETCH(QString, site);
        QFETCH(bool, matches);
        QCOMPARE(BrowserProtocol::urlMatches(entry, site), matches);
    }

    // ------------------------------------------------------------ protocol

    void browserExchangesKeys() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        const QJsonObject reply = extension.exchange(session);

        QCOMPARE(reply.value("action").toString(), QStringLiteral("change-public-keys"));
        QCOMPARE(reply.value("success").toString(), QStringLiteral("true"));
        QCOMPARE(extension.hostKey, session.hostPublicKey());
        QCOMPARE(extension.hostKey.size(), BrowserCrypto::keyBytes);
        QCOMPARE(unb64(reply.value("nonce")), BrowserCrypto::incremented(extension.lastNonce));
    }

    void browserRefusesABadKeyExchange() {
        FakeHost host;
        BrowserSession session(&host);
        const QJsonObject reply = Extension::send(session, {{"action", "change-public-keys"},
                                                            {"publicKey", b64("short")},
                                                            {"nonce", b64(QByteArray(24, 'n'))}});
        QCOMPARE(errorOf(reply), int(BrowserProtocol::ClientPublicKeyNotReceived));
    }

    void browserWantsKeysFirst() {
        FakeHost host;
        BrowserSession session(&host);
        const QJsonObject reply = Extension::send(session, {{"action", "get-databasehash"},
                                                            {"message", "x"},
                                                            {"nonce", "y"}});
        QCOMPARE(errorOf(reply), int(BrowserProtocol::ClientPublicKeyNotReceived));
    }

    void browserGivesTheDatabaseHash() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        const QJsonObject message = extension.call(session, "get-databasehash");
        QCOMPARE(message.value("hash").toString(), QStringLiteral("0123abcd"));
        QCOMPARE(message.value("version").toString(), QString::fromLatin1(BrowserProtocol::hostVersion));
        QCOMPARE(message.value("success").toString(), QStringLiteral("true"));
    }

    void browserSaysWhenLocked() {
        FakeHost host;
        host.isUnlocked = false;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        QCOMPARE(errorOf(extension.call(session, "get-databasehash")), int(BrowserProtocol::DatabaseNotOpened));
        QCOMPARE(host.unlockRequests, 0);

        // The extension asks for the window when the user clicked to unlock.
        QCOMPARE(errorOf(extension.call(session, "get-databasehash", {}, {{"triggerUnlock", "true"}})),
                 int(BrowserProtocol::DatabaseNotOpened));
        QCOMPARE(host.unlockRequests, 1);
    }

    void browserRefusesWhatDoesNotDecrypt() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        QJsonObject request = extension.request("get-databasehash");
        QByteArray sealed = unb64(request.value("message"));
        sealed[0] = char(sealed.at(0) ^ 1);
        request.insert("message", b64(sealed));
        QCOMPARE(errorOf(Extension::send(session, request)), int(BrowserProtocol::CannotDecryptMessage));

        // Nor one sealed for a session that has since changed its keys.
        const QJsonObject stale = extension.request("get-databasehash");
        Extension other;
        other.exchange(session);
        QCOMPARE(errorOf(Extension::send(session, stale)), int(BrowserProtocol::CannotDecryptMessage));
    }

    void browserAssociates() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        const QJsonObject message =
            extension.call(session, "associate", {{"key", b64(extension.keys.publicKey)}, {"idKey", b64(extension.idKey)}});
        QCOMPARE(message.value("id").toString(), QStringLiteral("laptop"));
        QCOMPARE(message.value("hash").toString(), QStringLiteral("0123abcd"));
        // What is kept is the identification key, not the session's.
        QCOMPARE(host.lastIdKey, b64(extension.idKey));
        QCOMPARE(host.keys.value("laptop"), b64(extension.idKey));
    }

    void browserAssociatesOnlyItsOwnKey() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        const QJsonObject reply = extension.call(session, "associate", {{"key", b64(extension.idKey)}});
        QCOMPARE(errorOf(reply), int(BrowserProtocol::AssociationFailed));
        QVERIFY(host.lastIdKey.isEmpty());
    }

    void browserReportsARefusedAssociation() {
        FakeHost host;
        host.associationName.clear();
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        QVERIFY(!extension.associate(session));
        QCOMPARE(errorOf(extension.call(session, "get-logins", {{"url", "https://example.com"}})),
                 int(BrowserProtocol::AssociationFailed));
    }

    void browserTestsAnAssociation() {
        FakeHost host;
        host.keys.insert("laptop", b64(QByteArray(32, 'k')));
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        QCOMPARE(errorOf(extension.call(session, "test-associate",
                                        {{"id", "laptop"}, {"key", b64(QByteArray(32, 'x'))}})),
                 int(BrowserProtocol::AssociationFailed));
        QCOMPARE(errorOf(extension.call(session, "test-associate", {{"id", "desktop"}, {"key", b64(QByteArray(32, 'k'))}})),
                 int(BrowserProtocol::AssociationFailed));

        const QJsonObject message =
            extension.call(session, "test-associate", {{"id", "laptop"}, {"key", b64(QByteArray(32, 'k'))}});
        QCOMPARE(message.value("id").toString(), QStringLiteral("laptop"));
        QCOMPARE(message.value("success").toString(), QStringLiteral("true"));
    }

    void browserGivesLogins() {
        FakeHost host;
        host.logins = {login(QString(32, 'a'), "GitHub", "ana", "s3nha", "654321"),
                       login(QString(32, 'b'), "GitHub (trabalho)", "ana@empresa", "outra")};
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        // Not before the browser is associated.
        QCOMPARE(errorOf(extension.call(session, "get-logins", {{"url", "https://github.com/"}})),
                 int(BrowserProtocol::AssociationFailed));

        QVERIFY(extension.associate(session));
        const QJsonObject message = extension.call(
            session, "get-logins",
            {{"url", "https://github.com/login"}, {"submitUrl", "https://github.com/session"}, {"id", "laptop"}});
        QCOMPARE(host.lastQuery.url, QStringLiteral("https://github.com/login"));
        QCOMPARE(host.lastQuery.submitUrl, QStringLiteral("https://github.com/session"));
        QCOMPARE(message.value("count").toInt(), 2);
        QCOMPARE(message.value("id").toString(), QStringLiteral("laptop"));

        const QJsonArray entries = message.value("entries").toArray();
        QCOMPARE(entries.size(), 2);
        const QJsonObject first = entries.at(0).toObject();
        QCOMPARE(first.value("login").toString(), QStringLiteral("ana"));
        QCOMPARE(first.value("password").toString(), QStringLiteral("s3nha"));
        QCOMPARE(first.value("name").toString(), QStringLiteral("GitHub"));
        QCOMPARE(first.value("uuid").toString(), QString(32, 'a'));
        QCOMPARE(first.value("group").toString(), QStringLiteral("Web"));
        QCOMPARE(first.value("totp").toString(), QStringLiteral("654321"));
        QVERIFY(!entries.at(1).toObject().contains("totp"));
    }

    void browserSaysWhenThereAreNoLogins() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        QVERIFY(extension.associate(session));

        QCOMPARE(errorOf(extension.call(session, "get-logins", {{"url", "https://nada.com"}})),
                 int(BrowserProtocol::NoLoginsFound));
        QCOMPARE(errorOf(extension.call(session, "get-logins")), int(BrowserProtocol::NoUrlProvided));
    }

    void browserGivesTheCurrentCode() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        QVERIFY(extension.associate(session));

        QCOMPARE(extension.call(session, "get-totp", {{"uuid", QString(32, 'a')}}).value("totp").toString(),
                 QStringLiteral("123456"));
        QCOMPARE(errorOf(extension.call(session, "get-totp", {{"uuid", "../etc"}})),
                 int(BrowserProtocol::NoValidUuidProvided));
    }

    void browserSavesLogins() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        QVERIFY(extension.associate(session));

        QJsonObject message = extension.call(session, "set-login",
                                             {{"url", "https://example.com/login"},
                                              {"submitUrl", "https://example.com/do"},
                                              {"login", "ana"},
                                              {"password", "nova"},
                                              {"group", "Web/Novos"},
                                              {"groupUuid", QString(32, '1')},
                                              {"id", "laptop"}});
        QCOMPARE(message.value("error").toString(), QStringLiteral("success"));
        QCOMPARE(host.lastSave.url, QStringLiteral("https://example.com/login"));
        QCOMPARE(host.lastSave.login, QStringLiteral("ana"));
        QCOMPARE(host.lastSave.password.toString(), QStringLiteral("nova"));
        QCOMPARE(host.lastSave.group, QStringLiteral("Web/Novos"));
        QCOMPARE(host.lastSave.groupUuid, QString(32, '1'));
        QVERIFY(host.lastSave.uuid.isEmpty());

        host.saveSucceeds = false;
        message = extension.call(session, "set-login",
                                 {{"url", "https://example.com"}, {"login", "ana"}, {"password", "x"},
                                  {"uuid", QString(32, 'b')}});
        QCOMPARE(message.value("error").toString(), QStringLiteral("error"));
        QCOMPARE(host.lastSave.uuid, QString(32, 'b'));

        QCOMPARE(errorOf(extension.call(session, "set-login", {{"url", "https://example.com"}, {"uuid", "x"}})),
                 int(BrowserProtocol::NoValidUuidProvided));
    }

    void browserListsAndCreatesGroups() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        QVERIFY(extension.associate(session));

        const QJsonArray groups =
            extension.call(session, "get-database-groups").value("groups").toObject().value("groups").toArray();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.at(0).toObject().value("name").toString(), QStringLiteral("Root"));
        QCOMPARE(groups.at(0).toObject().value("children").toArray().at(0).toObject().value("name").toString(),
                 QStringLiteral("Mail"));

        const QJsonObject created = extension.call(session, "create-new-group", {{"groupName", "Web/Novos"}});
        QCOMPARE(created.value("name").toString(), QStringLiteral("Novos"));
        QCOMPARE(created.value("uuid").toString(), QString(32, 'f'));
    }

    void browserGeneratesAndLocks() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);

        const QJsonObject generated = extension.call(session, "generate-password", {}, {{"requestID", "abcd1234"}});
        QCOMPARE(generated.value("password").toString(), QStringLiteral("gerada-123"));

        QCOMPARE(extension.call(session, "lock-database").value("success").toString(), QStringLiteral("true"));
        QCOMPARE(host.locks, 1);
        QVERIFY(!host.isUnlocked);
    }

    void browserEchoesTheRequestId() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        const QJsonObject reply =
            Extension::send(session, extension.request("generate-password", {}, {{"requestID", "abcd1234"}}));
        QCOMPARE(reply.value("requestID").toString(), QStringLiteral("abcd1234"));
    }

    void browserRefusesWhatItDoesNotServe() {
        FakeHost host;
        BrowserSession session(&host);
        Extension extension;
        extension.exchange(session);
        QVERIFY(extension.associate(session));

        for (const char *action : {"passkeys-get", "passkeys-register", "request-autotype", "delete-entry",
                                   "get-database-entries"})
            QCOMPARE(errorOf(extension.call(session, QLatin1String(action))), int(BrowserProtocol::IncorrectAction));
        QCOMPARE(errorOf(Extension::send(session, QJsonObject())), int(BrowserProtocol::EmptyMessageReceived));
    }

    // The user takes a while to answer; the reply goes out when they do,
    // and not at all once the browser has gone.
    void browserAnswersLater() {
        FakeHost host;
        host.deferAnswers = true;
        Extension extension;

        QJsonObject reply;
        int replies = 0;
        {
            BrowserSession session(&host);
            extension.exchange(session);
            session.handle(extension.request("associate", {{"key", b64(extension.keys.publicKey)}}),
                           [&](const QJsonObject &answer) {
                               reply = answer;
                               ++replies;
                           });
            QCOMPARE(replies, 0);
            QCOMPARE(host.pending.size(), 1);
            host.pending.takeFirst()();
            QCOMPARE(replies, 1);
            QCOMPARE(extension.open(reply).value("id").toString(), QStringLiteral("laptop"));

            session.handle(extension.request("get-logins", {{"url", "https://example.com"}}),
                           [&](const QJsonObject &) { ++replies; });
            QCOMPARE(host.pending.size(), 1);
        }
        host.pending.takeFirst()();
        QCOMPARE(replies, 1);
    }

    // ------------------------------------------------------------ socket

    void browserSplitsTheStream() {
        QByteArray buffer("{\"a\":\"}{\\\"\"}  {\"b\":{\"c\":1}}{\"d\":");
        const QList<QByteArray> objects = BrowserServer::takeObjects(&buffer);
        QCOMPARE(objects.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(objects.at(0)).object().value("a").toString(), QStringLiteral("}{\""));
        QCOMPARE(QJsonDocument::fromJson(objects.at(1)).object().value("b").toObject().value("c").toInt(), 1);
        QCOMPARE(buffer, QByteArray("{\"d\":"));

        buffer.append("2}");
        QCOMPARE(BrowserServer::takeObjects(&buffer).size(), 1);
        QVERIFY(buffer.isEmpty());
    }

    void browserServesTheSocket() {
        QTemporaryDir dir;
        const QString path = dir.filePath("browser.sock");
        FakeHost host;
        BrowserServer server(&host);
        QString error;
        QVERIFY2(server.start(&error, path), qPrintable(error));
        QVERIFY(server.running());

        // A second omapass (or KeePassXC) finds the place taken.
        BrowserServer other(&host);
        QVERIFY(!other.start(&error, path));
        QCOMPARE(error, QStringLiteral("busy"));

        QLocalSocket socket;
        socket.connectToServer(path);
        QVERIFY(socket.waitForConnected(2000));

        Extension extension;
        const QByteArray nonce = BrowserCrypto::randomBytes(24);
        const QJsonObject exchange{{"action", "change-public-keys"},
                                   {"publicKey", b64(extension.keys.publicKey)},
                                   {"nonce", b64(nonce)},
                                   {"clientID", extension.clientId}};
        // Two requests in one write: both answered.
        socket.write(QJsonDocument(exchange).toJson(QJsonDocument::Compact)
                     + QJsonDocument(QJsonObject{{"action", "nope"}, {"clientID", "x"}}).toJson());
        socket.flush();

        QByteArray received;
        QList<QByteArray> replies;
        QTRY_VERIFY_WITH_TIMEOUT((received.append(socket.readAll()),
                                  replies.append(BrowserServer::takeObjects(&received)), replies.size() >= 2),
                                 3000);
        const QJsonObject keys = QJsonDocument::fromJson(replies.at(0)).object();
        QCOMPARE(keys.value("action").toString(), QStringLiteral("change-public-keys"));
        QCOMPARE(unb64(keys.value("publicKey")).size(), BrowserCrypto::keyBytes);
        QCOMPARE(QJsonDocument::fromJson(replies.at(1)).object().value("errorCode").toString(),
                 QStringLiteral("3"));

        server.broadcast(QJsonObject{{"action", "database-locked"}});
        QTRY_VERIFY_WITH_TIMEOUT((received.append(socket.readAll()),
                                  replies.append(BrowserServer::takeObjects(&received)), replies.size() >= 3),
                                 3000);
        QCOMPARE(QJsonDocument::fromJson(replies.at(2)).object().value("action").toString(),
                 QStringLiteral("database-locked"));

        server.stop();
        QVERIFY(!server.running());
        QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::UnconnectedState, 3000);
        // And the place is free again.
        QVERIFY2(other.start(&error, path), qPrintable(error));
    }

    // ------------------------------------------------------------ proxy

    void browserRecognizesTheBrowsersCall() {
        QVERIFY(BrowserProxy::isProxyInvocation(
            {"omapass", "chrome-extension://oboonakemofpalcgghocfoadofidjkkk/", "--parent-window=0"}));
        QVERIFY(BrowserProxy::isProxyInvocation(
            {"omapass", "/home/u/.mozilla/native-messaging-hosts/org.keepassxc.keepassxc_browser.json",
             "keepassxc-browser@keepassxc.org"}));
        QVERIFY(!BrowserProxy::isProxyInvocation({"omapass"}));
        QVERIFY(!BrowserProxy::isProxyInvocation({"omapass", "--doctor"}));
        QVERIFY(!BrowserProxy::isProxyInvocation({"omapass", "/tmp/x.json", "other@example.org"}));
    }

    void browserFramesNativeMessages() {
        const QByteArray first = BrowserProxy::frame("{\"a\":1}");
        QCOMPARE(first.size(), 4 + 7);
        QByteArray stream = first + BrowserProxy::frame("{}") + BrowserProxy::frame("{\"partial\"").left(6);

        bool broken = true;
        const QList<QByteArray> frames = BrowserProxy::takeFrames(&stream, &broken);
        QVERIFY(!broken);
        QCOMPARE(frames, (QList<QByteArray>{"{\"a\":1}", "{}"}));
        QCOMPARE(stream.size(), 6);

        QByteArray garbage("\xff\xff\xff\xff{}", 6);
        QVERIFY(BrowserProxy::takeFrames(&garbage, &broken).isEmpty());
        QVERIFY(broken);
    }

    // ------------------------------------------------------------ manifests

    void browserInstallsManifests() {
        QTemporaryDir home;
        QDir().mkpath(home.filePath(".mozilla"));
        QDir().mkpath(home.filePath(".config/chromium"));
        QDir().mkpath(home.filePath(".config/BraveSoftware/Brave-Browser"));
        const QString proxy = home.filePath("bin/omapass");
        QDir().mkpath(home.filePath("bin"));

        // Brave already has KeePassXC's.
        const QString keepassxc = home.filePath("bin/keepassxc-proxy");
        QVERIFY(QFile(keepassxc).open(QIODevice::WriteOnly));
        const QString brave = home.filePath(
            ".config/BraveSoftware/Brave-Browser/NativeMessagingHosts/org.keepassxc.keepassxc_browser.json");
        QDir().mkpath(QFileInfo(brave).absolutePath());
        {
            QFile file(brave);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{"path", keepassxc}}).toJson());
        }

        BrowserManifests::Result result = BrowserManifests::install(home.path(), proxy, false);
        QCOMPARE(result.installed, (QStringList{"Firefox", "Chromium"}));
        QCOMPARE(result.conflicts, QStringList{"Brave"});
        QVERIFY(result.failed.isEmpty());

        const auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                                  : QJsonObject();
        };
        const QJsonObject firefox =
            read(home.filePath(".mozilla/native-messaging-hosts/org.keepassxc.keepassxc_browser.json"));
        QCOMPARE(firefox.value("path").toString(), proxy);
        QCOMPARE(firefox.value("name").toString(), QStringLiteral("org.keepassxc.keepassxc_browser"));
        QCOMPARE(firefox.value("allowed_extensions").toArray().at(0).toString(),
                 QStringLiteral("keepassxc-browser@keepassxc.org"));
        const QJsonObject chromium =
            read(home.filePath(".config/chromium/NativeMessagingHosts/org.keepassxc.keepassxc_browser.json"));
        QCOMPARE(chromium.value("allowed_origins").toArray().size(), 2);
        QCOMPARE(chromium.value("type").toString(), QStringLiteral("stdio"));
        QCOMPARE(read(brave).value("path").toString(), keepassxc);

        // Replacing is the user's call.
        result = BrowserManifests::install(home.path(), proxy, true);
        QVERIFY(result.conflicts.isEmpty());
        QCOMPARE(read(brave).value("path").toString(), proxy);

        // A manifest of a program that is gone is not a conflict.
        QFile::remove(keepassxc);
        BrowserManifests::uninstall(home.path(), proxy);
        QVERIFY(!QFile::exists(brave));
        QVERIFY(!QFile::exists(home.filePath(".mozilla/native-messaging-hosts/org.keepassxc.keepassxc_browser.json")));
        {
            QFile file(brave);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{"path", keepassxc}}).toJson());
        }
        QVERIFY(BrowserManifests::install(home.path(), proxy, false).conflicts.isEmpty());

        // Uninstalling leaves another program's manifest alone.
        {
            QVERIFY(QFile(keepassxc).open(QIODevice::WriteOnly));
            QFile file(brave);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{"path", keepassxc}}).toJson());
        }
        BrowserManifests::uninstall(home.path(), proxy);
        QVERIFY(QFile::exists(brave));
    }
};

OMAPASS_TEST(TestBrowser)
#include "tst_browser.moc"
