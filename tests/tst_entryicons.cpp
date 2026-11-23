// The icons the user picks, kept on this computer for the backends that have
// no place for one in the database.

#include "support.h"

#include "entryicons.h"
#include "vault.h"

#include <QBuffer>

namespace {

// Just enough of a vault to name a database; nothing is read from it.
class FakeVault : public Vault {
public:
    explicit FakeVault(const QString &path) : Vault(VaultKind::Pass, path) {}
    void list(QStringList *, QStringList *) const override {}
    QString titleFor(const QString &, const QString &fallback) const override { return fallback; }
    bool fetchPassword(const QString &, Secret *, QString *) const override { return false; }
    bool fetchEntry(const QString &, EntryData *, QString *) const override { return false; }
    bool addEntry(const QString &, const QString &, const EntryData &, QString *) const override { return false; }
    bool editEntry(const QString &, const QString &, const QString &, const EntryData &,
                   QString *) const override { return false; }
    bool removeEntry(const QString &, QString *) const override { return false; }
    bool renameGroup(const QString &, const QString &, QString *) const override { return false; }
    bool removeGroup(const QString &, QString *) const override { return false; }
};

QString writeImage(const QTemporaryDir &dir, int width, int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const QString path = dir.filePath(QStringLiteral("icone.png"));
    image.save(path);
    return path;
}

}

class TestEntryIcons : public QObject {
    Q_OBJECT

private slots:
    void entryIconsAreMadeSmallPngs() {
        QTemporaryDir dir;
        const QByteArray png = EntryIcons::fromFile(writeImage(dir, 200, 100));
        QVERIFY(png.startsWith("\x89PNG"));
        QCOMPARE(QImage::fromData(png).size(), QSize(32, 16));

        QFile text(dir.filePath(QStringLiteral("nota.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("isto não é uma imagem");
        text.close();
        QVERIFY(EntryIcons::fromFile(text.fileName()).isEmpty());
        QVERIFY(EntryIcons::fromFile(dir.filePath(QStringLiteral("sumiu.png"))).isEmpty());
    }

    void entryIconsLiveOnThisComputerAndFollowTheEntry() {
        QTemporaryDir store;
        QTemporaryDir pictures;
        const FakeVault vault(QStringLiteral("/home/ana/.password-store"));
        const QByteArray png = EntryIcons::fromFile(writeImage(pictures, 16, 16));

        EntryIcons icons(store.path());
        QVERIFY(icons.setLocal(&vault, QStringLiteral("Email/gmail"), png));
        const QString url = icons.urlFor(QStringLiteral("Email/gmail"));
        QVERIFY(url.startsWith(QLatin1String("image://entryicon/")));
        QCOMPARE(icons.image(url.mid(18)).size(), QSize(16, 16));

        // Nothing on disk spells out the database or the entry.
        QDirIterator files(store.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            QVERIFY2(!file.contains(QLatin1String("gmail")) && !file.contains(QLatin1String("password-store")),
                     qPrintable(file));
        }

        // A fresh start finds it again for the same entry, and only that one.
        EntryIcons again(store.path());
        again.load(&vault, {QStringLiteral("Email/gmail"), QStringLiteral("Email/outro")});
        QVERIFY(again.has(QStringLiteral("Email/gmail")));
        QVERIFY(!again.has(QStringLiteral("Email/outro")));

        // A renamed entry keeps it; a removed one loses it.
        again.moveLocal(&vault, QStringLiteral("Email/gmail"), QStringLiteral("Pessoal/gmail"));
        again.load(&vault, {QStringLiteral("Email/gmail"), QStringLiteral("Pessoal/gmail")});
        QVERIFY(!again.has(QStringLiteral("Email/gmail")));
        QVERIFY(again.has(QStringLiteral("Pessoal/gmail")));

        QVERIFY(again.setLocal(&vault, QStringLiteral("Pessoal/gmail"), QByteArray()));
        again.load(&vault, {QStringLiteral("Pessoal/gmail")});
        QVERIFY(!again.has(QStringLiteral("Pessoal/gmail")));

        // Another database with the same entry path does not share it.
        QVERIFY(icons.setLocal(&vault, QStringLiteral("x"), png));
        const FakeVault other(QStringLiteral("/home/ana/outra-loja"));
        EntryIcons third(store.path());
        third.load(&other, {QStringLiteral("x")});
        QVERIFY(!third.has(QStringLiteral("x")));

        icons.load(nullptr, {});
        QVERIFY(!icons.has(QStringLiteral("x")));
    }
};

OMAPASS_TEST(TestEntryIcons)
#include "tst_entryicons.moc"
