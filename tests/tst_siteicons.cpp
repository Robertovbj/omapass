// The site icons: where they are looked for, what is taken as one, and the
// whole fetch against a local server — nothing here reaches the internet.

#include "support.h"

#include "siteicons.h"

#include <QBuffer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

QByteArray pngOf(int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Answers each path from a table, 404 for the rest, and remembers what was
// asked for.
struct SiteServer {
    QTcpServer server;
    QHash<QByteArray, QPair<QByteArray, QByteArray>> pages;   // path → (type, body)
    QList<QByteArray> asked;
    QByteArray lastHeaders;

    SiteServer() {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            QTcpSocket *socket = server.nextPendingConnection();
            auto *received = new QByteArray;
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, received]() {
                *received += socket->readAll();
                if (!received->contains("\r\n\r\n"))
                    return;
                lastHeaders = *received;
                const QByteArray path = received->split(' ').value(1);
                asked.append(path);
                const auto page = pages.value(path);
                const QByteArray status = page.second.isNull() ? "404 Not Found" : "200 OK";
                socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + page.first
                              + "\r\nContent-Length: " + QByteArray::number(page.second.size())
                              + "\r\nConnection: close\r\n\r\n" + page.second);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, [socket, received]() {
                delete received;
                socket->deleteLater();
            });
        });
    }

    QString origin() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
};

}

class TestSiteIcons : public QObject {
    Q_OBJECT

private slots:
    void siteIconsTakeOnlyTheOrigin() {
        QCOMPARE(SiteIcons::originOf(QStringLiteral("https://Mail.Google.com/mail/u/0/?x=1#inbox")),
                 QStringLiteral("https://mail.google.com"));
        QCOMPARE(SiteIcons::originOf(QStringLiteral("github.com/login")), QStringLiteral("https://github.com"));
        QCOMPARE(SiteIcons::originOf(QStringLiteral("http://router.lan:8080/admin")),
                 QStringLiteral("http://router.lan:8080"));
        QCOMPARE(SiteIcons::originOf(QStringLiteral("https://a.example:443/")), QStringLiteral("https://a.example"));
        QVERIFY(SiteIcons::originOf(QString()).isEmpty());
        QVERIFY(SiteIcons::originOf(QStringLiteral("ssh://host")).isEmpty());
        QVERIFY(SiteIcons::originOf(QStringLiteral("ftp://host/x")).isEmpty());
        QVERIFY(SiteIcons::originOf(QStringLiteral("   ")).isEmpty());

        // The cache names do not spell out the sites.
        const QString key = SiteIcons::keyOf(QStringLiteral("https://github.com"));
        QCOMPARE(key.size(), 32);
        QVERIFY(!key.contains(QLatin1String("github")));
    }

    void siteIconsReadTheLinksOfAPage() {
        const QUrl page(QStringLiteral("https://example.com/home/"));
        const QString html = QStringLiteral(
            "<html><head>"
            "<link rel='apple-touch-icon' href='/touch.png'>"
            "<LINK REL=\"shortcut icon\" HREF=\"fav.ico\">"
            "<link href=\"https://cdn.example.com/i.png\" rel=\"icon\" sizes=\"32x32\">"
            "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/logo.svg\">"
            "<link rel=\"mask-icon\" href=\"/mask.svg\">"
            "<link rel=\"stylesheet\" href=\"/s.css\">"
            "<link rel=\"icon\" href=\"javascript:alert(1)\">"
            "</head></html>");

        const QList<QUrl> links = SiteIcons::iconLinks(html, page);
        QCOMPARE(links, QList<QUrl>({QUrl(QStringLiteral("https://example.com/home/fav.ico")),
                                     QUrl(QStringLiteral("https://cdn.example.com/i.png")),
                                     QUrl(QStringLiteral("https://example.com/touch.png"))}));
        QVERIFY(SiteIcons::iconLinks(QStringLiteral("<p>nada</p>"), page).isEmpty());
    }

    void siteIconsAreScaledDownAndJunkRefused() {
        const QImage big = SiteIcons::decodeIcon(pngOf(128, 64));
        QCOMPARE(big.size(), QSize(32, 16));
        QCOMPARE(SiteIcons::decodeIcon(pngOf(16, 16)).size(), QSize(16, 16));
        // /favicon.ico is what most sites have.
        QImage square(64, 64, QImage::Format_ARGB32);
        square.fill(Qt::blue);
        QByteArray ico;
        QBuffer icoBuffer(&ico);
        icoBuffer.open(QIODevice::WriteOnly);
        QVERIFY2(square.save(&icoBuffer, "ICO"), "the ico image plugin is missing");
        QCOMPARE(SiteIcons::decodeIcon(ico).size(), QSize(32, 32));

        QVERIFY(SiteIcons::decodeIcon(QByteArray("<html>not found</html>")).isNull());
        QVERIFY(SiteIcons::decodeIcon(QByteArray()).isNull());
    }

    void siteIconsFetchWhatThePageLinksTo() {
        SiteServer site;
        QVERIFY(site.server.isListening());
        site.pages.insert("/", {"text/html", "<link rel=\"icon\" href=\"/static/i.png\">"});
        site.pages.insert("/static/i.png", {"image/png", pngOf(48, 48)});

        QTemporaryDir cache;
        SiteIcons icons(cache.path());
        QSignalSpy ready(&icons, &SiteIcons::iconReady);
        icons.request(site.origin());
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.first().first().toString(), site.origin());
        QVERIFY(icons.hasIcon(site.origin()));
        QCOMPARE(icons.image(SiteIcons::keyOf(site.origin())).size(), QSize(32, 32));
        QCOMPARE(site.asked, QList<QByteArray>({"/", "/static/i.png"}));

        // Nothing about the user goes with it.
        QVERIFY(!site.lastHeaders.toLower().contains("cookie"));
        QVERIFY(site.lastHeaders.contains("User-Agent: Mozilla/5.0 (compatible; omapass)"));

        // Cached: not asked again.
        icons.request(site.origin());
        QTest::qWait(200);
        QCOMPARE(site.asked.size(), 2);
    }

    void siteIconsFallBackToFaviconIco() {
        SiteServer site;
        site.pages.insert("/", {"text/html", "<link rel=\"icon\" href=\"/missing.png\">"});
        site.pages.insert("/favicon.ico", {"image/png", pngOf(16, 16)});

        QTemporaryDir cache;
        SiteIcons icons(cache.path());
        QSignalSpy ready(&icons, &SiteIcons::iconReady);
        icons.request(site.origin());
        QVERIFY(ready.wait(5000));
        QCOMPARE(site.asked, QList<QByteArray>({"/", "/missing.png", "/favicon.ico"}));
    }

    // A site with nothing is remembered, so it is not asked on every start.
    void siteIconsRememberASiteWithNone() {
        SiteServer site;
        QTemporaryDir cache;
        {
            SiteIcons icons(cache.path());
            icons.request(site.origin());
            QTRY_VERIFY_WITH_TIMEOUT(
                QFileInfo::exists(cache.filePath(SiteIcons::keyOf(site.origin()) + QStringLiteral(".none"))),
                5000);
            QVERIFY(!icons.hasIcon(site.origin()));
        }
        const qsizetype asked = site.asked.size();
        QCOMPARE(asked, 2);   // the page and /favicon.ico; DuckDuckGo was not allowed

        SiteIcons again(cache.path());
        again.request(site.origin());
        QTest::qWait(200);
        QCOMPARE(site.asked.size(), asked);
    }

    // DuckDuckGo allowed after a site had nothing: only DuckDuckGo is asked,
    // not the site again; and once it too had nothing, nobody is.
    void siteIconsAskDuckDuckGoOnceItIsAllowed() {
        SiteServer site;      // the site itself: nothing
        SiteServer service;   // stands in for DuckDuckGo
        service.pages.insert("/ip3/127.0.0.1.ico", {"image/png", pngOf(16, 16)});
        const QString serviceUrl = service.origin() + QStringLiteral("/ip3/%1.ico");

        QTemporaryDir cache;
        const QString mark = cache.filePath(SiteIcons::keyOf(site.origin()) + QStringLiteral(".none"));
        {
            SiteIcons icons(cache.path());
            icons.setDuckDuckGoUrl(serviceUrl);
            icons.request(site.origin());
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(mark), 5000);
        }
        QCOMPARE(site.asked.size(), 2);
        QVERIFY(service.asked.isEmpty());

        SiteIcons icons(cache.path());
        icons.setDuckDuckGoUrl(serviceUrl);
        icons.setDuckDuckGo(true);
        QSignalSpy ready(&icons, &SiteIcons::iconReady);
        icons.request(site.origin());
        QVERIFY(ready.wait(5000));
        QCOMPARE(site.asked.size(), 2);
        QCOMPARE(service.asked, QList<QByteArray>({"/ip3/127.0.0.1.ico"}));
        QVERIFY(!QFileInfo::exists(mark));
    }

    void siteIconsDoNotAskDuckDuckGoTwice() {
        SiteServer site;
        SiteServer service;   // has nothing either
        QTemporaryDir cache;
        const QString mark = cache.filePath(SiteIcons::keyOf(site.origin()) + QStringLiteral(".none"));

        SiteIcons icons(cache.path());
        icons.setDuckDuckGoUrl(service.origin() + QStringLiteral("/ip3/%1.ico"));
        icons.setDuckDuckGo(true);
        icons.request(site.origin());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(mark), 5000);
        QCOMPARE(service.asked.size(), 1);

        SiteIcons again(cache.path());
        again.setDuckDuckGoUrl(service.origin() + QStringLiteral("/ip3/%1.ico"));
        again.setDuckDuckGo(true);
        again.request(site.origin());
        QTest::qWait(200);
        QCOMPARE(service.asked.size(), 1);
        QCOMPARE(site.asked.size(), 2);
    }

    void siteIconsNeverReadOutsideTheCache() {
        QTemporaryDir cache;
        const SiteIcons icons(cache.path());
        QVERIFY(icons.image(QStringLiteral("../../etc/passwd")).isNull());
        QVERIFY(icons.image(QString()).isNull());
    }
};

OMAPASS_TEST(TestSiteIcons)
#include "tst_siteicons.moc"
