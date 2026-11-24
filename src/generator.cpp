#include "generator.h"

#include "config.h"
#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <botan/system_rng.h>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace {

// Set by configure(); the defaults are what the tests and a bare run see.
QString configuredWordlist = QStringLiteral("auto");
QString interfaceLanguage = QStringLiteral("en");

constexpr int minLength = 4;
constexpr int maxLength = 128;
constexpr int minWords = 3;
constexpr int maxWords = 16;

// What each class draws from. The six symbol groups are KeePassXC's and
// split the printable ASCII punctuation between them; "similar" are the
// characters that are easy to take for one another.
const QString lowerSet = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
const QString upperSet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
const QString numberSet = QStringLiteral("0123456789");
const QString logogramSet = QStringLiteral("#$%&@^`~");
const QString punctuationSet = QStringLiteral(".,:;");
const QString quoteSet = QStringLiteral("\"'");
const QString dashSet = QStringLiteral("\\/|_-");
const QString mathSet = QStringLiteral("<>*+!?=");
const QString braceSet = QStringLiteral("{[()]}");
const QString similarSet = QStringLiteral("iIl1|Lo0O");

// Latin-1 from ¡ (U+00A1) to ÿ (U+00FF), less the soft hyphen, which does
// not show: what KeePassXC calls extended ASCII.
QString extendedAsciiSet() {
    QString set;
    for (ushort code = 0xA1; code <= 0xFF; ++code) {
        if (code != 0xAD)
            set.append(QChar(code));
    }
    return set;
}

// Each character once, in the order first seen: a set typed by hand may
// repeat one, which would make it likelier than the rest.
QString unique(const QString &text) {
    QString out;
    for (const QChar c : text) {
        if (!out.contains(c))
            out.append(c);
    }
    return out;
}

// A uniform number in [0, bound), without modulo bias.
quint32 randomBelow(quint32 bound) {
    if (bound <= 1)
        return 0;
    const quint32 limit = std::numeric_limits<quint32>::max() - (std::numeric_limits<quint32>::max() % bound);
    for (;;) {
        quint32 value = 0;
        Botan::system_rng().randomize(reinterpret_cast<uint8_t *>(&value), sizeof(value));
        if (value < limit)
            return value % bound;
    }
}

QString withoutChars(const QString &set, const QString &removed) {
    QString out;
    for (const QChar c : set) {
        if (!removed.contains(c))
            out.append(c);
    }
    return out;
}

QStringList readWords(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList words;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        // A list with dice numbers ("11111<TAB>abacus") keeps the word.
        const int tab = line.lastIndexOf(QLatin1Char('\t'));
        if (tab >= 0)
            line = line.mid(tab + 1).trimmed();
        if (!line.isEmpty())
            words.append(line);
    }
    return words;
}

}

namespace Generator {

GeneratorOptions normalize(GeneratorOptions options) {
    options.length = qBound(minLength, options.length, maxLength);
    options.words = qBound(minWords, options.words, maxWords);

    // A custom set replaces the classes, so it is the one thing that has to
    // be there when it is used.
    if (!options.custom.isEmpty())
        return options;

    // Every class off would leave nothing to draw from.
    if (!options.lower && !options.upper && !options.numbers && !options.logograms
        && !options.punctuation && !options.quotes && !options.dashes && !options.math
        && !options.braces && !options.extendedAscii && options.extra.isEmpty())
        options.lower = true;
    return options;
}

QStringList characterGroups(const GeneratorOptions &options) {
    QString removed = options.exclude;
    if (options.excludeSimilar)
        removed += similarSet;

    QStringList groups;
    if (!options.custom.isEmpty()) {
        groups << withoutChars(unique(options.custom), removed);
    } else {
        const std::pair<bool, QString> classes[] = {
            {options.lower, lowerSet},
            {options.upper, upperSet},
            {options.numbers, numberSet},
            {options.logograms, logogramSet},
            {options.punctuation, punctuationSet},
            {options.quotes, quoteSet},
            {options.dashes, dashSet},
            {options.math, mathSet},
            {options.braces, braceSet},
            {options.extendedAscii, extendedAsciiSet()},
        };
        QString taken;
        for (const auto &[on, set] : classes) {
            if (on) {
                groups << withoutChars(set, removed);
                taken += set;
            }
        }
        // The extra characters a class already has would count twice.
        groups << withoutChars(unique(options.extra), removed + taken);
    }
    groups.removeAll(QString());
    return groups;
}

double entropyBits(const GeneratorOptions &options) {
    const GeneratorOptions bounded = normalize(options);
    if (bounded.passphrase) {
        const QString wordlist = wordlistPath();
        const qsizetype size = wordlist.isEmpty() ? 0 : readWords(wordlist).size();
        return size > 1 ? bounded.words * std::log2(double(size)) : 0;
    }

    qsizetype pool = 0;
    for (const QString &group : characterGroups(bounded))
        pool += group.size();
    return pool > 1 ? bounded.length * std::log2(double(pool)) : 0;
}

QString quality(double bits) {
    if (bits < 40)
        return QStringLiteral("poor");
    if (bits < 75)
        return QStringLiteral("weak");
    if (bits < 100)
        return QStringLiteral("good");
    return QStringLiteral("excellent");
}

void configure(const QString &configured, const QString &language) {
    configuredWordlist = configured.isEmpty() ? QStringLiteral("auto") : configured;
    interfaceLanguage = language;
}

QStringList wordlistNames(const QString &configured, const QString &language) {
    // A path is used as given, with no fallback: naming a file that is not
    // there is a mistake worth noticing, not something to paper over.
    if (configured.contains(QLatin1Char('/')))
        return {configured};

    const QString english = QStringLiteral("eff_large.wordlist");
    if (configured == QLatin1String("en"))
        return {english};
    if (!configured.isEmpty() && configured != QLatin1String("auto"))
        return {configured + QStringLiteral(".wordlist"), english};

    // "auto": the interface language first, English as the fallback — the
    // language is a tag like "pt-BR", which is the list's own name.
    QStringList names;
    if (!language.isEmpty() && !language.startsWith(QLatin1String("en")))
        names << language + QStringLiteral(".wordlist");
    names << english;
    return names;
}

QStringList wordlistDirectories() {
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList directories{
        // Lists the user brought in through the settings sheet.
        Config::configDir() + QStringLiteral("/wordlists"),
#ifdef Q_OS_MACOS
        QDir::cleanPath(appDir + QStringLiteral("/../Resources/wordlists")),
#endif
        // Installed beside omapass (PREFIX/share/omapass/…), wherever that is.
        appDir + QStringLiteral("/../share/omapass/wordlists"),
        // Beside the executable (the Windows package, which has no
        // PREFIX/share to speak of).
        appDir + QStringLiteral("/wordlists"),
        // Running from build/: the lists in the repository.
        appDir + QStringLiteral("/../wordlists"),
    };

    const QStringList data = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &directory : data)
        directories << directory + QStringLiteral("/wordlists");
    return directories;
}

QString wordlistPath() {
    const QStringList names = wordlistNames(configuredWordlist, interfaceLanguage);
    for (const QString &name : names) {
        if (name.contains(QLatin1Char('/')))
            return QFile::exists(name) ? name : QString();

        for (const QString &directory : wordlistDirectories()) {
            const QString candidate = directory + QLatin1Char('/') + name;
            if (QFile::exists(candidate))
                return candidate;
        }
    }
    return QString();
}

bool generate(const GeneratorOptions &options, Secret *password, QString *error) {
    const GeneratorOptions bounded = normalize(options);

    try {
        if (bounded.passphrase) {
            const QString wordlist = wordlistPath();
            if (wordlist.isEmpty()) {
                *error = I18n::t(QStringLiteral("generator.no_wordlist"));
                return false;
            }
            const QStringList words = readWords(wordlist);
            if (words.size() < minimumWordlistSize) {
                *error = I18n::t(QStringLiteral("generator.error"));
                return false;
            }

            QStringList chosen;
            for (int i = 0; i < bounded.words; ++i)
                chosen.append(words.at(int(randomBelow(quint32(words.size())))));
            QString phrase = chosen.join(bounded.separator);
            *password = Secret(phrase);
            phrase.fill(QChar(0));
            return true;
        }

        // The classes in use, each with the characters left after the
        // exclusions; a custom set stands alone.
        const QStringList groups = characterGroups(bounded);
        if (groups.isEmpty()) {
            *error = I18n::t(QStringLiteral("generator.error"));
            return false;
        }

        QString pool;
        for (const QString &group : std::as_const(groups))
            pool += group;

        // With everyGroup, one from every group first, so the result meets
        // what was asked for; the rest from the whole pool; then shuffled so
        // the guaranteed ones do not sit at the front.
        std::vector<QChar> picked;
        if (bounded.everyGroup) {
            for (const QString &group : groups) {
                if (int(picked.size()) < bounded.length)
                    picked.push_back(group.at(int(randomBelow(quint32(group.size())))));
            }
        }
        while (int(picked.size()) < bounded.length)
            picked.push_back(pool.at(int(randomBelow(quint32(pool.size())))));
        for (size_t i = picked.size(); i > 1; --i)
            std::swap(picked[i - 1], picked[randomBelow(quint32(i))]);

        QString generated;
        generated.reserve(int(picked.size()));
        for (const QChar c : picked)
            generated.append(c);
        *password = Secret(generated);
        generated.fill(QChar(0));
        return true;
    } catch (const std::exception &) {
        *error = I18n::t(QStringLiteral("generator.error"));
        return false;
    }
}

}
