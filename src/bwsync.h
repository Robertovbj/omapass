#pragma once

#include <QByteArray>
#include <QObject>
#include <QSslSocket>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include <optional>

#include "secret.h"

// Real-time Bitwarden / Vaultwarden sync over SignalR MessagePack on top of a
// minimal RFC 6455 WebSocket client using QSslSocket (no QtWebSockets
// dependency). Runs asynchronously on the GUI thread's event loop.
struct BwSyncEvent {
    enum class Kind {
        CipherChanged,
        CipherDeleted,
        FullSync,
    };

    Kind kind = Kind::FullSync;
    QString cipherId;
};

class BwSync : public QObject {
    Q_OBJECT

public:
    explicit BwSync(QObject *parent = nullptr);
    ~BwSync() override;

    void connectTo(const QString &server, const Secret &accessToken);
    void disconnectFromServer();
    bool isConnected() const { return m_handshakeDone; }

    // Exposed for unit tests: decodes length-prefixed SignalR MessagePack
    // frames into QVariant messages, and maps ReceiveMessage payloads to
    // BwSyncEvent.
    static QVector<QVariant> decodeSignalRFrames(const QByteArray &payload);
    static std::optional<BwSyncEvent> parseNotification(const QVariant &message);

signals:
    void eventReceived(const BwSyncEvent &event);
    void disconnected(bool hadError);

private:
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError();
    void sendHttpUpgrade();
    void processHttpResponse();
    void processWebSocketFrames();
    void handleCompleteMessage(quint8 opcode, const QByteArray &payload);
    void sendWebSocketFrame(quint8 opcode, const QByteArray &payload);
    void sendSignalRPing();

    QSslSocket *m_socket = nullptr;
    QTimer m_pingTimer;
    QString m_server;
    QUrl m_url;
    QByteArray m_wsKey;
    QByteArray m_readBuffer;
    bool m_httpUpgraded = false;
    bool m_handshakeDone = false;
    bool m_stopping = false;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragmentPayload;
};
