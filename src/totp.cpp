#include "totp.h"

#include <QUrl>
#include <QUrlQuery>

#include <botan/mac.h>

#include "i18n.h"

namespace {

const QString base32Alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
// Steam Guard's codes: five characters from an alphabet without the ones
// that read alike (0/O, 1/I, A/4…).
const char steamAlphabet[] = "23456789BCDFGHJKMNPQRTVWXY";

std::optional<TotpParams> fail(QString *error, const char *key) {
    if (error)
        *error = I18n::t(QString::fromLatin1(key));
    return std::nullopt;
}

// "SHA1", "sha-256", "SHA512" → the spelling TotpParams keeps; empty when it
// is none of the three RFC 6238 allows.
QString normalizeAlgorithm(const QString &text) {
    const QString name = text.trimmed().toUpper().remove(QLatin1Char('-'));
    if (name == QLatin1String("SHA1") || name == QLatin1String("SHA256")
        || name == QLatin1String("SHA512"))
        return name;
    return QString();
}

std::optional<TotpParams> fromSecret(const QString &secret, QString *error) {
    std::optional<QByteArray> key = Totp::base32Decode(secret);
    if (!key || key->isEmpty())
        return fail(error, "totp.invalid_secret");
    TotpParams params;
    params.key = Secret(*key);
    key->fill('\0');
    return params;
}

std::optional<TotpParams> fromUri(const QString &text, QString *error) {
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid())
        return fail(error, "totp.invalid_uri");
    if (url.host().compare(QLatin1String("totp"), Qt::CaseInsensitive) != 0)
        return fail(error, "totp.not_totp");

    const QUrlQuery query(url);
    const auto value = [&query](const char *name) {
        return query.queryItemValue(QString::fromLatin1(name), QUrl::FullyDecoded);
    };

    std::optional<TotpParams> params = fromSecret(value("secret"), error);
    if (!params)
        return std::nullopt;

    // The label is "Issuer:account" or just "account"; an issuer parameter,
    // where there is one, is the authority on the issuer.
    QString label = url.path(QUrl::FullyDecoded);
    if (label.startsWith(QLatin1Char('/')))
        label.remove(0, 1);
    const qsizetype colon = label.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        params->issuer = label.left(colon).trimmed();
        params->account = label.mid(colon + 1).trimmed();
    } else {
        params->account = label.trimmed();
    }
    if (query.hasQueryItem(QStringLiteral("issuer")))
        params->issuer = value("issuer").trimmed();

    if (query.hasQueryItem(QStringLiteral("algorithm"))) {
        params->algorithm = normalizeAlgorithm(value("algorithm"));
        if (params->algorithm.isEmpty())
            return fail(error, "totp.invalid_parameter");
    }

    bool ok = true;
    if (query.hasQueryItem(QStringLiteral("digits"))) {
        params->digits = value("digits").toInt(&ok);
        if (!ok || params->digits < 5 || params->digits > 10)
            return fail(error, "totp.invalid_parameter");
    }
    if (query.hasQueryItem(QStringLiteral("period"))) {
        params->period = value("period").toInt(&ok);
        if (!ok || params->period < 1)
            return fail(error, "totp.invalid_parameter");
    }

    // KeePassXC marks Steam codes with an encoder parameter.
    if (value("encoder").compare(QLatin1String("steam"), Qt::CaseInsensitive) == 0) {
        params->steam = true;
        params->digits = 5;
    }
    return params;
}

// RFC 4226's dynamic truncation of HMAC(key, counter): 31 bits of the MAC.
quint32 truncatedHmac(const TotpParams &params, quint64 counter) {
    const QString name = QStringLiteral("HMAC(SHA-%1)")
                             .arg(params.algorithm == QLatin1String("SHA1") ? QStringLiteral("1")
                                                                            : params.algorithm.mid(3));
    const auto hmac = Botan::MessageAuthenticationCode::create_or_throw(name.toStdString());
    hmac->set_key(reinterpret_cast<const uint8_t *>(params.key.bytes().constData()),
                  static_cast<size_t>(params.key.length()));

    uint8_t message[8];
    for (int i = 7; i >= 0; --i) {
        message[i] = static_cast<uint8_t>(counter & 0xff);
        counter >>= 8;
    }
    hmac->update(message, sizeof(message));
    const auto mac = hmac->final();

    const size_t offset = mac[mac.size() - 1] & 0x0f;
    return (quint32(mac[offset] & 0x7f) << 24) | (quint32(mac[offset + 1]) << 16)
           | (quint32(mac[offset + 2]) << 8) | quint32(mac[offset + 3]);
}

}

namespace Totp {

std::optional<TotpParams> parse(const QString &text, QString *error) {
    const QString value = text.trimmed();
    if (value.isEmpty())
        return fail(error, "totp.invalid_secret");

    if (value.startsWith(QLatin1String("otpauth:"), Qt::CaseInsensitive))
        return fromUri(value, error);

    if (value.startsWith(QLatin1String("steam://"), Qt::CaseInsensitive)) {
        std::optional<TotpParams> params = fromSecret(value.mid(8), error);
        if (params) {
            params->steam = true;
            params->digits = 5;
        }
        return params;
    }

    return fromSecret(value, error);
}

std::optional<TotpParams> fromKeepassLegacy(const QString &seed, const QString &settings) {
    std::optional<TotpParams> params = fromSecret(seed, nullptr);
    if (!params)
        return std::nullopt;

    const QStringList parts = settings.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    bool ok = true;
    if (parts.size() >= 1) {
        params->period = parts.at(0).trimmed().toInt(&ok);
        if (!ok || params->period < 1)
            return std::nullopt;
    }
    if (parts.size() >= 2) {
        const QString digits = parts.at(1).trimmed();
        if (digits.compare(QLatin1String("S"), Qt::CaseInsensitive) == 0) {
            params->steam = true;
            params->digits = 5;
        } else {
            params->digits = digits.toInt(&ok);
            if (!ok || params->digits < 5 || params->digits > 10)
                return std::nullopt;
        }
    }
    return params;
}

QString code(const TotpParams &params, qint64 unixTime) {
    const quint64 counter = quint64(unixTime < 0 ? 0 : unixTime) / quint64(params.period);
    quint32 value = truncatedHmac(params, counter);

    if (params.steam) {
        QString out;
        for (int i = 0; i < 5; ++i) {
            out.append(QLatin1Char(steamAlphabet[value % 26]));
            value /= 26;
        }
        return out;
    }

    quint64 modulus = 1;
    for (int i = 0; i < params.digits; ++i)
        modulus *= 10;
    return QString::number(quint64(value) % modulus).rightJustified(params.digits, QLatin1Char('0'));
}

int secondsLeft(const TotpParams &params, qint64 unixTime) {
    const qint64 now = unixTime < 0 ? 0 : unixTime;
    return params.period - int(now % params.period);
}

QString toUri(const TotpParams &params, const QString &label) {
    QString name = params.account.isEmpty() ? label : params.account;
    if (!params.issuer.isEmpty())
        name = params.issuer + QLatin1Char(':') + name;

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("secret"), base32Encode(params.key.bytes()));
    query.addQueryItem(QStringLiteral("period"), QString::number(params.period));
    query.addQueryItem(QStringLiteral("digits"), QString::number(params.digits));
    if (params.algorithm != QLatin1String("SHA1"))
        query.addQueryItem(QStringLiteral("algorithm"), params.algorithm);
    if (!params.issuer.isEmpty())
        query.addQueryItem(QStringLiteral("issuer"), params.issuer);
    if (params.steam)
        query.addQueryItem(QStringLiteral("encoder"), QStringLiteral("steam"));

    return QStringLiteral("otpauth://totp/")
           + QString::fromLatin1(QUrl::toPercentEncoding(name, ":@"))
           + QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
}

std::optional<QByteArray> base32Decode(const QString &text) {
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    for (const QChar c : text) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('='))
            continue;
        const qsizetype index = base32Alphabet.indexOf(c.toUpper());
        if (index < 0)
            return std::nullopt;
        buffer = (buffer << 5) | quint32(index);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((buffer >> bits) & 0xff));
        }
    }
    buffer = 0;
    return out;
}

QString base32Encode(const QByteArray &bytes) {
    QString out;
    quint32 buffer = 0;
    int bits = 0;
    for (const char byte : bytes) {
        buffer = (buffer << 8) | quint8(byte);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.append(base32Alphabet.at((buffer >> bits) & 0x1f));
        }
    }
    if (bits > 0)
        out.append(base32Alphabet.at((buffer << (5 - bits)) & 0x1f));
    return out;
}

}
