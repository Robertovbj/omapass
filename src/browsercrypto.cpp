#include "browsercrypto.h"

#include <botan/sodium.h>

namespace Sodium = Botan::Sodium;

namespace {

const uint8_t *bytesOf(const QByteArray &data) {
    return reinterpret_cast<const uint8_t *>(data.constData());
}

}

namespace BrowserCrypto {

KeyPair generateKeyPair() {
    QByteArray publicKey(keyBytes, '\0');
    QByteArray secretKey(keyBytes, '\0');
    Sodium::crypto_box_keypair(reinterpret_cast<uint8_t *>(publicKey.data()),
                               reinterpret_cast<uint8_t *>(secretKey.data()));
    KeyPair pair{publicKey, Secret(secretKey)};
    Sodium::sodium_memzero(secretKey.data(), size_t(secretKey.size()));
    return pair;
}

QByteArray publicKeyOf(const Secret &secretKey) {
    if (secretKey.length() != keyBytes)
        return QByteArray();
    QByteArray publicKey(keyBytes, '\0');
    Sodium::crypto_scalarmult_base(reinterpret_cast<uint8_t *>(publicKey.data()),
                                   bytesOf(secretKey.bytes()));
    return publicKey;
}

QByteArray seal(const QByteArray &plain, const QByteArray &nonce, const QByteArray &theirPublic,
                const Secret &ourSecret) {
    if (nonce.size() != nonceBytes || theirPublic.size() != keyBytes
        || ourSecret.length() != keyBytes)
        return QByteArray();

    QByteArray sealed(plain.size() + macBytes, '\0');
    if (Sodium::crypto_box_easy(reinterpret_cast<uint8_t *>(sealed.data()), bytesOf(plain),
                                size_t(plain.size()), bytesOf(nonce), bytesOf(theirPublic),
                                bytesOf(ourSecret.bytes()))
        != 0)
        return QByteArray();
    return sealed;
}

bool open(const QByteArray &sealed, const QByteArray &nonce, const QByteArray &theirPublic,
          const Secret &ourSecret, QByteArray *plain) {
    if (nonce.size() != nonceBytes || theirPublic.size() != keyBytes
        || ourSecret.length() != keyBytes || sealed.size() < macBytes)
        return false;

    QByteArray opened(sealed.size() - macBytes, '\0');
    if (Sodium::crypto_box_open_easy(reinterpret_cast<uint8_t *>(opened.data()), bytesOf(sealed),
                                     size_t(sealed.size()), bytesOf(nonce), bytesOf(theirPublic),
                                     bytesOf(ourSecret.bytes()))
        != 0)
        return false;
    *plain = opened;
    return true;
}

QByteArray incremented(const QByteArray &nonce) {
    QByteArray next = nonce;
    next.detach();
    Sodium::sodium_increment(reinterpret_cast<uint8_t *>(next.data()), size_t(next.size()));
    return next;
}

QByteArray randomBytes(int count) {
    QByteArray bytes(count, '\0');
    Sodium::randombytes_buf(bytes.data(), size_t(count));
    return bytes;
}

}
