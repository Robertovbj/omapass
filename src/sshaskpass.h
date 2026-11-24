#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Built-in OpenSSH askpass responder. Invoked either via `omapass --ssh-askpass
// <prompt>` or through a symlink named `omapass-ssh-askpass` / `ssh-askpass`,
// since OpenSSH executes SSH_ASKPASS directly without splitting arguments.
namespace SshAskpass {

enum class Mode {
    Passphrase,
    Confirm,
    Info,
};

struct Request {
    Mode mode = Mode::Passphrase;
    QString prompt;
};

bool isInvocation(const QStringList &args);
Request parseRequest(const QStringList &args, const QString &promptTypeEnv);
int run(int argc, char *argv[], const QStringList &args);

// User-level SSH_ASKPASS registration (~/.config/environment.d and, when UWSM
// is present, ~/.config/uwsm/env.d). Supported on Linux only.
bool integrationSupported();
bool isInstalled();
bool isInstalled(const QString &homeDir);
bool install(QStringList *writtenFiles, QString *error);
bool install(const QString &homeDir, const QString &executablePath, bool includeUwsm,
             bool updateSystemd, QStringList *writtenFiles, QString *error);
bool uninstall(QStringList *removedFiles, QString *error);
bool uninstall(const QString &homeDir, bool updateSystemd, QStringList *removedFiles, QString *error);

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT)
    Q_PROPERTY(QString prompt READ prompt CONSTANT)

public:
    explicit Controller(const Request &request, QObject *parent = nullptr);

    QString mode() const;
    QString prompt() const { return m_request.prompt; }
    bool accepted() const { return m_accepted; }
    QString secret() const { return m_secret; }
    void clearSecret();

    Q_INVOKABLE void accept(const QString &secret = QString());
    Q_INVOKABLE void reject();

private:
    Request m_request;
    bool m_accepted = false;
    QString m_secret;
};

}
