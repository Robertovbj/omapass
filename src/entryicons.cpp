#include "entryicons.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QSaveFile>

#include "siteicons.h"
#include "vault.h"

namespace {

QString hashOf(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(32));
}

}

EntryIcons::EntryIcons(const QString &localDir) : m_localDir(localDir) {}

QString EntryIcons::folderFor(const Vault *vault) const {
    const QByteArray id = QByteArray::number(int(vault->kind())) + ':' + vault->path().toUtf8();
    return m_localDir + QLatin1Char('/') + hashOf(id);
}

QString EntryIcons::fileFor(const Vault *vault, const QString &entryPath) const {
    return folderFor(vault) + QLatin1Char('/') + hashOf(entryPath.toUtf8()) + QStringLiteral(".png");
}

void EntryIcons::insert(const QString &entryPath, const QByteArray &png) {
    const QImage image = QImage::fromData(png);
    if (image.isNull())
        return;
    const QString key = hashOf(entryPath.toUtf8() + '\0' + png);
    m_keys.insert(entryPath, key);
    const QMutexLocker locker(&m_mutex);
    m_images.insert(key, image);
}

void EntryIcons::load(const Vault *vault, const QStringList &entries) {
    m_keys.clear();
    {
        const QMutexLocker locker(&m_mutex);
        m_images.clear();
    }
    if (!vault)
        return;

    if (vault->storesIcons()) {
        const QHash<QString, QByteArray> icons = vault->entryIcons();
        for (auto it = icons.cbegin(); it != icons.cend(); ++it)
            insert(it.key(), it.value());
        return;
    }

    // Only the entries that exist are looked up: the names are hashes, so
    // there is nothing to read them back from.
    const QDir folder(folderFor(vault));
    if (!folder.exists())
        return;
    for (const QString &entry : std::as_const(entries)) {
        QFile file(fileFor(vault, entry));
        if (file.open(QIODevice::ReadOnly))
            insert(entry, file.readAll());
    }
}

QString EntryIcons::urlFor(const QString &entryPath) const {
    const QString key = m_keys.value(entryPath);
    return key.isEmpty() ? QString() : QStringLiteral("image://entryicon/") + key;
}

QImage EntryIcons::image(const QString &key) const {
    const QMutexLocker locker(&m_mutex);
    return m_images.value(key);
}

bool EntryIcons::setLocal(const Vault *vault, const QString &entryPath, const QByteArray &png) {
    const QString path = fileFor(vault, entryPath);
    if (png.isEmpty()) {
        m_keys.remove(entryPath);
        return !QFile::exists(path) || QFile::remove(path);
    }

    if (!QDir().mkpath(folderFor(vault)))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit())
        return false;
    insert(entryPath, png);
    return true;
}

void EntryIcons::moveLocal(const Vault *vault, const QString &from, const QString &to) {
    if (from == to)
        return;
    const QString source = fileFor(vault, from);
    if (!QFile::exists(source))
        return;
    const QString target = fileFor(vault, to);
    QFile::remove(target);
    QFile::rename(source, target);
}

QByteArray EntryIcons::fromFile(const QString &path) {
    QFile file(path);
    // A picture far bigger than any icon is not worth decoding.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return QByteArray();
    const QImage icon = SiteIcons::decodeIcon(file.readAll());
    if (icon.isNull())
        return QByteArray();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!icon.save(&buffer, "PNG"))
        return QByteArray();
    return png;
}
