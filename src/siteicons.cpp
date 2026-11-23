#include "siteicons.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {

constexpr int maxRunning = 4;
constexpr int iconSize = 32;
constexpr qint64 retryAfterSeconds = 7 * 24 * 60 * 60;

// A generic client name: nothing here should tell a site who is asking.
const QByteArray userAgent = "Mozilla/5.0 (compatible; omapass)";

QString attribute(const QString &tag, const QString &name) {
    // name="value", name='value' or name=value
    const QRegularExpression pattern(
        QStringLiteral("\\b%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(tag);
    if (!match.hasMatch())
        return QString();
    for (int group = 1; group <= 3; ++group) {
        if (match.capturedStart(group) >= 0)
            return match.captured(group).trimmed();
    }
    return QString();
}

}

SiteIcons::SiteIcons(const QString &cacheDir, QObject *parent)
    : QObject(parent), m_cacheDir(cacheDir) {
    // https never gives way to http on a redirect.
    m_network.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QString SiteIcons::originOf(const QString &url) {
    QString text = url.trimmed();
    if (text.isEmpty())
        return QString();
    if (!text.contains(QStringLiteral("://")))
        text.prepend(QStringLiteral("https://"));

    const QUrl parsed(text, QUrl::TolerantMode);
    const QString scheme = parsed.scheme().toLower();
    const QString host = parsed.host().toLower();
    if (!parsed.isValid() || host.isEmpty()
        || (scheme != QLatin1String("https") && scheme != QLatin1String("http")))
        return QString();

    QString origin = scheme + QStringLiteral("://") + host;
    const int port = parsed.port();
    if (port > 0 && !((scheme == QLatin1String("https") && port == 443)
                      || (scheme == QLatin1String("http") && port == 80)))
        origin += QLatin1Char(':') + QString::number(port);
    return origin;
}

QString SiteIcons::keyOf(const QString &origin) {
    return QString::fromLatin1(
        QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

QString SiteIcons::pathFor(const QString &key, const char *suffix) const {
    return m_cacheDir + QLatin1Char('/') + key + QLatin1String(suffix);
}

QImage SiteIcons::image(const QString &key) const {
    // Keys are hashes; anything else is not a name in the cache.
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{32}$"));
    if (!hex.match(key).hasMatch())
        return QImage();
    return QImage(pathFor(key, ".png"));
}

bool SiteIcons::hasIcon(const QString &origin) const {
    return !origin.isEmpty() && QFileInfo::exists(pathFor(keyOf(origin), ".png"));
}

void SiteIcons::request(const QString &origin) {
    if (origin.isEmpty() || m_pending.contains(origin) || hasIcon(origin))
        return;

    // A site that had nothing is left alone for a while. The mark says
    // whether DuckDuckGo was asked too; when it was not and now may be, it
    // is the only one asked — the site already said it has nothing.
    Job job;
    job.origin = origin;
    const QFileInfo none(pathFor(keyOf(origin), ".none"));
    if (none.exists()
        && none.lastModified().secsTo(QDateTime::currentDateTime()) < retryAfterSeconds) {
        QFile mark(none.filePath());
        const bool askedDuckDuckGo = mark.open(QIODevice::ReadOnly)
            && mark.readAll().trimmed() == "duckduckgo";
        if (!m_duckDuckGo || askedDuckDuckGo)
            return;
        job.pageRead = true;
        job.askedDuckDuckGo = true;
        job.candidates.append(duckDuckGoFor(origin));
    }

    m_pending.insert(origin);
    m_queue.append(job);
    pump();
}

void SiteIcons::pump() {
    while (m_running < maxRunning && !m_queue.isEmpty()) {
        ++m_running;
        next(m_queue.takeFirst());
    }
}

void SiteIcons::next(Job job) {
    if (!job.pageRead) {
        job.pageRead = true;
        fetch(job, QUrl(job.origin + QLatin1Char('/')), true);
        return;
    }
    if (!job.candidates.isEmpty()) {
        const QUrl url = job.candidates.takeFirst();
        fetch(job, url, false);
        return;
    }
    giveUp(job);
}

QUrl SiteIcons::duckDuckGoFor(const QString &origin) const {
    return QUrl(m_duckDuckGoUrl.arg(QUrl(origin).host()));
}

void SiteIcons::fetch(Job job, const QUrl &url, bool isPage) {
    // An icon given inline in the page needs no request.
    if (url.scheme() == QLatin1String("data")) {
        const QString path = url.path();
        const qsizetype comma = path.indexOf(QLatin1Char(','));
        const QByteArray payload = comma < 0 ? QByteArray() : path.mid(comma + 1).toLatin1();
        const QImage icon = path.left(comma).endsWith(QLatin1String(";base64"))
            ? decodeIcon(QByteArray::fromBase64(payload))
            : decodeIcon(QByteArray::fromPercentEncoding(payload));
        if (!store(job.origin, icon))
            next(job);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    // No cookies go out with the request, and none it sets are kept.
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = m_network.get(request);
    // Anything bigger than an icon (or a home page) is not one.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > maxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, job, reply, isPage]() {
        finished(job, reply, isPage);
    });
}

void SiteIcons::finished(Job job, QNetworkReply *reply, bool isPage) {
    reply->deleteLater();
    const bool ok = reply->error() == QNetworkReply::NoError;
    const QByteArray body = ok ? reply->read(maxBytes + 1) : QByteArray();

    if (isPage) {
        // What the page links to comes first, then the conventional places.
        if (ok && body.size() <= maxBytes)
            job.candidates = iconLinks(QString::fromUtf8(body), reply->url());
        job.candidates.append(QUrl(job.origin + QStringLiteral("/favicon.ico")));
        if (m_duckDuckGo) {
            job.candidates.append(duckDuckGoFor(job.origin));
            job.askedDuckDuckGo = true;
        }
        next(job);
        return;
    }

    const QImage icon = ok && body.size() <= maxBytes ? decodeIcon(body) : QImage();
    if (!store(job.origin, icon))
        next(job);
}

bool SiteIcons::store(const QString &origin, const QImage &icon) {
    if (icon.isNull() || !QDir().mkpath(m_cacheDir) || !icon.save(pathFor(keyOf(origin), ".png")))
        return false;

    QFile::remove(pathFor(keyOf(origin), ".none"));
    m_pending.remove(origin);
    --m_running;
    emit iconReady(origin);
    pump();
    return true;
}

void SiteIcons::giveUp(const Job &job) {
    const QString &origin = job.origin;
    if (QDir().mkpath(m_cacheDir)) {
        QFile none(pathFor(keyOf(origin), ".none"));
        if (none.open(QIODevice::WriteOnly | QIODevice::Truncate))
            none.write(job.askedDuckDuckGo ? "duckduckgo" : "direct");
    }
    m_pending.remove(origin);
    --m_running;
    pump();
}

QList<QUrl> SiteIcons::iconLinks(const QString &html, const QUrl &page) {
    QList<QUrl> preferred;
    QList<QUrl> touch;

    static const QRegularExpression linkTag(QStringLiteral("<link\\b[^>]*>"),
                                            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = linkTag.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QStringList rel = attribute(tag, QStringLiteral("rel")).toLower()
                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString href = attribute(tag, QStringLiteral("href"));
        if (href.isEmpty())
            continue;

        const QString type = attribute(tag, QStringLiteral("type")).toLower();
        if (type.contains(QLatin1String("svg")) || href.toLower().endsWith(QLatin1String(".svg")))
            continue;

        const QUrl url = page.resolved(QUrl(href));
        const QString scheme = url.scheme();
        if (scheme != QLatin1String("https") && scheme != QLatin1String("http")
            && scheme != QLatin1String("data"))
            continue;

        if (rel.contains(QLatin1String("icon")))
            preferred.append(url);
        else if (rel.contains(QLatin1String("apple-touch-icon"))
                 || rel.contains(QLatin1String("apple-touch-icon-precomposed")))
            touch.append(url);
    }
    return preferred + touch;
}

QImage SiteIcons::decodeIcon(const QByteArray &data) {
    if (data.isEmpty())
        return QImage();

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    // An .ico carries several sizes; the largest is the one to scale down.
    QImage best;
    do {
        const QImage frame = reader.read();
        if (!frame.isNull() && frame.width() * frame.height() > best.width() * best.height())
            best = frame;
    } while (reader.jumpToNextImage());

    if (best.isNull() || best.width() < 1 || best.height() < 1)
        return QImage();
    if (best.width() > iconSize || best.height() > iconSize)
        best = best.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return best.convertToFormat(QImage::Format_ARGB32);
}
