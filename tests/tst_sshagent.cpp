#include "support.h"

#include "process.h"
#include "sshagent.h"

#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>

class SshAgentTest : public QObject {
    Q_OBJECT

private slots:
    void tracksOnlyItsOwnKeys() {
#ifndef Q_OS_UNIX
        QSKIP("Requires an OpenSSH agent on Unix");
#else
        for (const QString &program : {QStringLiteral("ssh-agent"), QStringLiteral("ssh-add"),
                                       QStringLiteral("ssh-keygen")}) {
            if (QStandardPaths::findExecutable(program).isEmpty())
                QSKIP("OpenSSH tools are not installed");
        }

        QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/omapass-ssh-XXXXXX"));
        QVERIFY(dir.isValid());
        const QString socket = dir.filePath(QStringLiteral("ssh-agent.socket"));
        QProcess sshAgent;
        auto agentEnv = QProcessEnvironment::systemEnvironment();
        agentEnv.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        agentEnv.insert(QStringLiteral("SSH_ASKPASS"), QStandardPaths::findExecutable(QStringLiteral("false")));
        sshAgent.setProcessEnvironment(agentEnv);
        sshAgent.start(QStringLiteral("ssh-agent"),
                       {QStringLiteral("-D"), QStringLiteral("-a"), socket});
        QVERIFY(sshAgent.waitForStarted());
        QTRY_VERIFY(QFile::exists(socket));

        const auto previousSocket = qEnvironmentVariable("SSH_AUTH_SOCK");
        QVERIFY(qputenv("SSH_AUTH_SOCK", QFile::encodeName(socket)));
        const auto restoreSocket = qScopeGuard([&]() {
            if (previousSocket.isNull())
                qunsetenv("SSH_AUTH_SOCK");
            else
                qputenv("SSH_AUTH_SOCK", QFile::encodeName(previousSocket));
            sshAgent.terminate();
            if (!sshAgent.waitForFinished(1000)) {
                sshAgent.kill();
                sshAgent.waitForFinished();
            }
        });

        const auto createKey = [&](const QString &name) {
            const QString path = dir.filePath(name);
            return runProcess(QStringLiteral("ssh-keygen"),
                              {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                               QStringLiteral("-N"), QString(), QStringLiteral("-C"), name,
                               QStringLiteral("-f"), path}).success;
        };
        const QString first = QStringLiteral("existing-key");
        const QString second = QStringLiteral("vault-key");
        QVERIFY(createKey(first));
        QVERIFY(createKey(second));
        QFile file(dir.filePath(second));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const Secret key(file.readAll());

        SshAgent agent;
        const auto listed = []() {
            return runProcess(QStringLiteral("ssh-add"), {QStringLiteral("-L")}).out;
        };
        const auto canSign = [&]() {
            return runProcess(QStringLiteral("ssh-add"),
                              {QStringLiteral("-T"), dir.filePath(second + QStringLiteral(".pub"))}).success;
        };
        const QString error = agent.add(second, key, {});
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(canSign());
        QVERIFY(agent.add(second, key, {false, true, 0}).isEmpty());
        QVERIFY(!canSign());
        QVERIFY(agent.clear().isEmpty());

        QVERIFY(runProcess(QStringLiteral("ssh-add"), {dir.filePath(first)}).success);
        const QString before = listed();
        QVERIFY(before.contains(first));
        QVERIFY(agent.add(second, key, {false, true, 0}).isEmpty());
        QVERIFY(listed().contains(second));
        QVERIFY(!canSign());
        QVERIFY(agent.remove(second).isEmpty());
        QCOMPARE(listed(), before);

        QVERIFY(agent.add(second, key, {}).isEmpty());
        QVERIFY(agent.clear().isEmpty());
        QCOMPARE(listed(), before);

        QVERIFY(agent.add(second, key, {false, false, 1}).isEmpty());
        QTRY_COMPARE(listed(), before);
        QVERIFY(agent.clear().isEmpty());

        QFile firstKey(dir.filePath(first));
        QVERIFY(firstKey.open(QIODevice::ReadOnly));
        QVERIFY(!agent.add(first, Secret(firstKey.readAll()), {}).isEmpty());
        QVERIFY(agent.clear().isEmpty());
        QCOMPARE(listed(), before);

        // A new instance has no record for a key left in the agent.
        QVERIFY(agent.add(second, key, {}).isEmpty());
        QVERIFY(agent.isLoaded(second));
        // Another entry with the same key is still detected as loaded in the agent.
        QVERIFY(agent.isLoaded(QStringLiteral("duplicate-entry"), key));
        SshAgent restarted;
        QVERIFY(!restarted.tracks(second));
        QVERIFY(restarted.isLoaded(second, key));
        QVERIFY(!restarted.add(second, key, {}).isEmpty());
        QVERIFY(restarted.clear().isEmpty());
        QVERIFY(listed().contains(second));
        QVERIFY(restarted.removeKey(key).isEmpty());
        QVERIFY(!restarted.isLoaded(second, key));
        QCOMPARE(listed(), before);
        QVERIFY(restarted.add(second, key, {false, true, 0}).isEmpty());
        QVERIFY(!canSign());
        QVERIFY(restarted.clear().isEmpty());
        QCOMPARE(listed(), before);
#ifdef Q_OS_LINUX
        const QByteArray oldRuntime = qgetenv("XDG_RUNTIME_DIR");
        const auto restoreRuntime = qScopeGuard([&]() {
            if (oldRuntime.isNull())
                qunsetenv("XDG_RUNTIME_DIR");
            else
                qputenv("XDG_RUNTIME_DIR", oldRuntime);
        });
        QVERIFY(qputenv("XDG_RUNTIME_DIR", QFile::encodeName(dir.path())));
        QVERIFY(qputenv("SSH_AUTH_SOCK", QFile::encodeName(dir.filePath(QStringLiteral("missing")))));
        QVERIFY(!agent.add(second, key, {}).isEmpty());
        QVERIFY(qunsetenv("SSH_AUTH_SOCK"));
        QVERIFY(agent.add(second, key, {}).isEmpty());
        QVERIFY(agent.clear().isEmpty());
#endif
#endif
    }

    void installsAndUninstallsAgentSetupFiles() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QVERIFY(!SshAgent::isSetupInstalled(home.path()));

        QStringList written;
        QString error;
        // Without a system ssh-agent.socket: writes environment.d, uwsm/env.d,
        // and the fallback user service.
        QVERIFY(SshAgent::installSetup(home.path(), false, true, false, &written, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(written.size(), 3);
        QVERIFY(SshAgent::isSetupInstalled(home.path()));

        QFile envFile(home.filePath(QStringLiteral(".config/environment.d/90-omapass-ssh-agent.conf")));
        QVERIFY(envFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(envFile.readAll())
                    .contains(QStringLiteral("SSH_AUTH_SOCK=${XDG_RUNTIME_DIR}/ssh-agent.socket")));

        QFile uwsmFile(home.filePath(QStringLiteral(".config/uwsm/env.d/20-omapass-ssh-agent")));
        QVERIFY(uwsmFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(uwsmFile.readAll())
                    .contains(QStringLiteral("export SSH_AUTH_SOCK=\"${XDG_RUNTIME_DIR}/ssh-agent.socket\"")));

        QFile unitFile(home.filePath(QStringLiteral(".config/systemd/user/omapass-ssh-agent.service")));
        QVERIFY(unitFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(unitFile.readAll()).contains(QStringLiteral("-D -a %t/ssh-agent.socket")));

        QStringList removed;
        QVERIFY(SshAgent::uninstallSetup(home.path(), false, &removed, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(removed.size(), 3);
        QVERIFY(!SshAgent::isSetupInstalled(home.path()));
    }
};

OMAPASS_TEST(SshAgentTest)
#include "tst_sshagent.moc"
