#pragma once

#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "browserprotocol.h"
#include "browserserver.h"
#include "vault.h"

class BrowserStore;

// What the integration needs from the rest of the app. AppController is the
// one that implements it; keeping it to this keeps the integration from
// reaching into the controller's state.
class BrowserApp {
public:
    virtual ~BrowserApp() = default;
    // The open vault, or null while locked.
    virtual const Vault *browserVault() const = 0;
    // The entries as the list has them (empty groups left out), the groups,
    // and the login and URL of every entry it knows them for.
    virtual QStringList browserEntries() const = 0;
    virtual QStringList browserGroups() const = 0;
    virtual QHash<QString, EntryMeta> browserMeta() const = 0;
    virtual void browserLock() = 0;
    virtual Secret browserGeneratePassword() = 0;
    // Runs a change to the vault off the main thread, the way the app's own
    // edits run, then lists the entries again. False at once when the vault
    // cannot take a change now (no vault, or another change running).
    virtual bool browserChange(std::function<bool(const Vault *, QString *)> work,
                               std::function<void(bool)> done) = 0;
};

// The browser integration as the app sees it: the socket the extension
// reaches, the manifests that point the browser at omapass, and the questions
// it puts to the user (link a browser, let one have a login, overwrite a
// password). Also what QML shows those questions with.
class BrowserIntegration : public QObject, public BrowserHost {
    Q_OBJECT

    // Linux only for now: elsewhere the option is not offered.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    // The question on screen, or an empty map. `kind` says which:
    // "associate" (`vault`), "access" (`site`, `entries`: [{path, name,
    // login}]), "update" (`name`, `login`, `site`) or "conflict"
    // (`browsers`, a list of names).
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged)

public:
    BrowserIntegration(BrowserApp *app, const QString &localDir, QObject *parent = nullptr);
    ~BrowserIntegration() override;

    static bool supported();
    bool running() const;
    QVariantMap prompt() const { return m_prompt; }

    // Starts or stops answering the browser, with the manifests. Turning it
    // on can stop at a "conflict" prompt (another program's manifest).
    // `quiet` skips the status messages, for the start of the app.
    void setEnabled(bool enabled, bool quiet = false);

    // The open vault changed (opened, locked, switched): pending questions
    // about the old one are dropped and the browsers are told.
    void vaultChanged();
    // Waits for the reads in flight, before the vault they read goes.
    void waitForReads();
    // The app's own edits, so a local copy of the permissions follows.
    void entryMoved(const QString &from, const QString &to);
    void entryRemoved(const QString &path);

    // Answers to the prompt. An empty name refuses the association.
    Q_INVOKABLE void answerAssociate(const QString &name);
    Q_INVOKABLE bool associationExists(const QString &name) const;
    // The paths let out, of those asked about; `remember` keeps the answer
    // for the site, both ways.
    Q_INVOKABLE void answerAccess(const QStringList &allowed, bool remember);
    Q_INVOKABLE void answerUpdate(bool accepted);
    Q_INVOKABLE void answerConflict(bool replace);

    // BrowserHost
    bool unlocked() const override;
    void showUnlock() override;
    QString databaseHash() const override;
    QString associationKey(const QString &id) const override;
    void associate(const QString &idKey, std::function<void(const QString &)> done) override;
    void findLogins(const BrowserLoginQuery &query,
                    std::function<void(const QVector<BrowserLogin> &)> done) override;
    void totp(const QString &uuid, std::function<void(const QString &)> done) override;
    void saveLogin(const BrowserSaveRequest &request, std::function<void(bool)> done) override;
    QJsonArray groups() const override;
    void createGroup(const QString &path,
                     std::function<void(const QString &, const QString &)> done) override;
    Secret generatePassword() override;
    void lock() override;

signals:
    void runningChanged();
    void promptChanged();
    // The window should come up: the browser asked to unlock, or asks the
    // user something.
    void raiseRequested();
    void message(const QString &text, bool isError);

private:
    struct Prompt {
        QVariantMap shown;
        // Called with the answer; a dropped prompt is answered with nothing.
        std::function<void(const QVariant &)> answer;
    };
    void ask(const QVariantMap &shown, std::function<void(const QVariant &)> answer);
    void answer(const QVariant &value);
    void showNextPrompt();
    void dropPrompts();

    BrowserStore *store() const;
    QString pathOfUuid(const QString &uuid) const;
    // Reads `paths` from the vault off the main thread and hands the logins
    // to `done`, if the vault is still the same one by then.
    void deliver(const QStringList &paths, std::function<void(const QVector<BrowserLogin> &)> done);
    void install(bool replace, bool quiet);

    BrowserApp *m_app;
    QString m_localDir;
    BrowserServer m_server;
    bool m_enabled = false;
    mutable std::unique_ptr<BrowserStore> m_store;
    mutable const Vault *m_storeVault = nullptr;
    // Counts the vaults opened, so an answer about one is not given for the
    // next.
    quint64 m_generation = 0;

    QVariantMap m_prompt;
    QList<Prompt> m_prompts;

    QList<QFuture<void>> m_reads;
    // The entries handed to a browser since the vault opened: a code is only
    // given for one of them.
    QSet<QString> m_handedOut;
    // Groups the browser asked for that have no entry yet (and so do not
    // exist anywhere but here), by the id it was given.
    QHash<QString, QString> m_pendingGroups;
};
