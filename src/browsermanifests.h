#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The native messaging manifests that tell each browser which program to
// start for the KeePassXC-Browser extension (`org.keepassxc.keepassxc_browser`).
// They point at the omapass binary, which then runs as the proxy (see
// browserproxy.h). Linux only, in the user's own directories.
namespace BrowserManifests {

inline constexpr char hostName[] = "org.keepassxc.keepassxc_browser";

struct Browser {
    QString name;       // shown to the user
    QString configDir;  // the browser's own directory: no manifest without it
    QString manifest;   // where the manifest goes
    bool firefox = false;
};

// Every browser omapass knows where to register with, installed or not,
// under `home`.
QVector<Browser> knownBrowsers(const QString &home);

// The program the manifests should start: the AppImage when running from
// one (the binary inside it lives in a mount that changes every run), or
// this executable.
QString proxyPath();

// The manifest omapass writes for `browser`.
QByteArray manifestFor(const Browser &browser, const QString &proxy);

struct Result {
    QStringList installed;   // browser names
    // Browsers whose manifest starts some other program (KeePassXC's own
    // proxy), left alone unless `replace` was set. Another omapass (a build
    // tree's, the package's) is not another program: it is replaced.
    QStringList conflicts;
    QStringList failed;
};

// Writes the manifest of every browser present under `home`.
Result install(const QString &home, const QString &proxy, bool replace);
// Removes the manifests that start `proxy` or another omapass, and only
// those.
void uninstall(const QString &home, const QString &proxy);

}
