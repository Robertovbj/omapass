#include "sshagent.h"

#include "i18n.h"
#include "process.h"

#include <QDir>
#include <QStandardPaths>

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
