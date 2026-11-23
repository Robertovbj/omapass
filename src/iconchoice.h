#pragma once

#include <QString>

class Vault;

// Whether the open vault's rows fetch their sites' icons, chosen per vault.
// A KeePass database keeps the choice in its own custom data, so it travels
// with the file to another computer; any other vault keeps it in omapass'
// settings on this one.
//
// Icons stored in the database (KeePassXC's downloaded favicons, or ones
// picked by hand) show either way: this is only about asking the sites.
struct IconChoice {
    bool download = false;
    bool duckDuckGo = false;
    // The user chose for this vault, as opposed to the rules below deciding.
    bool chosen = false;

    // The vault's own choice; else, for a database that already stores icons
    // (one KeePassXC filled), downloading on, so the entries without one
    // match the rest; else the defaults from config.toml.
    static IconChoice load(const Vault *vault, bool defaultDownload, bool defaultDuckDuckGo);
    // Keeps `choice` for the vault. False, with the reason, when the
    // database could not be written.
    static bool save(const Vault *vault, bool download, bool duckDuckGo, QString *error);

    // The custom data keys, for the tests.
    static QString downloadKey();
    static QString duckDuckGoKey();
};
