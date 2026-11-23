#include "browsermanifests.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace BrowserManifests {

namespace {

// The extension's ids in the Chrome Web Store and the Edge Add-ons store,
// and in Mozilla's: the same ones KeePassXC allows.
const QStringList chromeOrigins{QStringLiteral("chrome-extension://pdffhmdngciaglkoonimfcmckehcpafo/"),
                                QStringLiteral("chrome-extension://oboonakemofpalcgghocfoadofidjkkk/")};
const QStringList firefoxExtensions{QStringLiteral("keepassxc-browser@keepassxc.org")};

QString manifestName() {
    return QLatin1String(hostName) + QStringLiteral(".json");
}

// The program a manifest already there starts, empty when it is not one.
QString pathIn(const QString &manifest) {
    QFile file(manifest);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("path")).toString();
}

}

QVector<Browser> knownBrowsers(const QString &home) {
    const QString config = home + QStringLiteral("/.config/");
    const auto chromium = [&](const QString &name, const QString &dir) {
        return Browser{name, config + dir,
                       config + dir + QStringLiteral("/NativeMessagingHosts/") + manifestName(), false};
    };
    const auto firefox = [&](const QString &name, const QString &dir) {
        return Browser{name, home + QLatin1Char('/') + dir,
                       home + QLatin1Char('/') + dir + QStringLiteral("/native-messaging-hosts/")
                           + manifestName(),
                       true};
    };
    return {
        firefox(QStringLiteral("Firefox"), QStringLiteral(".mozilla")),
        firefox(QStringLiteral("LibreWolf"), QStringLiteral(".librewolf")),
        chromium(QStringLiteral("Chrome"), QStringLiteral("google-chrome")),
        chromium(QStringLiteral("Chromium"), QStringLiteral("chromium")),
        chromium(QStringLiteral("Brave"), QStringLiteral("BraveSoftware/Brave-Browser")),
        chromium(QStringLiteral("Vivaldi"), QStringLiteral("vivaldi")),
        chromium(QStringLiteral("Edge"), QStringLiteral("microsoft-edge")),
    };
}

QString proxyPath() {
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
        return appImage;
    return QCoreApplication::applicationFilePath();
}

QByteArray manifestFor(const Browser &browser, const QString &proxy) {
    QJsonObject manifest{
        {QStringLiteral("name"), QLatin1String(hostName)},
        {QStringLiteral("description"), QStringLiteral("omapass integration with native messaging support")},
        {QStringLiteral("path"), proxy},
        {QStringLiteral("type"), QStringLiteral("stdio")},
    };
    if (browser.firefox)
        manifest.insert(QStringLiteral("allowed_extensions"), QJsonArray::fromStringList(firefoxExtensions));
    else
        manifest.insert(QStringLiteral("allowed_origins"), QJsonArray::fromStringList(chromeOrigins));
    return QJsonDocument(manifest).toJson();
}

Result install(const QString &home, const QString &proxy, bool replace) {
    Result result;
    for (const Browser &browser : knownBrowsers(home)) {
        if (!QFileInfo(browser.configDir).isDir())
            continue;

        const QString current = pathIn(browser.manifest);
        if (!current.isEmpty() && current != proxy && QFileInfo::exists(current) && !replace) {
            result.conflicts.append(browser.name);
            continue;
        }

        const QByteArray wanted = manifestFor(browser, proxy);
        QFile existing(browser.manifest);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == wanted) {
            result.installed.append(browser.name);
            continue;
        }
        existing.close();

        QSaveFile file(browser.manifest);
        if (!QDir().mkpath(QFileInfo(browser.manifest).absolutePath())
            || !file.open(QIODevice::WriteOnly) || file.write(wanted) != wanted.size() || !file.commit()) {
            result.failed.append(browser.name);
            continue;
        }
        result.installed.append(browser.name);
    }
    return result;
}

void uninstall(const QString &home, const QString &proxy) {
    for (const Browser &browser : knownBrowsers(home)) {
        if (pathIn(browser.manifest) == proxy)
            QFile::remove(browser.manifest);
    }
}

}
