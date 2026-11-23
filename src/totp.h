#pragma once

#include <QString>

#include <optional>

#include "secret.h"

// Time-based one-time passwords (RFC 6238), as the four backends store them.
//
// What a backend holds is one of:
// - an `otpauth://totp/...` URI (KeePassXC's `otp` attribute, pass-otp,
//   1Password, and Bitwarden when the item was set up from a QR code);
// - a bare base32 secret (Bitwarden, 1Password, or what the user pastes);
// - `steam://SECRET` (Bitwarden's spelling for Steam Guard codes).
//
// Everything is parsed into TotpParams once and the code is computed from
// that; what gets written back is always an otpauth URI, the one form every
// backend reads.
struct TotpParams {
    Secret key;                                // the decoded secret
    QString algorithm = QStringLiteral("SHA1"); // SHA1, SHA256 or SHA512
    int digits = 6;
    int period = 30;
    bool steam = false;                        // five characters of Steam's alphabet
    QString issuer;
    QString account;
};

namespace Totp {

// Reads a stored or pasted value. On failure returns nothing and, when
// `error` is given, says why in the user's language.
std::optional<TotpParams> parse(const QString &text, QString *error = nullptr);

// KeePass 2's older pair of attributes — `TOTP Seed` (base32) and
// `TOTP Settings` ("30;6", or "30;S" for Steam) — which KeePassXC still
// reads. Empty settings means the defaults.
std::optional<TotpParams> fromKeepassLegacy(const QString &seed, const QString &settings);

// The code at `unixTime` (seconds), zero-padded to the number of digits.
QString code(const TotpParams &params, qint64 unixTime);

// Seconds until the code at `unixTime` changes, 1 to the period.
int secondsLeft(const TotpParams &params, qint64 unixTime);

// The canonical otpauth URI for `params`. `label` names the account in an
// authenticator app when the params carry none (the entry title, say).
QString toUri(const TotpParams &params, const QString &label = QString());

// What an entry form's OTP field saves. `original` is what the entry held
// when the form opened: left untouched (whitespace aside), it goes back
// exactly as it was, in whatever form its backend keeps it. Anything else is
// checked and written as an otpauth URI named `label`. Empty clears it;
// nothing, with `error` set, when what was typed is not a TOTP secret.
std::optional<QString> forSaving(const QString &typed, const QString &original,
                                 const QString &label, QString *error = nullptr);

// Base32 as RFC 4648 writes it, read leniently: any case, spaces, hyphens
// and padding are ignored (authenticator setup pages print the secret in
// groups). Nothing on invalid input.
std::optional<QByteArray> base32Decode(const QString &text);
QString base32Encode(const QByteArray &bytes);

}
