// The password and passphrase generator.

#include "support.h"

#include "generator.h"
#include "config.h"

#include <algorithm>
#include <cmath>

class TestGenerator : public QObject {
    Q_OBJECT

private slots:
    void generatorHonoursClassesLengthAndExclusions() {
        GeneratorOptions options;
        options.length = 40;
        options.logograms = options.punctuation = options.quotes = false;
        options.dashes = options.math = options.braces = false;
        options.excludeSimilar = true;
        options.exclude = QStringLiteral("aeiou");

        for (int round = 0; round < 20; ++round) {
            Secret generated;
            QString error;
            QVERIFY(Generator::generate(options, &generated, &error));
            const QString text = generated.toString();
            QCOMPARE(text.size(), 40);
            bool lower = false, upper = false, number = false;
            for (const QChar c : text) {
                QVERIFY2(c.isLetterOrNumber(), "no special characters were asked for");
                QVERIFY(!QStringLiteral("aeiouiIl1|Lo0O").contains(c));
                lower |= c.isLower();
                upper |= c.isUpper();
                number |= c.isDigit();
            }
            QVERIFY(lower && upper && number);
        }
    }

    void generatorCustomSetReplacesTheClasses() {
        GeneratorOptions options;
        options.custom = QStringLiteral("ab");
        options.length = 30;

        Secret generated;
        QString error;
        QVERIFY(Generator::generate(options, &generated, &error));
        for (const QChar c : generated.toString())
            QVERIFY(c == QLatin1Char('a') || c == QLatin1Char('b'));
    }

    void generatorPassphraseDrawsWordsFromTheList() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString list = dir.filePath(QStringLiteral("w.wordlist"));
        QFile file(list);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < Generator::minimumWordlistSize; ++i)
            file.write(QByteArray("palavra") + QByteArray::number(i) + "\n");
        file.close();

        Generator::configure(list, QStringLiteral("en"));
        GeneratorOptions options;
        options.passphrase = true;
        options.words = 5;
        options.separator = QStringLiteral("-");

        Secret generated;
        QString error;
        QVERIFY(Generator::generate(options, &generated, &error));
        const QStringList words = generated.toString().split(QLatin1Char('-'));
        QCOMPARE(words.size(), 5);
        for (const QString &word : words)
            QVERIFY(word.startsWith(QStringLiteral("palavra")));
        Generator::configure(QStringLiteral("auto"), QStringLiteral("en"));
    }

    void generatorWordlistFollowsConfigAndLanguage() {
        const QString english = QStringLiteral("eff_large.wordlist");

        // "auto": the interface language first, English behind it.
        QCOMPARE(Generator::wordlistNames(QStringLiteral("auto"), QStringLiteral("pt-BR")),
                 QStringList({QStringLiteral("pt-BR.wordlist"), english}));
        QCOMPARE(Generator::wordlistNames(QStringLiteral("auto"), QStringLiteral("en")),
                 QStringList({english}));

        // A named list still falls back to English; "en" is the English one.
        QCOMPARE(Generator::wordlistNames(QStringLiteral("pt-BR"), QStringLiteral("en")),
                 QStringList({QStringLiteral("pt-BR.wordlist"), english}));
        QCOMPARE(Generator::wordlistNames(QStringLiteral("en"), QStringLiteral("pt-BR")),
                 QStringList({english}));

        // A path is taken as it is, with nothing behind it.
        QCOMPARE(Generator::wordlistNames(QStringLiteral("/tmp/minha.txt"), QStringLiteral("pt-BR")),
                 QStringList({QStringLiteral("/tmp/minha.txt")}));

        QVERIFY(!Generator::wordlistDirectories().isEmpty());
    }

    void generatorOptionsAreBounded() {
        GeneratorOptions options;
        options.length = 2;
        options.words = 99;
        options.lower = options.upper = options.numbers = false;
        options.logograms = options.punctuation = options.quotes = false;
        options.dashes = options.math = options.braces = false;

        const GeneratorOptions bounded = Generator::normalize(options);
        QCOMPARE(bounded.length, 4);
        QCOMPARE(bounded.words, 16);
        // Nothing selected would leave no characters to draw from.
        QVERIFY(bounded.lower);

        GeneratorOptions custom;
        custom.custom = QStringLiteral("xyz");
        custom.lower = custom.upper = custom.numbers = false;
        custom.logograms = custom.punctuation = custom.quotes = false;
        custom.dashes = custom.math = custom.braces = false;
        QVERIFY(!Generator::normalize(custom).lower);

        // Characters to choose from as well are something to draw from too.
        GeneratorOptions extra = bounded;
        extra.lower = false;
        extra.extra = QStringLiteral("xyz");
        QVERIFY(!Generator::normalize(extra).lower);
    }

    // The six symbol groups split the ASCII punctuation between them, as
    // KeePassXC's do: all of them together are the old "special" class.
    void generatorSymbolGroupsCoverThePunctuation() {
        GeneratorOptions options;
        options.lower = options.upper = options.numbers = false;
        const QStringList groups = Generator::characterGroups(options);
        QCOMPARE(groups.size(), 6);
        QString all = groups.join(QString());
        std::sort(all.begin(), all.end());
        QCOMPARE(all, QStringLiteral("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"));

        // Just one group: only its characters come out.
        options.logograms = options.punctuation = options.quotes = options.dashes = false;
        options.math = false;
        Secret generated;
        QString error;
        QVERIFY(Generator::generate(options, &generated, &error));
        for (const QChar c : generated.toString())
            QVERIFY(QStringLiteral("{[()]}").contains(c));
    }

    void generatorExtendedAsciiIsLatin1() {
        GeneratorOptions options;
        options.lower = options.upper = options.numbers = false;
        options.logograms = options.punctuation = options.quotes = false;
        options.dashes = options.math = options.braces = false;
        options.extendedAscii = true;
        options.length = 64;

        const QStringList groups = Generator::characterGroups(options);
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups.first().size(), 94);           // ¡ to ÿ, less the soft hyphen
        QVERIFY(!groups.first().contains(QChar(0xAD)));

        Secret generated;
        QString error;
        QVERIFY(Generator::generate(options, &generated, &error));
        for (const QChar c : generated.toString())
            QVERIFY(c.unicode() >= 0xA1 && c.unicode() <= 0xFF);
    }

    // "Also choose from" adds a group beside the classes, without repeating
    // what a class already has; the custom set replaces them all.
    void generatorExtraCharactersJoinTheClasses() {
        GeneratorOptions options;
        options.upper = options.numbers = false;
        options.logograms = options.punctuation = options.quotes = false;
        options.dashes = options.math = options.braces = false;
        options.extra = QStringLiteral("ÆØÅaa");
        const QStringList groups = Generator::characterGroups(options);
        QCOMPARE(groups, QStringList({QStringLiteral("abcdefghijklmnopqrstuvwxyz"),
                                      QStringLiteral("ÆØÅ")}));

        // With every group required, both show up every time.
        options.length = 4;
        for (int round = 0; round < 20; ++round) {
            Secret generated;
            QString error;
            QVERIFY(Generator::generate(options, &generated, &error));
            const QString text = generated.toString();
            bool lower = false, extra = false;
            for (const QChar c : text) {
                lower |= c.isLower() && c.unicode() < 0x80;
                extra |= QStringLiteral("ÆØÅ").contains(c);
            }
            QVERIFY(lower && extra);
        }
    }

    void generatorEntropyAndQuality() {
        GeneratorOptions options;
        options.length = 20;
        options.upper = options.numbers = false;
        options.logograms = options.punctuation = options.quotes = false;
        options.dashes = options.math = options.braces = false;
        // 26 letters: 20 × log2(26) ≈ 94 bits.
        QVERIFY(qAbs(Generator::entropyBits(options) - 20 * std::log2(26.0)) < 0.001);
        QCOMPARE(Generator::quality(Generator::entropyBits(options)), QStringLiteral("good"));

        options.length = 8;
        QCOMPARE(Generator::quality(Generator::entropyBits(options)), QStringLiteral("poor"));
        QCOMPARE(Generator::quality(60), QStringLiteral("weak"));
        QCOMPARE(Generator::quality(100), QStringLiteral("excellent"));
    }
};

OMAPASS_TEST(TestGenerator)
#include "tst_generator.moc"
