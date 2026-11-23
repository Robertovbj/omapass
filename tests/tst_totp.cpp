// One-time passwords: the RFC 6238 test vectors, and the shapes each
// backend stores a secret in.

#include "support.h"

#include "totp.h"

class TestTotp : public QObject {
    Q_OBJECT

    static TotpParams rfcParams(const char *algorithm, const QByteArray &key) {
        TotpParams params;
        params.key = Secret(key);
        params.algorithm = QString::fromLatin1(algorithm);
        params.digits = 8;
        return params;
    }

private slots:
    // RFC 6238, appendix B: the seeds are the ASCII digits repeated to the
    // hash's block size, eight digits, thirty-second steps.
    void totpMatchesTheRfcVectors_data() {
        QTest::addColumn<qint64>("time");
        QTest::addColumn<QString>("sha1");
        QTest::addColumn<QString>("sha256");
        QTest::addColumn<QString>("sha512");

        QTest::newRow("59") << qint64(59) << "94287082" << "46119246" << "90693936";
        QTest::newRow("1111111109") << qint64(1111111109) << "07081804" << "68084774" << "25091201";
        QTest::newRow("1111111111") << qint64(1111111111) << "14050471" << "67062674" << "99943326";
        QTest::newRow("1234567890") << qint64(1234567890) << "89005924" << "91819424" << "93441116";
        QTest::newRow("2000000000") << qint64(2000000000) << "69279037" << "90698825" << "38618901";
        QTest::newRow("20000000000") << qint64(20000000000) << "65353130" << "77737706" << "47863826";
    }

    void totpMatchesTheRfcVectors() {
        QFETCH(qint64, time);
        QFETCH(QString, sha1);
        QFETCH(QString, sha256);
        QFETCH(QString, sha512);

        const QByteArray seed20("12345678901234567890");
        const QByteArray seed32 = seed20 + "123456789012";
        const QByteArray seed64 = seed20 + seed20 + seed20 + "1234";

        QCOMPARE(Totp::code(rfcParams("SHA1", seed20), time), sha1);
        QCOMPARE(Totp::code(rfcParams("SHA256", seed32), time), sha256);
        QCOMPARE(Totp::code(rfcParams("SHA512", seed64), time), sha512);
    }

    // The same vector reached through the URI a user would paste.
    void totpReadsAnOtpauthUri() {
        const QString secret = Totp::base32Encode("12345678901234567890");
        const auto params = Totp::parse(
            QStringLiteral("otpauth://totp/Example:alice@example.com?secret=%1&issuer=Example"
                           "&algorithm=SHA1&digits=8&period=30").arg(secret));
        QVERIFY(params);
        QCOMPARE(params->issuer, QStringLiteral("Example"));
        QCOMPARE(params->account, QStringLiteral("alice@example.com"));
        QCOMPARE(Totp::code(*params, 59), QStringLiteral("94287082"));
    }

    void totpDefaultsToSixDigitsAndThirtySeconds() {
        const auto params = Totp::parse(QStringLiteral("JBSWY3DPEHPK3PXP"));
        QVERIFY(params);
        QCOMPARE(params->digits, 6);
        QCOMPARE(params->period, 30);
        QCOMPARE(Totp::code(*params, 0).size(), 6);
        QCOMPARE(Totp::secondsLeft(*params, 0), 30);
        QCOMPARE(Totp::secondsLeft(*params, 29), 1);
        QCOMPARE(Totp::secondsLeft(*params, 30), 30);
    }

    // Setup pages print the secret in lower-case groups; that is what gets
    // pasted.
    void totpAcceptsAGroupedLowerCaseSecret() {
        const auto spaced = Totp::parse(QStringLiteral(" jbsw y3dp ehpk 3pxp "));
        const auto plain = Totp::parse(QStringLiteral("JBSWY3DPEHPK3PXP"));
        QVERIFY(spaced && plain);
        QCOMPARE(spaced->key.bytes(), plain->key.bytes());
        QCOMPARE(spaced->key.bytes(), QByteArray("Hello!\xde\xad\xbe\xef"));
    }

    void totpRejectsWhatIsNotATotpSecret() {
        QString error;
        QVERIFY(!Totp::parse(QStringLiteral("not base32!"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Totp::parse(QString()));
        QVERIFY(!Totp::parse(QStringLiteral("otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP&counter=1")));
        QVERIFY(!Totp::parse(QStringLiteral("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&algorithm=MD5")));
        QVERIFY(!Totp::parse(QStringLiteral("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=3")));
        QVERIFY(!Totp::parse(QStringLiteral("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&period=0")));
    }

    // What omapass writes back reads as the same thing, special characters
    // in the issuer included.
    void totpUriRoundTrips() {
        auto params = Totp::parse(QStringLiteral("JBSWY3DPEHPK3PXP"));
        QVERIFY(params);
        params->issuer = QStringLiteral("Acme & Co");
        params->algorithm = QStringLiteral("SHA256");
        params->digits = 8;
        params->period = 60;

        const QString uri = Totp::toUri(*params, QStringLiteral("alice@example.com"));
        QVERIFY2(uri.startsWith(QLatin1String("otpauth://totp/")), qPrintable(uri));

        const auto back = Totp::parse(uri);
        QVERIFY2(back, qPrintable(uri));
        QCOMPARE(back->key.bytes(), params->key.bytes());
        QCOMPARE(back->issuer, QStringLiteral("Acme & Co"));
        QCOMPARE(back->account, QStringLiteral("alice@example.com"));
        QCOMPARE(back->algorithm, QStringLiteral("SHA256"));
        QCOMPARE(back->digits, 8);
        QCOMPARE(back->period, 60);
        QCOMPARE(Totp::code(*back, 1234567890), Totp::code(*params, 1234567890));
    }

    // Bitwarden's steam://, KeePassXC's encoder=steam and KeePass 2's
    // "30;S" are the same code: five characters of Steam's alphabet.
    void totpReadsTheSteamSpellings() {
        const auto bitwarden = Totp::parse(QStringLiteral("steam://JBSWY3DPEHPK3PXP"));
        const auto keepassxc = Totp::parse(
            QStringLiteral("otpauth://totp/Steam:alice?secret=JBSWY3DPEHPK3PXP&encoder=steam"));
        const auto legacy = Totp::fromKeepassLegacy(QStringLiteral("JBSWY3DPEHPK3PXP"),
                                                    QStringLiteral("30;S"));
        QVERIFY(bitwarden && keepassxc && legacy);

        const QString code = Totp::code(*bitwarden, 1234567890);
        QCOMPARE(code.size(), 5);
        for (const QChar c : code)
            QVERIFY2(QStringLiteral("23456789BCDFGHJKMNPQRTVWXY").contains(c), qPrintable(code));
        QCOMPARE(Totp::code(*keepassxc, 1234567890), code);
        QCOMPARE(Totp::code(*legacy, 1234567890), code);

        const auto back = Totp::parse(Totp::toUri(*bitwarden, QStringLiteral("alice")));
        QVERIFY(back && back->steam);
    }

    void totpReadsKeepassLegacySettings() {
        const QString seed = Totp::base32Encode("12345678901234567890");
        const auto params = Totp::fromKeepassLegacy(seed, QStringLiteral("30;8"));
        QVERIFY(params);
        QCOMPARE(Totp::code(*params, 59), QStringLiteral("94287082"));

        const auto defaults = Totp::fromKeepassLegacy(seed, QString());
        QVERIFY(defaults);
        QCOMPARE(defaults->digits, 6);
        QCOMPARE(defaults->period, 30);

        QVERIFY(!Totp::fromKeepassLegacy(QStringLiteral("!!"), QString()));
        QVERIFY(!Totp::fromKeepassLegacy(seed, QStringLiteral("x;6")));
    }

    void base32RoundTrips() {
        for (const QByteArray &bytes : {QByteArray(), QByteArray("f"), QByteArray("fo"),
                                        QByteArray("foo"), QByteArray("foob"), QByteArray("fooba"),
                                        QByteArray("foobar")}) {
            const auto decoded = Totp::base32Decode(Totp::base32Encode(bytes));
            QVERIFY(decoded);
            QCOMPARE(*decoded, bytes);
        }
        // RFC 4648's own vector, padding and all.
        QCOMPARE(*Totp::base32Decode(QStringLiteral("MZXW6YTBOI======")), QByteArray("foobar"));
        QCOMPARE(Totp::base32Encode("foobar"), QStringLiteral("MZXW6YTBOI"));
    }
};

OMAPASS_TEST(TestTotp)
#include "tst_totp.moc"
