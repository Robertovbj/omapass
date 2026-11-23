#include "iconchoice.h"

#include <QCryptographicHash>
#include <QSettings>

#include "vault.h"

namespace {

QString flag(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

bool inDatabase(const Vault *vault) {
    return vault->kind() == VaultKind::Keepass;
}

// The settings group of a vault that keeps its choice here, named after a
// hash so omapass.conf does not list where the vaults are.
QString localGroup(const Vault *vault) {
    const QByteArray id = QByteArray::number(int(vault->kind())) + ':' + vault->path().toUtf8();
    return QStringLiteral("siteIcons/")
        + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().left(32));
}

}

QString IconChoice::downloadKey() {
    return QStringLiteral("OMAPASS_SITE_ICONS");
}

QString IconChoice::duckDuckGoKey() {
    return QStringLiteral("OMAPASS_SITE_ICONS_DUCKDUCKGO");
}

IconChoice IconChoice::load(const Vault *vault, bool defaultDownload, bool defaultDuckDuckGo) {
    IconChoice choice;
    choice.download = defaultDownload;
    choice.duckDuckGo = defaultDownload && defaultDuckDuckGo;
    if (!vault)
        return choice;

    QString download;
    QString duckDuckGo;
    if (inDatabase(vault)) {
        const QHash<QString, QString> data = vault->customData();
        download = data.value(downloadKey());
        duckDuckGo = data.value(duckDuckGoKey());
    } else {
        QSettings settings;
        settings.beginGroup(localGroup(vault));
        download = settings.value(QStringLiteral("download")).toString();
        duckDuckGo = settings.value(QStringLiteral("duckduckgo")).toString();
    }

    if (!download.isEmpty()) {
        choice.chosen = true;
        choice.download = download == QLatin1String("true");
        choice.duckDuckGo = choice.download && duckDuckGo == QLatin1String("true");
        return choice;
    }
    if (vault->storesIcons() && !vault->entryIcons().isEmpty())
        choice.download = true;
    return choice;
}

bool IconChoice::save(const Vault *vault, bool download, bool duckDuckGo, QString *error) {
    duckDuckGo = download && duckDuckGo;
    if (inDatabase(vault))
        return vault->setCustomData(downloadKey(), flag(download), error)
            && vault->setCustomData(duckDuckGoKey(), flag(duckDuckGo), error);
    QSettings settings;
    settings.beginGroup(localGroup(vault));
    settings.setValue(QStringLiteral("download"), flag(download));
    settings.setValue(QStringLiteral("duckduckgo"), flag(duckDuckGo));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        *error = settings.fileName();
        return false;
    }
    return true;
}
