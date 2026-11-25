#include "browserproxy.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QSocketNotifier>
#include <QThread>
#include <QtEndian>

#include <cstring>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "browserserver.h"

namespace BrowserProxy {

bool isProxyInvocation(const QStringList &args) {
    if (args.size() < 2)
        return false;
    if (args.at(1).startsWith(QLatin1String("chrome-extension://")))
        return true;
    return args.size() >= 3 && args.at(1).endsWith(QLatin1String(".json"))
        && args.at(2) == QLatin1String("keepassxc-browser@keepassxc.org");
}

QList<QByteArray> takeFrames(QByteArray *buffer, bool *broken) {
    QList<QByteArray> frames;
    *broken = false;
    // Native byte order, as the browsers write it.
    while (buffer->size() >= 4) {
        quint32 length = 0;
        std::memcpy(&length, buffer->constData(), 4);
        if (length > maxFromBrowser) {
            *broken = true;
            return frames;
        }
        if (buffer->size() < qsizetype(4 + length))
            break;
        frames.append(buffer->mid(4, length));
        buffer->remove(0, 4 + length);
    }
    return frames;
}

QByteArray frame(const QByteArray &message) {
    const quint32 length = quint32(message.size());
    QByteArray out(reinterpret_cast<const char *>(&length), 4);
    out.append(message);
    return out;
}

#ifdef Q_OS_UNIX

namespace {

bool writeAll(int fd, const QByteArray &bytes) {
    qsizetype done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.constData() + done, size_t(bytes.size() - done));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        done += n;
    }
    return true;
}

}

int run(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QLocalSocket socket;
    const QString socketPath = BrowserServer::socketPath();
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(250)) {
        // omapass is not running yet: start it so clicking the browser
        // extension's icon opens the vault instead of failing silently.
        const QString appImage = qEnvironmentVariable("APPIMAGE").trimmed();
        const QString executable = (!appImage.isEmpty() && QFileInfo::exists(appImage))
            ? appImage : QCoreApplication::applicationFilePath();
        if (!QProcess::startDetached(executable, {}))
            return 0;

        QElapsedTimer deadline;
        deadline.start();
        bool connected = false;
        while (deadline.elapsed() < 6000) {
            QThread::msleep(150);
            socket.abort();
            socket.connectToServer(socketPath);
            if (socket.waitForConnected(250)) {
                connected = true;
                break;
            }
        }
        if (!connected)
            return 0;
    }

    QByteArray fromBrowser;
    QByteArray fromApp;

    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&]() {
        char chunk[65536];
        const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                return;
            // The browser closed the port.
            app.quit();
            return;
        }
        fromBrowser.append(chunk, qsizetype(n));
        bool broken = false;
        for (const QByteArray &message : takeFrames(&fromBrowser, &broken))
            socket.write(message);
        socket.flush();
        if (broken)
            app.quit();
    });

    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&]() {
        fromApp.append(socket.readAll());
        for (const QByteArray &message : BrowserServer::takeObjects(&fromApp)) {
            if (quint32(message.size()) > maxToBrowser)
                continue;
            if (!writeAll(STDOUT_FILENO, frame(message))) {
                app.quit();
                return;
            }
        }
        if (fromApp.size() > BrowserServer::maxMessageBytes)
            app.quit();
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);

    return app.exec();
}

#else

// Only Linux has the integration for now: elsewhere the socket and the
// manifests live in other places, and nothing installs this as a host.
int run(int, char *[]) {
    return 1;
}

#endif

}
