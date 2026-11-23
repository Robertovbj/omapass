#pragma once

#include <QByteArray>
#include <QList>
#include <QStringList>

// The native messaging host the browser starts for the KeePassXC-Browser
// extension: the omapass binary itself, which recognizes the arguments a
// browser passes and, instead of opening a window, relays messages between
// the browser (stdin/stdout, each message behind a 4-byte length) and the
// running omapass (BrowserServer's socket). It exits when either side goes
// away, and at once when omapass is not running, which the extension reports
// as such.
namespace BrowserProxy {

// Chrome and its kin pass the extension's origin (`chrome-extension://…/`),
// Firefox the manifest's path and the extension's id.
bool isProxyInvocation(const QStringList &args);

int run(int argc, char *argv[]);

// The complete native messages at the front of `buffer`, taken out of it.
// A length past `maxBytes` makes the stream unreadable: `broken` is set.
QList<QByteArray> takeFrames(QByteArray *buffer, bool *broken);
QByteArray frame(const QByteArray &message);

// Chrome refuses a message from the host larger than this.
constexpr quint32 maxToBrowser = 1024 * 1024;
// And sends at most this much the other way (64 MiB); nothing the
// extension says comes near it.
constexpr quint32 maxFromBrowser = 64 * 1024 * 1024;

}
