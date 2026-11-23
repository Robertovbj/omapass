#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

#include "browserprotocol.h"

class QLocalServer;
class QLocalSocket;

// The local socket the browser's proxy talks to, at the path KeePassXC
// listens on, so the extension's native messaging host reaches omapass
// instead. Each connection is one browser; each client of it (`clientID`)
// gets a BrowserSession of its own.
class BrowserServer : public QObject {
    Q_OBJECT

public:
    explicit BrowserServer(BrowserHost *host, QObject *parent = nullptr);
    ~BrowserServer() override;

    // $XDG_RUNTIME_DIR/app/org.keepassxc.KeePassXC/org.keepassxc.KeePassXC.BrowserServer
    static QString socketPath();
    // Where KeePassXC used to listen, linked to the path above for proxies
    // that still look there.
    static QString legacySocketPath();

    // Listens on `path` (socketPath() when empty). Refuses, saying so in
    // `error`, when another program — KeePassXC — is already answering there.
    bool start(QString *error, const QString &path = QString());
    void stop();
    bool running() const;

    // A message to every connected browser, unencrypted, as KeePassXC sends
    // `database-locked` and `database-unlocked`.
    void broadcast(const QJsonObject &message);

    // The complete JSON objects at the front of `buffer`, taken out of it;
    // whatever is left is an object still arriving. A socket hands over
    // bytes, not messages: two can arrive in one read, or one in two.
    static QList<QByteArray> takeObjects(QByteArray *buffer);

    // Larger than anything the extension sends; a connection that goes past
    // it without finishing an object is dropped.
    static constexpr qsizetype maxMessageBytes = 1024 * 1024;

private:
    void accept();
    void read(QLocalSocket *socket);
    void forget(QLocalSocket *socket);

    struct Connection {
        QByteArray buffer;
        QHash<QString, std::shared_ptr<BrowserSession>> sessions;
    };

    BrowserHost *m_host;
    QLocalServer *m_server = nullptr;
    QString m_path;
    QHash<QLocalSocket *, Connection> m_connections;
};
