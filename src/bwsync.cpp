#include "bwsync.h"

#include "bwapi.h"
#include "bwcrypto.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslConfiguration>
#include <QUrlQuery>
#include <QtEndian>

#include <cstring>

namespace {

constexpr int signalRPingIntervalMs = 15000;

// Compact MessagePack decoder sufficient for SignalR Hub Protocol payloads.
class MsgPackReader {
public:
    MsgPackReader(const uint8_t *data, qsizetype size) : m_data(data), m_size(size) {}

    bool atEnd() const { return m_pos >= m_size; }
    bool ok() const { return m_ok; }

    QVariant readValue() {
        if (m_pos >= m_size) {
            m_ok = false;
            return {};
        }
        const uint8_t b = m_data[m_pos++];

        // Positive fixint (0x00..0x7f)
        if (b <= 0x7f)
            return qint64(b);
        // Fixmap (0x80..0x8f)
        if ((b & 0xf0) == 0x80)
            return readMap(b & 0x0f);
        // Fixarray (0x90..0x9f)
        if ((b & 0xf0) == 0x90)
            return readArray(b & 0x0f);
        // Fixstr (0xa0..0xbf)
        if ((b & 0xe0) == 0xa0)
            return readString(b & 0x1f);
        // Negative fixint (0xe0..0xff)
        if (b >= 0xe0)
            return qint64(static_cast<int8_t>(b));

        switch (b) {
        case 0xc0: return QVariant();  // nil
        case 0xc2: return false;
        case 0xc3: return true;
        case 0xc4: return readBin(readU8());
        case 0xc5: return readBin(readU16());
        case 0xc6: return readBin(readU32());
        case 0xc7: { const quint32 n = readU8(); skip(1 + qsizetype(n)); return {}; }
        case 0xc8: { const quint32 n = readU16(); skip(1 + qsizetype(n)); return {}; }
        case 0xc9: { const quint32 n = readU32(); skip(1 + qsizetype(n)); return {}; }
        case 0xca: return double(readFloat32());
        case 0xcb: return readFloat64();
        case 0xcc: return quint64(readU8());
        case 0xcd: return quint64(readU16());
        case 0xce: return quint64(readU32());
        case 0xcf: return readU64();
        case 0xd0: return qint64(static_cast<int8_t>(readU8()));
        case 0xd1: return qint64(static_cast<int16_t>(readU16()));
        case 0xd2: return qint64(static_cast<int32_t>(readU32()));
        case 0xd3: return static_cast<qint64>(readU64());
        case 0xd4: skip(2); return {};   // fixext 1
        case 0xd5: skip(3); return {};   // fixext 2
        case 0xd6: skip(5); return {};   // fixext 4
        case 0xd7: skip(9); return {};   // fixext 8
        case 0xd8: skip(17); return {};  // fixext 16
        case 0xd9: return readString(readU8());
        case 0xda: return readString(readU16());
        case 0xdb: return readString(readU32());
        case 0xdc: return readArray(readU16());
        case 0xdd: return readArray(readU32());
        case 0xde: return readMap(readU16());
        case 0xdf: return readMap(readU32());
        default:
            m_ok = false;
            return {};
        }
    }

private:
    bool ensure(qsizetype n) {
        if (n < 0 || m_pos + n > m_size) {
            m_ok = false;
            return false;
        }
        return true;
    }

    void skip(qsizetype n) {
        if (ensure(n))
            m_pos += n;
    }

    quint8 readU8() {
        if (!ensure(1))
            return 0;
        return m_data[m_pos++];
    }

    quint16 readU16() {
        if (!ensure(2))
            return 0;
        const quint16 v = qFromBigEndian<quint16>(m_data + m_pos);
        m_pos += 2;
        return v;
    }

    quint32 readU32() {
        if (!ensure(4))
            return 0;
        const quint32 v = qFromBigEndian<quint32>(m_data + m_pos);
        m_pos += 4;
        return v;
    }

    quint64 readU64() {
        if (!ensure(8))
            return 0;
        const quint64 v = qFromBigEndian<quint64>(m_data + m_pos);
        m_pos += 8;
        return v;
    }

    float readFloat32() {
        const quint32 bits = readU32();
        float v = 0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    double readFloat64() {
        const quint64 bits = readU64();
        double v = 0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    QString readString(quint32 len) {
        if (!ensure(qsizetype(len)))
            return {};
        const QString s = QString::fromUtf8(reinterpret_cast<const char *>(m_data + m_pos), qsizetype(len));
        m_pos += qsizetype(len);
        return s;
    }

    QByteArray readBin(quint32 len) {
        if (!ensure(qsizetype(len)))
            return {};
        const QByteArray b(reinterpret_cast<const char *>(m_data + m_pos), qsizetype(len));
        m_pos += qsizetype(len);
        return b;
    }

    QVariantList readArray(quint32 count) {
        QVariantList list;
        if (count > 10000) {
            m_ok = false;
            return {};
        }
        list.reserve(int(count));
        for (quint32 i = 0; i < count && m_ok; ++i)
            list.append(readValue());
        return list;
    }

    QVariantMap readMap(quint32 count) {
        QVariantMap map;
        if (count > 10000) {
            m_ok = false;
            return {};
        }
        for (quint32 i = 0; i < count && m_ok; ++i) {
            const QString key = readValue().toString();
            const QVariant val = readValue();
            map.insert(key, val);
        }
        return map;
    }

    const uint8_t *m_data;
    qsizetype m_size;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

QVariant mapLookupIgnoreCase(const QVariantMap &map, const QString &lowerKey) {
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key().toLower() == lowerKey)
            return it.value();
    }
    return {};
}

}  // namespace

BwSync::BwSync(QObject *parent) : QObject(parent) {
    m_pingTimer.setInterval(signalRPingIntervalMs);
    connect(&m_pingTimer, &QTimer::timeout, this, &BwSync::sendSignalRPing);
}

BwSync::~BwSync() {
    disconnectFromServer();
}

void BwSync::connectTo(const QString &server, const Secret &accessToken) {
    disconnectFromServer();
    if (accessToken.isEmpty())
        return;

    m_stopping = false;
    m_httpUpgraded = false;
    m_handshakeDone = false;
    m_readBuffer.clear();
    m_fragmentOpcode = 0;
    m_fragmentPayload.clear();
    m_server = server;

    QUrl url(BwApi::notificationsUrl(server));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("access_token"), accessToken.toString());
    url.setQuery(query);
    m_url = url;

    m_socket = new QSslSocket(this);
    if (!BwApi::extraCaCertificates().isEmpty()) {
        QSslConfiguration ssl = m_socket->sslConfiguration();
        ssl.addCaCertificates(BwApi::extraCaCertificates());
        m_socket->setSslConfiguration(ssl);
    }

    const bool useTls = m_url.scheme().compare(QLatin1String("wss"), Qt::CaseInsensitive) == 0;
    if (useTls)
        connect(m_socket, &QSslSocket::encrypted, this, &BwSync::onSocketConnected);
    else
        connect(m_socket, &QAbstractSocket::connected, this, &BwSync::onSocketConnected);

    connect(m_socket, &QIODevice::readyRead, this, &BwSync::onSocketReadyRead);
    connect(m_socket, &QAbstractSocket::disconnected, this, &BwSync::onSocketDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &BwSync::onSocketError);
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) {
        onSocketError();
    });

    const quint16 port = quint16(m_url.port(useTls ? 443 : 80));
    if (useTls)
        m_socket->connectToHostEncrypted(m_url.host(), port);
    else
        m_socket->connectToHost(m_url.host(), port);
}

void BwSync::disconnectFromServer() {
    m_stopping = true;
    m_pingTimer.stop();
    m_httpUpgraded = false;
    m_handshakeDone = false;
    m_readBuffer.clear();
    m_fragmentOpcode = 0;
    m_fragmentPayload.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void BwSync::onSocketConnected() {
    sendHttpUpgrade();
}

void BwSync::sendHttpUpgrade() {
    if (!m_socket)
        return;

    const BwBytes keyBytes = BwCrypto::randomBytes(16);
    m_wsKey = QByteArray(reinterpret_cast<const char *>(keyBytes.data()), qsizetype(keyBytes.size())).toBase64();

    QByteArray path = m_url.path(QUrl::FullyEncoded).toUtf8();
    if (path.isEmpty())
        path = "/";
    if (m_url.hasQuery())
        path += '?' + m_url.query(QUrl::FullyEncoded).toUtf8();

    QByteArray host = m_url.host().toUtf8();
    if (m_url.port() != -1)
        host += ':' + QByteArray::number(m_url.port());

    QByteArray req;
    req += "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + m_wsKey + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "User-Agent: omapass\r\n";
    req += "\r\n";

    m_socket->write(req);
    m_socket->flush();
}

void BwSync::onSocketReadyRead() {
    if (!m_socket)
        return;
    m_readBuffer.append(m_socket->readAll());
    if (!m_httpUpgraded)
        processHttpResponse();
    if (m_httpUpgraded)
        processWebSocketFrames();
}

void BwSync::processHttpResponse() {
    const qsizetype headerEnd = m_readBuffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    const QByteArray headers = m_readBuffer.left(headerEnd);
    m_readBuffer.remove(0, headerEnd + 4);

    const QList<QByteArray> lines = headers.split('\n');
    if (lines.isEmpty() || !lines.first().contains(" 101 ")) {
        onSocketError();
        return;
    }

    const QByteArray expectedAccept =
        QCryptographicHash::hash(m_wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                 QCryptographicHash::Sha1)
            .toBase64();
    bool acceptOk = false;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "sec-websocket-accept") {
            if (line.mid(colon + 1).trimmed() == expectedAccept)
                acceptOk = true;
        }
    }
    if (!acceptOk) {
        onSocketError();
        return;
    }

    m_httpUpgraded = true;
    // SignalR MessagePack handshake (terminated with ASCII Record Separator 0x1e).
    sendWebSocketFrame(0x1, QByteArray("{\"protocol\":\"messagepack\",\"version\":1}\x1e"));
}

void BwSync::processWebSocketFrames() {
    while (m_readBuffer.size() >= 2) {
        const auto *raw = reinterpret_cast<const uint8_t *>(m_readBuffer.constData());
        const bool fin = (raw[0] & 0x80) != 0;
        const quint8 opcode = raw[0] & 0x0f;
        const bool masked = (raw[1] & 0x80) != 0;
        quint64 payloadLen = raw[1] & 0x7f;
        qsizetype offset = 2;

        if (payloadLen == 126) {
            if (m_readBuffer.size() < offset + 2)
                return;
            payloadLen = qFromBigEndian<quint16>(raw + offset);
            offset += 2;
        } else if (payloadLen == 127) {
            if (m_readBuffer.size() < offset + 8)
                return;
            payloadLen = qFromBigEndian<quint64>(raw + offset);
            offset += 8;
        }

        if (payloadLen > 16 * 1024 * 1024) {
            onSocketError();
            return;
        }

        const qsizetype maskLen = masked ? 4 : 0;
        if (m_readBuffer.size() < offset + maskLen + qsizetype(payloadLen))
            return;

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked) {
            std::memcpy(mask, raw + offset, 4);
            offset += 4;
        }

        QByteArray payload = m_readBuffer.mid(offset, qsizetype(payloadLen));
        m_readBuffer.remove(0, offset + qsizetype(payloadLen));

        if (masked) {
            for (qsizetype i = 0; i < payload.size(); ++i)
                payload[i] = char(uint8_t(payload[i]) ^ mask[i % 4]);
        }

        // Control frames (Close/Ping/Pong) may arrive mid-fragmentation.
        if (opcode == 0x8) {
            sendWebSocketFrame(0x8, QByteArray());
            if (m_socket)
                m_socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x9) {
            sendWebSocketFrame(0xA, payload);
            continue;
        }
        if (opcode == 0xA)
            continue;

        if (opcode != 0x0) {
            if (fin) {
                handleCompleteMessage(opcode, payload);
            } else {
                m_fragmentOpcode = opcode;
                m_fragmentPayload = payload;
            }
        } else {
            m_fragmentPayload.append(payload);
            if (fin) {
                const quint8 op = m_fragmentOpcode;
                const QByteArray full = std::exchange(m_fragmentPayload, QByteArray());
                m_fragmentOpcode = 0;
                handleCompleteMessage(op, full);
            }
        }
    }
}

void BwSync::handleCompleteMessage(quint8 opcode, const QByteArray &payload) {
    if (opcode == 0x1) {
        // Handshake response arrives as JSON terminated with 0x1e.
        const qsizetype sep = payload.indexOf('\x1e');
        const QByteArray jsonBytes = sep >= 0 ? payload.left(sep) : payload;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (doc.isObject() && !doc.object().contains(QStringLiteral("error"))) {
            m_handshakeDone = true;
            m_pingTimer.start();
        } else if (doc.isObject() && doc.object().contains(QStringLiteral("error"))) {
            onSocketError();
        }
        return;
    }

    if (opcode == 0x2) {
        const QVector<QVariant> messages = decodeSignalRFrames(payload);
        for (const QVariant &message : messages) {
            if (const std::optional<BwSyncEvent> event = parseNotification(message))
                emit eventReceived(*event);
        }
    }
}

void BwSync::sendWebSocketFrame(quint8 opcode, const QByteArray &payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray frame;
    frame.append(char(0x80 | (opcode & 0x0f)));

    const quint64 len = quint64(payload.size());
    if (len < 126) {
        frame.append(char(0x80 | len));
    } else if (len <= 0xffff) {
        frame.append(char(0x80 | 126));
        quint16 be = qToBigEndian(quint16(len));
        frame.append(reinterpret_cast<const char *>(&be), 2);
    } else {
        frame.append(char(0x80 | 127));
        quint64 be = qToBigEndian(len);
        frame.append(reinterpret_cast<const char *>(&be), 8);
    }

    const BwBytes mask = BwCrypto::randomBytes(4);
    if (mask.size() != 4)
        return;
    frame.append(reinterpret_cast<const char *>(mask.data()), 4);

    const qsizetype start = frame.size();
    frame.append(payload);
    for (qsizetype i = 0; i < payload.size(); ++i)
        frame[start + i] = char(uint8_t(frame[start + i]) ^ mask[size_t(i % 4)]);

    m_socket->write(frame);
    m_socket->flush();
}

void BwSync::sendSignalRPing() {
    // SignalR MessagePack Ping: length=2, fixarray(1), positive fixint 6.
    static const QByteArray ping("\x02\x91\x06", 3);
    sendWebSocketFrame(0x2, ping);
}

void BwSync::onSocketDisconnected() {
    m_pingTimer.stop();
    m_httpUpgraded = false;
    m_handshakeDone = false;
    if (!m_stopping)
        emit disconnected(false);
}

void BwSync::onSocketError() {
    m_pingTimer.stop();
    m_httpUpgraded = false;
    m_handshakeDone = false;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (!m_stopping)
        emit disconnected(true);
}

QVector<QVariant> BwSync::decodeSignalRFrames(const QByteArray &payload) {
    QVector<QVariant> messages;
    const auto *data = reinterpret_cast<const uint8_t *>(payload.constData());
    const qsizetype total = payload.size();
    qsizetype pos = 0;

    while (pos < total) {
        // Unsigned LEB128 varint length prefix.
        quint64 length = 0;
        int shift = 0;
        bool complete = false;
        while (pos < total && shift < 35) {
            const uint8_t byte = data[pos++];
            length |= quint64(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) {
                complete = true;
                break;
            }
            shift += 7;
        }
        if (!complete || length == 0 || pos + qsizetype(length) > total)
            break;

        MsgPackReader reader(data + pos, qsizetype(length));
        const QVariant value = reader.readValue();
        if (reader.ok() && value.isValid())
            messages.append(value);
        pos += qsizetype(length);
    }
    return messages;
}

std::optional<BwSyncEvent> BwSync::parseNotification(const QVariant &message) {
    const QVariantList frame = message.toList();
    // SignalR Invocation: [1, headers, invocationId, target, arguments]
    if (frame.size() < 5 || frame.at(0).toInt() != 1)
        return std::nullopt;
    if (frame.at(3).toString() != QLatin1String("ReceiveMessage"))
        return std::nullopt;

    const QVariantList args = frame.at(4).toList();
    if (args.isEmpty())
        return std::nullopt;

    const QVariantMap envelope = args.first().toMap();
    const QVariant typeVar = mapLookupIgnoreCase(envelope, QStringLiteral("type"));
    if (!typeVar.isValid())
        return std::nullopt;

    const int type = typeVar.toInt();
    const QVariantMap payload = mapLookupIgnoreCase(envelope, QStringLiteral("payload")).toMap();
    const QString cipherId = mapLookupIgnoreCase(payload, QStringLiteral("id")).toString();

    switch (type) {
    case 0:  // SyncCipherUpdate
    case 1:  // SyncCipherCreate
        if (!cipherId.isEmpty())
            return BwSyncEvent{BwSyncEvent::Kind::CipherChanged, cipherId};
        return BwSyncEvent{BwSyncEvent::Kind::FullSync, {}};
    case 2:   // SyncLoginDelete
    case 11:  // SyncCipherDelete
        if (!cipherId.isEmpty())
            return BwSyncEvent{BwSyncEvent::Kind::CipherDeleted, cipherId};
        return BwSyncEvent{BwSyncEvent::Kind::FullSync, {}};
    case 3:  // SyncFolderDelete
    case 4:  // SyncCiphers
    case 5:  // SyncVault
    case 6:  // SyncOrgKeys
    case 7:  // SyncFolderCreate
    case 8:  // SyncFolderUpdate
        return BwSyncEvent{BwSyncEvent::Kind::FullSync, {}};
    default:
        return std::nullopt;
    }
}
