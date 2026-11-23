#include "browserserver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

namespace {

const QString serverName = QStringLiteral("org.keepassxc.KeePassXC.BrowserServer");

}

BrowserServer::BrowserServer(BrowserHost *host, QObject *parent) : QObject(parent), m_host(host) {}

BrowserServer::~BrowserServer() {
    stop();
}

QString BrowserServer::socketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/app/org.keepassxc.KeePassXC/") + serverName;
}

QString BrowserServer::legacySocketPath() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/')
        + serverName;
}

bool BrowserServer::start(QString *error, const QString &path) {
    if (m_server)
        return true;
    const QString target = path.isEmpty() ? socketPath() : path;
    QDir().mkpath(QFileInfo(target).absolutePath());

    // Someone answering there is another program's server, not a leftover
    // from a crash: taking the path from under it would break it.
    {
        QLocalSocket probe;
        probe.connectToServer(target);
        if (probe.waitForConnected(200)) {
            probe.disconnectFromServer();
            *error = QStringLiteral("busy");
            return false;
        }
    }
    QLocalServer::removeServer(target);

    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(target)) {
        *error = server->errorString();
        delete server;
        return false;
    }
    m_server = server;
    m_path = target;
    connect(server, &QLocalServer::newConnection, this, &BrowserServer::accept);

    if (path.isEmpty()) {
        const QString legacy = legacySocketPath();
        const QFileInfo existing(legacy);
        // Only a link (or a dead socket) is replaced: never a file of
        // someone else's.
        if (existing.isSymLink() || !existing.exists() || !existing.isFile()) {
            QFile::remove(legacy);
            QFile::link(target, legacy);
        }
    }
    return true;
}

void BrowserServer::stop() {
    if (!m_server)
        return;
    const QList<QLocalSocket *> sockets = m_connections.keys();
    m_connections.clear();
    for (QLocalSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_server->close();
    delete m_server;
    m_server = nullptr;

    if (QFileInfo(legacySocketPath()).symLinkTarget() == m_path)
        QFile::remove(legacySocketPath());
    m_path.clear();
}

bool BrowserServer::running() const {
    return m_server && m_server->isListening();
}

void BrowserServer::broadcast(const QJsonObject &message) {
    const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    for (auto it = m_connections.cbegin(); it != m_connections.cend(); ++it) {
        it.key()->write(bytes);
        it.key()->flush();
    }
}

QList<QByteArray> BrowserServer::takeObjects(QByteArray *buffer) {
    QList<QByteArray> objects;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    qsizetype start = -1;
    qsizetype consumed = 0;

    for (qsizetype i = 0; i < buffer->size(); ++i) {
        const char c = buffer->at(i);
        if (inString) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        if (c == '"' && depth > 0) {
            inString = true;
        } else if (c == '{') {
            if (depth++ == 0)
                start = i;
        } else if (c == '}' && depth > 0) {
            if (--depth == 0) {
                objects.append(buffer->mid(start, i - start + 1));
                consumed = i + 1;
            }
        } else if (depth == 0) {
            // Anything between objects (whitespace, junk) goes.
            consumed = i + 1;
        }
    }
    buffer->remove(0, consumed);
    return objects;
}

void BrowserServer::accept() {
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_connections.insert(socket, Connection());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() { forget(socket); });
    }
}

void BrowserServer::read(QLocalSocket *socket) {
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    it->buffer.append(socket->readAll());
    const QList<QByteArray> messages = takeObjects(&it->buffer);
    if (it->buffer.size() > maxMessageBytes) {
        forget(socket);
        return;
    }

    const QPointer<QLocalSocket> guard(socket);
    for (const QByteArray &bytes : messages) {
        const QJsonObject request = QJsonDocument::fromJson(bytes).object();
        const QString client = request.value(QStringLiteral("clientID")).toString();
        auto connection = m_connections.find(socket);
        if (connection == m_connections.end())
            return;
        std::shared_ptr<BrowserSession> &session = connection->sessions[client];
        if (!session)
            session = std::make_shared<BrowserSession>(m_host);
        // Held by the call, so a reply can still go out while the session is
        // being replaced by a new key exchange.
        const std::shared_ptr<BrowserSession> keep = session;
        keep->handle(request, [guard](const QJsonObject &reply) {
            if (!guard || guard->state() != QLocalSocket::ConnectedState)
                return;
            guard->write(QJsonDocument(reply).toJson(QJsonDocument::Compact));
            guard->flush();
        });
    }
}

void BrowserServer::forget(QLocalSocket *socket) {
    if (!m_connections.remove(socket))
        return;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}
