#include "support.h"

#include "sshaskpass.h"

class TestSshAskpass : public QObject {
    Q_OBJECT

private slots:
    void detectsExplicitFlagAndSymlinkNames() {
        QVERIFY(SshAskpass::isInvocation({QStringLiteral("omapass"), QStringLiteral("--ssh-askpass")}));
        QVERIFY(SshAskpass::isInvocation({QStringLiteral("/usr/bin/omapass"),
                                          QStringLiteral("--ssh-askpass"),
                                          QStringLiteral("Allow use of key?")}));
        QVERIFY(SshAskpass::isInvocation({QStringLiteral("/usr/bin/omapass-ssh-askpass"),
                                          QStringLiteral("Allow use of key?")}));
        QVERIFY(SshAskpass::isInvocation({QStringLiteral("/usr/lib/ssh/ssh-askpass"),
                                          QStringLiteral("Allow use of key?")}));
        QVERIFY(SshAskpass::isInvocation({QStringLiteral("omapass-ssh-askpass.exe")}));

        QVERIFY(!SshAskpass::isInvocation({}));
        QVERIFY(!SshAskpass::isInvocation({QStringLiteral("omapass")}));
        QVERIFY(!SshAskpass::isInvocation({QStringLiteral("omapass"), QStringLiteral("--doctor")}));
    }

    void parsesPromptModeAndMessage() {
        const SshAskpass::Request confirm = SshAskpass::parseRequest(
            {QStringLiteral("/usr/lib/ssh/ssh-askpass"), QStringLiteral("Allow use of key id_ed25519?")},
            QStringLiteral("confirm"));
        QCOMPARE(confirm.mode, SshAskpass::Mode::Confirm);
        QCOMPARE(confirm.prompt, QStringLiteral("Allow use of key id_ed25519?"));

        const SshAskpass::Request info = SshAskpass::parseRequest(
            {QStringLiteral("omapass"), QStringLiteral("--ssh-askpass"), QStringLiteral("Touch"),
             QStringLiteral("security key")},
            QStringLiteral(" NONE "));
        QCOMPARE(info.mode, SshAskpass::Mode::Info);
        QCOMPARE(info.prompt, QStringLiteral("Touch security key"));

        const SshAskpass::Request passphrase = SshAskpass::parseRequest(
            {QStringLiteral("/usr/bin/omapass-ssh-askpass"), QStringLiteral("Enter passphrase:")},
            QString());
        QCOMPARE(passphrase.mode, SshAskpass::Mode::Passphrase);
        QCOMPARE(passphrase.prompt, QStringLiteral("Enter passphrase:"));
    }

    void controllerTracksDecisionAndClearsSecret() {
        SshAskpass::Request request;
        request.mode = SshAskpass::Mode::Passphrase;
        request.prompt = QStringLiteral("Passphrase:");

        SshAskpass::Controller controller(request);
        QCOMPARE(controller.mode(), QStringLiteral("passphrase"));
        QCOMPARE(controller.prompt(), QStringLiteral("Passphrase:"));
        QVERIFY(!controller.accepted());

        controller.accept(QStringLiteral("s3cret"));
        QVERIFY(controller.accepted());
        QCOMPARE(controller.secret(), QStringLiteral("s3cret"));

        controller.clearSecret();
        QVERIFY(controller.secret().isEmpty());

        controller.reject();
        QVERIFY(!controller.accepted());
        QVERIFY(controller.secret().isEmpty());
    }

    void installsAndUninstallsEnvironmentFiles() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        QVERIFY(!SshAskpass::isInstalled(home.path()));

        QStringList written;
        QString error;
        QVERIFY(SshAskpass::install(home.path(), QStringLiteral("/usr/bin/omapass-ssh-askpass"),
                                    true, false, &written, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(written.size(), 2);
        QVERIFY(SshAskpass::isInstalled(home.path()));

        QFile envFile(home.filePath(QStringLiteral(".config/environment.d/90-omapass-ssh-askpass.conf")));
        QVERIFY(envFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString envText = QString::fromUtf8(envFile.readAll());
        QVERIFY(envText.contains(QStringLiteral("SSH_ASKPASS=/usr/bin/omapass-ssh-askpass")));
        QVERIFY(envText.contains(QStringLiteral("SSH_ASKPASS_REQUIRE=prefer")));

        QFile uwsmFile(home.filePath(QStringLiteral(".config/uwsm/env.d/20-omapass-ssh-askpass")));
        QVERIFY(uwsmFile.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString uwsmText = QString::fromUtf8(uwsmFile.readAll());
        QVERIFY(uwsmText.contains(QStringLiteral("export SSH_ASKPASS='/usr/bin/omapass-ssh-askpass'")));
        QVERIFY(uwsmText.contains(QStringLiteral("export SSH_ASKPASS_REQUIRE=prefer")));

        QStringList removed;
        QVERIFY(SshAskpass::uninstall(home.path(), false, &removed, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(removed.size(), 2);
        QVERIFY(!SshAskpass::isInstalled(home.path()));
    }
};

OMAPASS_TEST(TestSshAskpass)

#include "tst_sshaskpass.moc"
