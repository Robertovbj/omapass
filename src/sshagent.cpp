#include "sshagent.h"

#include "i18n.h"
#include "process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <utility>

namespace {

constexpr int timeoutMs = 2000;

QString publicKey(const QString &line) {
    const QStringList fields = line.simplified().split(QLatin1Char(' '));
    if (fields.size() < 2 || fields.at(1).isEmpty())
        return {};
    return fields.at(0) + QLatin1Char(' ') + fields.at(1);
}

ProcResult runSsh(const QString &program, const QStringList &args, const QByteArray &input = {}) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_LINUX
    if (program == QLatin1String("ssh-add") && env.value(QStringLiteral("SSH_AUTH_SOCK")).isEmpty()) {
        const QString runtime = env.value(QStringLiteral("XDG_RUNTIME_DIR"));
        if (QDir::isAbsolutePath(runtime))
            env.insert(QStringLiteral("SSH_AUTH_SOCK"), QDir(runtime).filePath(QStringLiteral("ssh-agent.socket")));
    }
#endif
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("never"));
    return runProcess(program, args, input, env, timeoutMs);
}

bool canResolveIdentity() {
    return SshAgent::supported()
        && !QStandardPaths::findExecutable(QStringLiteral("ssh-add")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("ssh-keygen")).isEmpty();
}

QString derivePublicKey(const Secret &key) {
    const ProcResult result = runSsh(QStringLiteral("ssh-keygen"),
        {QStringLiteral("-y"), QStringLiteral("-P"), QString(), QStringLiteral("-f"),
         QStringLiteral("/dev/stdin")}, key.bytes());
    return result.success ? publicKey(result.out) : QString();
}

bool identities(QStringList *keys) {
    const ProcResult result = runSsh(QStringLiteral("ssh-add"), {QStringLiteral("-L")});
    if (result.success) {
        for (const QString &line : result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            keys->append(publicKey(line));
        return true;
    }
    return result.out.trimmed() == QLatin1String("The agent has no identities.");
}

bool removeIdentities(const QByteArray &keys) {
    return runSsh(QStringLiteral("ssh-add"),
                  {QStringLiteral("-k"), QStringLiteral("-d"), QStringLiteral("-")}, keys).success;
}

}

bool SshAgent::supported() {
#ifdef Q_OS_UNIX
    return true;
#else
    return false;
#endif
}

QString SshAgent::publicIdentity(const Secret &key) {
    return derivePublicKey(key);
}

bool SshAgent::isIdentityLoaded(const QString &publicKeyLine) {
    if (!canResolveIdentity())
        return false;
    const QString identity = publicKey(publicKeyLine);
    if (identity.isEmpty())
        return false;
    QStringList keys;
    return identities(&keys) && keys.contains(identity);
}

bool SshAgent::isLoaded(const QString &id, const Secret &key) const {
    if (!canResolveIdentity())
        return false;
    QString identity = m_added.value(id);
    if (identity.isEmpty() && !key.isEmpty())
        identity = derivePublicKey(key);
    if (identity.isEmpty())
        return false;
    QStringList keys;
    return identities(&keys) && keys.contains(identity);
}

QString SshAgent::add(const QString &id, const Secret &key, const SshKeyOptions &options) {
    if (!canResolveIdentity())
        return I18n::t(QStringLiteral("ssh.unavailable"));

    const QString identity = derivePublicKey(key);
    if (identity.isEmpty())
        return I18n::t(QStringLiteral("ssh.invalid_key"));

    QStringList keys;
    if (!identities(&keys))
        return I18n::t(QStringLiteral("ssh.unavailable"));
    const QString previous = m_added.value(id);
    const bool alreadyLoaded = keys.contains(identity);
    if (alreadyLoaded && previous != identity)
        return I18n::t(QStringLiteral("ssh.already_present"));

    if (!previous.isEmpty() && previous != identity && keys.contains(previous)) {
        const QString error = remove(id);
        if (!error.isEmpty())
            return error;
    }

    QStringList args{QStringLiteral("-k")};
    if (options.confirm)
        args << QStringLiteral("-c");
    if (options.lifetimeSeconds > 0)
        args << QStringLiteral("-t") << QString::number(options.lifetimeSeconds);
    args << QStringLiteral("-");

    // Keep tracking a fresh key even if the response is lost. A failed
    // update of an existing key leaves its original registration in place.
    if (!alreadyLoaded)
        m_added.insert(id, identity);
    if (runSsh(QStringLiteral("ssh-add"), args, key.bytes()).success)
        return {};
    if (!alreadyLoaded)
        remove(id);
    return I18n::t(QStringLiteral("ssh.add_failed"));
}

QString SshAgent::removePublicKey(const QString &key) {
    if (removeIdentities(key.toUtf8() + '\n'))
        return {};

    QStringList keys;
    if (identities(&keys) && !keys.contains(key))
        return {}; // The agent may already have expired the key.
    return I18n::t(QStringLiteral("ssh.remove_failed"));
}

QString SshAgent::remove(const QString &id) {
    const auto added = m_added.constFind(id);
    if (added == m_added.cend())
        return I18n::t(QStringLiteral("ssh.not_added"));
    const QString error = removePublicKey(added.value());
    if (error.isEmpty())
        m_added.remove(id);
    return error;
}

QString SshAgent::removeKey(const Secret &key) {
    if (!canResolveIdentity())
        return I18n::t(QStringLiteral("ssh.unavailable"));
    const QString identity = derivePublicKey(key);
    if (identity.isEmpty())
        return I18n::t(QStringLiteral("ssh.invalid_key"));

    const QString error = removePublicKey(identity);
    if (error.isEmpty())
        m_added.removeIf([&](QHash<QString, QString>::iterator entry) {
            return entry.value() == identity;
        });
    return error;
}

QString SshAgent::clear() {
    if (m_added.isEmpty())
        return {};

    QByteArray keys;
    for (const QString &key : std::as_const(m_added))
        keys += key.toUtf8() + '\n';
    removeIdentities(keys);

    // ssh-add can report success after removing only some of its input.
    QStringList remaining;
    if (identities(&remaining)) {
        for (auto it = m_added.begin(); it != m_added.end();) {
            if (remaining.contains(it.value()))
                ++it;
            else
                it = m_added.erase(it);
        }
    }
    return m_added.isEmpty() ? QString() : I18n::t(QStringLiteral("ssh.remove_failed"));
}

namespace {

QString agentEnvDPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.config/environment.d/90-omapass-ssh-agent.conf");
}

QString agentUwsmEnvPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.config/uwsm/env.d/20-omapass-ssh-agent");
}

QString agentFallbackUnitPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.config/systemd/user/omapass-ssh-agent.service");
}

bool hasSystemSshAgentSocket() {
    return QFileInfo::exists(QStringLiteral("/usr/lib/systemd/user/ssh-agent.socket"))
        || QFileInfo::exists(QStringLiteral("/etc/systemd/user/ssh-agent.socket"));
}

bool detectUwsmSession(const QString &homeDir) {
    return QDir(homeDir + QStringLiteral("/.config/uwsm")).exists()
        || !qEnvironmentVariable("UWSM_FINALIZE_VARNAMES").isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("uwsm")).isEmpty();
}

bool writeConfigFile(const QString &path, const QString &content, QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not create directory: ") + info.absolutePath();
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not write ") + path + QStringLiteral(": ") + file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << content;
    return true;
}

}

bool SshAgent::setupSupported() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool SshAgent::isSetupInstalled() {
    return isSetupInstalled(QDir::homePath());
}

bool SshAgent::isSetupInstalled(const QString &homeDir) {
    return QFileInfo::exists(agentEnvDPath(homeDir))
        || QFileInfo::exists(agentUwsmEnvPath(homeDir))
        || QFileInfo::exists(agentFallbackUnitPath(homeDir));
}

bool SshAgent::installSetup(QStringList *writtenFiles, QString *error) {
    if (!setupSupported()) {
        if (error)
            *error = I18n::t(QStringLiteral("ssh.agent_setup_unsupported"));
        return false;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ssh-agent")).isEmpty()) {
        if (error)
            *error = I18n::t(QStringLiteral("ssh.agent_binary_missing"));
        return false;
    }
    const QString homeDir = QDir::homePath();
    return installSetup(homeDir, hasSystemSshAgentSocket(), detectUwsmSession(homeDir),
                        true, writtenFiles, error);
}

bool SshAgent::installSetup(const QString &homeDir, bool hasSystemSocket, bool includeUwsm,
                            bool runSystemctl, QStringList *writtenFiles, QString *error) {
    QStringList written;

    const QString envD = agentEnvDPath(homeDir);
    const QString envDContent = QStringLiteral(
        "# Managed by omapass\n"
        "SSH_AUTH_SOCK=${XDG_RUNTIME_DIR}/ssh-agent.socket\n");
    if (!writeConfigFile(envD, envDContent, error))
        return false;
    written.append(envD);

    if (includeUwsm) {
        const QString uwsm = agentUwsmEnvPath(homeDir);
        const QString uwsmContent = QStringLiteral(
            "# Managed by omapass\n"
            "export SSH_AUTH_SOCK=\"${XDG_RUNTIME_DIR}/ssh-agent.socket\"\n");
        if (!writeConfigFile(uwsm, uwsmContent, error))
            return false;
        written.append(uwsm);
    }

    QString unitToEnable = QStringLiteral("ssh-agent.socket");
    if (!hasSystemSocket) {
        const QString sshAgentBin = QStandardPaths::findExecutable(QStringLiteral("ssh-agent"));
        const QString execBin = sshAgentBin.isEmpty() ? QStringLiteral("/usr/bin/ssh-agent") : sshAgentBin;
        const QString unitPath = agentFallbackUnitPath(homeDir);
        const QString unitContent = QStringLiteral(
            "# Managed by omapass\n"
            "[Unit]\n"
            "Description=OpenSSH key agent (omapass)\n"
            "\n"
            "[Service]\n"
            "Type=simple\n"
            "ExecStart=") + execBin + QStringLiteral(" -D -a %t/ssh-agent.socket\n"
            "SuccessExitStatus=2\n"
            "\n"
            "[Install]\n"
            "WantedBy=default.target\n");
        if (!writeConfigFile(unitPath, unitContent, error))
            return false;
        written.append(unitPath);
        unitToEnable = QStringLiteral("omapass-ssh-agent.service");
    }

    if (runSystemctl) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            if (!hasSystemSocket)
                runProcess(systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, {}, {}, 2000);
            runProcess(systemctl,
                       {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("--now"),
                        unitToEnable},
                       {}, {}, 3000);
            const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
            if (!runtimeDir.isEmpty()) {
                runProcess(systemctl,
                           {QStringLiteral("--user"), QStringLiteral("set-environment"),
                            QStringLiteral("SSH_AUTH_SOCK=") + runtimeDir + QStringLiteral("/ssh-agent.socket")},
                           {}, {}, 2000);
            }
        }
    }

    if (writtenFiles)
        *writtenFiles = written;
    return true;
}

bool SshAgent::uninstallSetup(QStringList *removedFiles, QString *error) {
    return uninstallSetup(QDir::homePath(), true, removedFiles, error);
}

bool SshAgent::uninstallSetup(const QString &homeDir, bool runSystemctl,
                              QStringList *removedFiles, QString *error) {
    const bool hadFallbackUnit = QFileInfo::exists(agentFallbackUnitPath(homeDir));

    if (runSystemctl) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            const QString unit = hadFallbackUnit
                ? QStringLiteral("omapass-ssh-agent.service")
                : QStringLiteral("ssh-agent.socket");
            runProcess(systemctl,
                       {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), unit},
                       {}, {}, 3000);
            runProcess(systemctl,
                       {QStringLiteral("--user"), QStringLiteral("unset-environment"),
                        QStringLiteral("SSH_AUTH_SOCK")},
                       {}, {}, 2000);
        }
    }

    QStringList removed;
    const auto tryRemove = [&removed, error](const QString &path) {
        if (!QFileInfo::exists(path))
            return true;
        if (!QFile::remove(path)) {
            if (error)
                *error = QStringLiteral("Could not remove ") + path;
            return false;
        }
        removed.append(path);
        return true;
    };

    if (!tryRemove(agentEnvDPath(homeDir))
        || !tryRemove(agentUwsmEnvPath(homeDir))
        || !tryRemove(agentFallbackUnitPath(homeDir)))
        return false;

    if (runSystemctl && hadFallbackUnit) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty())
            runProcess(systemctl, {QStringLiteral("--user"), QStringLiteral("daemon-reload")}, {}, {}, 2000);
    }

    if (removedFiles)
        *removedFiles = removed;
    return true;
}
