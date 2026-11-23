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

    QString add(const QString &id, const Secret &key, const SshKeyOptions &options);
    bool tracks(const QString &id) const { return m_added.contains(id); }
    QString remove(const QString &id);
    QString removeKey(const Secret &key);
    QString clear();

private:
    QHash<QString, QString> m_added;
    QString removePublicKey(const QString &key);
};
