#include "hyprrounding.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

HyprRounding::HyprRounding(QObject *parent, const QString &hyprctl, const QString &events)
    : QObject(parent) {
    const bool hyprland = !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : hyprland ? QStandardPaths::findExecutable(QStringLiteral("hyprctl")) : QString();

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(500);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::errorOccurred, this, [this] { m_timeout.stop(); });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        const QJsonValue value =
            QJsonDocument::fromJson(m_process.readAllStandardOutput()).object().value(QStringLiteral("int"));
        if (code == 0 && status == QProcess::NormalExit && value.isDouble() && value.toInt(-1) >= 0
            && value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit radiusChanged(m_radius);
        }
        if (m_again) {
            m_again = false;
            refresh();
        }
    });

    const QString socket = !events.isEmpty() ? events
                         : hyprland ? qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/hypr/")
                                          + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE")
                                          + QStringLiteral("/.socket2.sock")
                                    : QString();
    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        while (m_events.canReadLine())
            if (m_events.readLine().startsWith("configreloaded>>"))
                refresh();
    });
    // Asked again on every connection, since a reload may have come between.
    connect(&m_events, &QLocalSocket::connected, this, [this] {
        m_reconnect.setInterval(1000);
        refresh();
    });
    // A socket that can't be reached or drops is tried again, less often each time.
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(1000);
    connect(&m_events, &QLocalSocket::disconnected, &m_reconnect, qOverload<>(&QTimer::start));
    connect(&m_events, &QLocalSocket::errorOccurred, &m_reconnect, qOverload<>(&QTimer::start));
    connect(&m_reconnect, &QTimer::timeout, this, [this, socket] {
        m_reconnect.setInterval(std::min(m_reconnect.interval() * 2, 60000));
        m_events.abort();
        m_events.connectToServer(socket, QIODevice::ReadOnly);
    });
    if (!socket.isEmpty())
        m_events.connectToServer(socket, QIODevice::ReadOnly);
    refresh();
}

HyprRounding::~HyprRounding() {
    // Nothing reconnects or asks again while the members go.
    m_events.disconnect();
    m_process.disconnect();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void HyprRounding::refresh() {
    if (m_hyprctl.isEmpty())
        return;
    // A query still running may have read the value from before the reload.
    if (m_process.state() != QProcess::NotRunning) {
        m_again = true;
        return;
    }
    m_process.start(m_hyprctl, {QStringLiteral("-j"), QStringLiteral("getoption"),
                                QStringLiteral("decoration:rounding")});
    m_timeout.start();
}
