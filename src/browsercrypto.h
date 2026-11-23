#pragma once

#include <QByteArray>

#include "secret.h"

// The NaCl box the KeePassXC-Browser extension encrypts its messages with
// (curve25519-xsalsa20-poly1305, tweetnacl's `nacl.box`), through Botan's
// libsodium-compatible API — the same one KeePassXC itself uses. Kept to this
// one file, so switching to libsodium proper would change nothing else.
//
// Everything here is raw bytes; the protocol speaks base64 on top.
namespace BrowserCrypto {

constexpr int keyBytes = 32;
constexpr int nonceBytes = 24;
constexpr int macBytes = 16;

struct KeyPair {
    QByteArray publicKey;
    Secret secretKey;
};

KeyPair generateKeyPair();

// The public key that goes with a secret one (for the tests, which need a
// fixed pair).
QByteArray publicKeyOf(const Secret &secretKey);

// `plain` sealed for `theirPublic`, or empty when a key or the nonce has the
// wrong size.
QByteArray seal(const QByteArray &plain, const QByteArray &nonce, const QByteArray &theirPublic,
                const Secret &ourSecret);
// The message, or nothing when it does not authenticate.
bool open(const QByteArray &sealed, const QByteArray &nonce, const QByteArray &theirPublic,
          const Secret &ourSecret, QByteArray *plain);

// The nonce plus one, little-endian (sodium_increment): every reply carries
// the request's nonce incremented.
QByteArray incremented(const QByteArray &nonce);

QByteArray randomBytes(int count);

}
