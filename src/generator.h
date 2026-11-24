#pragma once

#include <QString>
#include <QStringList>

#include "secret.h"

// Password and passphrase generation, in-process: fast enough to regenerate
// on every change in the sheet, and the same for every backend (`pass
// generate`, for one, writes an entry instead of just generating one).
// Characters and words are drawn uniformly from the system's random source.

struct GeneratorOptions {
    // Words from a wordlist instead of characters.
    bool passphrase = false;

    // Password
    int length = 20;
    bool lower = true;
    bool upper = true;
    bool numbers = true;
    // The printable ASCII punctuation, in KeePassXC's six groups. Together
    // they are every symbol on a US keyboard.
    bool logograms = true;     // # $ % & @ ^ ` ~
    bool punctuation = true;   // . , : ;
    bool quotes = true;        // " '
    bool dashes = true;        // \ / | _ -
    bool math = true;          // < > * + ! ? =
    bool braces = true;        // { [ ( ) ] }
    // Latin-1's printable characters past ASCII (¡ to ÿ).
    bool extendedAscii = false;
    bool excludeSimilar = false;
    // At least one character from each group in use.
    bool everyGroup = true;
    // Characters to leave out; characters to draw from as a group of their
    // own, beside the classes; and a set to draw from instead of the
    // classes. All optional.
    QString exclude;
    QString extra;
    QString custom;

    // Passphrase
    int words = 6;
    QString separator = QStringLiteral(" ");
};

namespace Generator {

// Below this many words a list is refused: with fewer, six words would not
// carry the strength a passphrase is meant to (6^4).
constexpr int minimumWordlistSize = 1296;

// Bounds the numbers and makes sure at least one character class is on, so
// the sheet cannot ask for something that has nothing to draw from.
GeneratorOptions normalize(GeneratorOptions options);

// Which wordlist the passphrase mode uses. `configured` is the config file's
// `generator.wordlist`: "auto" follows `language`, "pt-BR"/"en" name a list
// that ships with omapass, and anything else is a path. Set once at startup.
void configure(const QString &configured, const QString &language);

// The filenames to look for, in order: exposed for testing, since which file
// exists depends on the machine.
QStringList wordlistNames(const QString &configured, const QString &language);
// Directories searched for those names: installed beside omapass, the source
// tree when running from build/, and the user's own data directory.
QStringList wordlistDirectories();

// The first wordlist that is actually there. Empty when none is, which is
// what hides the passphrase mode.
QString wordlistPath();

// The groups a password is drawn from, after the exclusions: the classes
// and the extra characters, or the custom set alone. Empty groups are left
// out.
QStringList characterGroups(const GeneratorOptions &options);

// How many bits of entropy a result of these options carries, as generated:
// length × log2(pool) for a password, words × log2(list size) for a
// passphrase. 0 when nothing can be generated.
double entropyBits(const GeneratorOptions &options);

// KeePassXC's bands: below 40 bits "poor", below 75 "weak", below 100
// "good", and "excellent" from there.
QString quality(double bits);

bool generate(const GeneratorOptions &options, Secret *password, QString *error);

}
