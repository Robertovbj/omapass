#pragma once

#include <QHash>
#include <QString>

#include "secret.h"

struct SshKeyOptions {
    bool addOnUnlock = false;
    bool confirm = false;
    int lifetimeSeconds = 0;
};

struct SshKeyConfig {
    QString id;
    SshKeyOptions defaults;
};

// One open vault's additions to the agent inherited by the application.
// Retains only public keys, so removal still works after entries change.
class SshAgent {
public:
    static bool supported();
    static QString publicIdentity(const Secret &key);
    static bool isIdentityLoaded(const QString &publicKeyLine);

    // User-level OpenSSH agent setup (systemd user socket/service +
    // ~/.config/environment.d and ~/.config/uwsm/env.d). Linux only.
    static bool setupSupported();
    static bool isSetupInstalled();
    static bool isSetupInstalled(const QString &homeDir);
    static bool installSetup(QStringList *writtenFiles, QString *error);
    static bool installSetup(const QString &homeDir, bool hasSystemSocket, bool includeUwsm,
                             bool runSystemctl, QStringList *writtenFiles, QString *error);
    static bool uninstallSetup(QStringList *removedFiles, QString *error);
    static bool uninstallSetup(const QString &homeDir, bool runSystemctl,
                               QStringList *removedFiles, QString *error);

    QString add(const QString &id, const Secret &key, const SshKeyOptions &options);
    bool tracks(const QString &id) const { return m_added.contains(id); }
    bool isLoaded(const QString &id, const Secret &key = {}) const;
    QString remove(const QString &id);
    QString removeKey(const Secret &key);
    QString clear();

private:
    QHash<QString, QString> m_added;
    QString removePublicKey(const QString &key);
};
