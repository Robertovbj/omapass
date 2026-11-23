// The NaCl box the browser extension speaks, against libsodium's own output:
// the key pairs are the ones from "Cryptography in NaCl" (Alice and Bob), and
// the ciphertext is what libsodium 1.0.22's crypto_box_easy gave for them.

#include "support.h"

#include "browsercrypto.h"

namespace {

Secret hexSecret(const char *hex) {
    return Secret(QByteArray::fromHex(hex));
}

const char aliceSecret[] = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
const char alicePublic[] = "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
const char bobSecret[] = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
const char bobPublic[] = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
const char nonce[] = "69696ee955b62b73cd62bda875fc73d68219e0036b7a0b37";
const char message[] = "{\"action\":\"get-databasehash\"}";
const char sealedByLibsodium[] =
    "828675cf38e2da824dddfab88580b6c74bbc053900808fc82fb861cbbc6357d17b6f8aef3b5c38186d7a547c28";

}

class TestBrowserCrypto : public QObject {
    Q_OBJECT

private slots:
    void browserCryptoDerivesThePaperKeys() {
        QCOMPARE(BrowserCrypto::publicKeyOf(hexSecret(aliceSecret)).toHex(), QByteArray(alicePublic));
        QCOMPARE(BrowserCrypto::publicKeyOf(hexSecret(bobSecret)).toHex(), QByteArray(bobPublic));
    }

    void browserCryptoSealsLikeLibsodium() {
        const QByteArray sealed = BrowserCrypto::seal(QByteArray(message), QByteArray::fromHex(nonce),
                                                      QByteArray::fromHex(bobPublic),
                                                      hexSecret(aliceSecret));
        QCOMPARE(sealed.toHex(), QByteArray(sealedByLibsodium));
    }

    void browserCryptoOpensWhatLibsodiumSealed() {
        QByteArray plain;
        QVERIFY(BrowserCrypto::open(QByteArray::fromHex(sealedByLibsodium), QByteArray::fromHex(nonce),
                                    QByteArray::fromHex(alicePublic), hexSecret(bobSecret), &plain));
        QCOMPARE(plain, QByteArray(message));
    }

    void browserCryptoRefusesATamperedMessage() {
        QByteArray sealed = QByteArray::fromHex(sealedByLibsodium);
        sealed[20] = char(sealed.at(20) ^ 1);
        QByteArray plain("untouched");
        QVERIFY(!BrowserCrypto::open(sealed, QByteArray::fromHex(nonce), QByteArray::fromHex(alicePublic),
                                     hexSecret(bobSecret), &plain));
        QCOMPARE(plain, QByteArray("untouched"));

        // Nor one opened with the wrong key, or too short to carry a MAC.
        QVERIFY(!BrowserCrypto::open(QByteArray::fromHex(sealedByLibsodium), QByteArray::fromHex(nonce),
                                     QByteArray::fromHex(bobPublic), hexSecret(bobSecret), &plain));
        QVERIFY(!BrowserCrypto::open(QByteArray(10, 'x'), QByteArray::fromHex(nonce),
                                     QByteArray::fromHex(alicePublic), hexSecret(bobSecret), &plain));
    }

    void browserCryptoRefusesBadSizes() {
        QVERIFY(BrowserCrypto::seal("x", QByteArray(23, 'n'), QByteArray::fromHex(bobPublic),
                                    hexSecret(aliceSecret)).isEmpty());
        QVERIFY(BrowserCrypto::seal("x", QByteArray::fromHex(nonce), QByteArray(31, 'k'),
                                    hexSecret(aliceSecret)).isEmpty());
    }

    void browserCryptoRoundTripsWithFreshKeys() {
        const auto browser = BrowserCrypto::generateKeyPair();
        const auto host = BrowserCrypto::generateKeyPair();
        QCOMPARE(browser.publicKey.size(), BrowserCrypto::keyBytes);
        QCOMPARE(BrowserCrypto::publicKeyOf(browser.secretKey), browser.publicKey);
        QVERIFY(browser.publicKey != host.publicKey);

        const QByteArray n = BrowserCrypto::randomBytes(BrowserCrypto::nonceBytes);
        const QByteArray sealed = BrowserCrypto::seal("olá", n, host.publicKey, browser.secretKey);
        QByteArray plain;
        QVERIFY(BrowserCrypto::open(sealed, n, browser.publicKey, host.secretKey, &plain));
        QCOMPARE(QString::fromUtf8(plain), QStringLiteral("olá"));
    }

    // sodium_increment: little-endian, carrying into the next byte.
    void browserCryptoIncrementsTheNonce() {
        QCOMPARE(BrowserCrypto::incremented(QByteArray::fromHex(nonce)).toHex(),
                 QByteArray("6a696ee955b62b73cd62bda875fc73d68219e0036b7a0b37"));
        QByteArray carry(24, char(0xff));
        carry[23] = 0;
        QCOMPARE(BrowserCrypto::incremented(carry).toHex(),
                 QByteArray("000000000000000000000000000000000000000000000001"));
    }
};

OMAPASS_TEST(TestBrowserCrypto)
#include "tst_browsercrypto.moc"
