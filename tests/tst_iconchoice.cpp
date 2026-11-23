// Whether a vault's rows ask the sites for icons: chosen per vault, kept in
// the .kdbx for KeePass so it follows the file, and switched on for a
// database KeePassXC already filled with icons.

#include "support.h"

#include "iconchoice.h"
#include "kdbx.h"
#include "keepassvault.h"

namespace {

class LocalVault : public Vault {
public:
    explicit LocalVault(const QString &path) : Vault(VaultKind::Pass, path) {}
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

// A .kdbx with one entry, with a stored icon or without.
std::unique_ptr<KeepassVault> keepass(const QTemporaryDir &dir, bool withIcon) {
    const QString path = dir.filePath(withIcon ? "com-icone.kdbx" : "sem-icone.kdbx");
    const Secret password(QStringLiteral("senha"));
    auto db = KdbxDatabase::create(password, "Teste");
    KdbxEntry entry;
    entry.title = "GitHub";
    entry.password = Secret(QStringLiteral("p"));
    db->addEntry(QString(), entry);
    if (withIcon)
        db->setEntryIcon("GitHub", "png-do-keepassxc");
    db->save(path);
    QString error;
    return std::unique_ptr<KeepassVault>(KeepassVault::unlock(path, password, &error));
}

}

class TestIconChoice : public QObject {
    Q_OBJECT

private slots:
    void iconChoiceFollowsTheDefaultWithoutAChoice() {
        QTemporaryDir dir;
        auto vault = keepass(dir, false);
        QVERIFY(vault);
        IconChoice choice = IconChoice::load(vault.get(), false, true);
        QVERIFY(!choice.download && !choice.duckDuckGo && !choice.chosen);
        choice = IconChoice::load(vault.get(), true, true);
        QVERIFY(choice.download && choice.duckDuckGo && !choice.chosen);
        // DuckDuckGo is a fallback of downloading, never on alone.
        QVERIFY(!IconChoice::load(vault.get(), false, true).duckDuckGo);
    }

    // A database KeePassXC filled with favicons: the rest of its entries
    // are fetched too, even with the default off.
    void iconChoiceTurnsOnForADatabaseWithIcons() {
        QTemporaryDir dir;
        auto vault = keepass(dir, true);
        QVERIFY(vault);
        const IconChoice choice = IconChoice::load(vault.get(), false, false);
        QVERIFY(choice.download);
        QVERIFY(!choice.duckDuckGo);
        QVERIFY(!choice.chosen);
    }

    // The choice goes in the .kdbx and wins over both rules.
    void iconChoiceTravelsWithTheKdbx() {
        QTemporaryDir dir;
        auto vault = keepass(dir, true);
        QVERIFY(vault);
        QString error;
        QVERIFY2(IconChoice::save(vault.get(), false, true, &error), qPrintable(error));
        QCOMPARE(vault->customData().value(IconChoice::downloadKey()), QStringLiteral("false"));
        QCOMPARE(vault->customData().value(IconChoice::duckDuckGoKey()), QStringLiteral("false"));

        // As on another computer: the file, opened again, with the default on.
        const QString path = vault->path();
        vault.reset();
        QString openError;
        std::unique_ptr<KeepassVault> again(KeepassVault::unlock(path, Secret(QStringLiteral("senha")), &openError));
        QVERIFY(again);
        IconChoice choice = IconChoice::load(again.get(), true, true);
        QVERIFY(choice.chosen);
        QVERIFY(!choice.download);

        QVERIFY(IconChoice::save(again.get(), true, true, &error));
        choice = IconChoice::load(again.get(), false, false);
        QVERIFY(choice.chosen && choice.download && choice.duckDuckGo);
    }

    // Any other vault keeps its choice on this computer, one per vault.
    void iconChoiceIsKeptLocallyForTheRest() {
        const LocalVault store("/home/ana/.password-store");
        const LocalVault other("/home/ana/outro-store");
        QVERIFY(!IconChoice::load(&store, false, false).chosen);

        QString error;
        QVERIFY(IconChoice::save(&store, true, false, &error));
        IconChoice choice = IconChoice::load(&store, false, false);
        QVERIFY(choice.chosen && choice.download && !choice.duckDuckGo);
        QVERIFY(!IconChoice::load(&other, false, false).chosen);

        QVERIFY(!QSettings().allKeys().join(' ').contains("password-store"));
    }
};

OMAPASS_TEST(TestIconChoice)
#include "tst_iconchoice.moc"
