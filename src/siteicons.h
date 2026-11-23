#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

// The icons of the sites the entries point to, fetched the way KeePassXC
// fetches them and kept in a cache of their own.
//
// Off unless the user turns it on: fetching means asking every site with
// an entry for its icon. Only the origin (scheme, host and port) of an
// entry's URL ever goes out — never its path, query or anything else in
// the entry — and the requests carry no cookies.
//
// For each site, in order: the icons the home page links to
// (`<link rel="icon">`), then /favicon.ico, then — only when the user also
// allowed it — DuckDuckGo's icon service, which does get told the host.
// The first that decodes wins; a site with none is not asked again for a
// week.
class SiteIcons : public QObject {
    Q_OBJECT

public:
    explicit SiteIcons(const QString &cacheDir, QObject *parent = nullptr);

    void setDuckDuckGo(bool enabled) { m_duckDuckGo = enabled; }

    // `scheme://host[:port]` of an entry's URL, lower case; "example.com"
    // and other scheme-less URLs are taken as https. Empty when there is no
    // http(s) host to ask.
    static QString originOf(const QString &url);

    // The cache's name for an origin: a hash, so the directory listing does
    // not spell out the sites.
    static QString keyOf(const QString &origin);

    // The cached icon for a key, or a null image. Reads the file only, so
    // it is safe from the image provider's thread.
    QImage image(const QString &key) const;
    bool hasIcon(const QString &origin) const;

    // Starts fetching `origin`'s icon unless it is cached, known to have
    // none, or already on its way. iconReady() follows when it arrives.
    void request(const QString &origin);

    // Exposed for testing: the icon URLs a home page links to, best first —
    // "icon" and "shortcut icon" before "apple-touch-icon", SVG left out
    // (nothing here draws it), relative ones resolved against `page`.
    static QList<QUrl> iconLinks(const QString &html, const QUrl &page);

    // Exposed for testing: an image made into the cached form, at most 32
    // pixels on a side. Null when `data` does not decode.
    static QImage decodeIcon(const QByteArray &data);

    static constexpr int maxBytes = 1024 * 1024;
    static constexpr int timeoutMs = 10000;

signals:
    void iconReady(const QString &origin);

private:
    struct Job {
        QString origin;
        QList<QUrl> candidates;   // what is left to try
        bool pageRead = false;    // the home page has been asked for its links
    };

    void pump();
    void next(Job job);
    void fetch(Job job, const QUrl &url, bool isPage);
    void finished(Job job, QNetworkReply *reply, bool isPage);
    // Caches `icon` as `origin`'s and ends its job; false (and nothing
    // ended) when the image is null or cannot be written.
    bool store(const QString &origin, const QImage &icon);
    void giveUp(const QString &origin);
    QString pathFor(const QString &key, const char *suffix) const;

    QString m_cacheDir;
    QNetworkAccessManager m_network;
    bool m_duckDuckGo = false;
    QList<Job> m_queue;
    QSet<QString> m_pending;      // queued or running
    int m_running = 0;
};
