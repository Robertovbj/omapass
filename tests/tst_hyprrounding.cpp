// Following Hyprland's window rounding: asked of hyprctl on start, and again
// when the event socket says the config reloaded. Both are fakes here, a
// script standing in for hyprctl and a local server for the socket, so the
// real compositor is never asked.

#include "support.h"

#include "hyprrounding.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestHyprRounding : public QObject {
    Q_OBJECT

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        m_value = m_dir.filePath(QStringLiteral("value"));
        m_hyprctl = m_dir.filePath(QStringLiteral("hyprctl"));
        QFile script(m_hyprctl);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\n"
                     "[ \"$*\" = \"-j getoption decoration:rounding\" ] || exit 1\n"
                     "printf '{\"option\": \"decoration:rounding\", \"int\": %s, \"set\": true}' \"$(cat '"
                     + QFile::encodeName(m_value) + "')\"\n");
        script.close();
        QVERIFY(QFile::setPermissions(m_hyprctl, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        setValue("8");
    }

    void asksOnStart() {
        HyprRounding rounding(nullptr, m_hyprctl, m_dir.filePath(QStringLiteral("none.sock")));
        QSignalSpy changed(&rounding, &HyprRounding::radiusChanged);
        QVERIFY(changed.wait(3000));
        QCOMPARE(rounding.radius(), 8);
    }

    void asksAgainWhenTheConfigReloads() {
        QLocalServer server;
        QVERIFY(server.listen(m_dir.filePath(QStringLiteral("events.sock"))));
        HyprRounding rounding(nullptr, m_hyprctl, server.fullServerName());
        QSignalSpy changed(&rounding, &HyprRounding::radiusChanged);
        QVERIFY(server.waitForNewConnection(3000));
        QLocalSocket *events = server.nextPendingConnection();
        QTRY_COMPARE_WITH_TIMEOUT(rounding.radius(), 8, 3000);

        // Other events say nothing about the rounding.
        setValue("12");
        events->write("activewindow>>foot,~\n");
        events->flush();
        QTest::qWait(300);
        QCOMPARE(rounding.radius(), 8);

        events->write("configreloaded>>\n");
        events->flush();
        QTRY_COMPARE_WITH_TIMEOUT(rounding.radius(), 12, 3000);
        QCOMPARE(changed.count(), 2);
    }

    void withoutHyprlandTheCornersStaySharp() {
        const QByteArray signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        HyprRounding rounding;
        QSignalSpy changed(&rounding, &HyprRounding::radiusChanged);
        QTest::qWait(300);
        if (!signature.isNull())
            qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(rounding.radius(), 0);
    }

private:
    void setValue(const QByteArray &value) {
        QFile file(m_value);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(value);
    }

    QTemporaryDir m_dir;
    QString m_hyprctl;
    QString m_value;
};

OMAPASS_TEST(TestHyprRounding)
#include "tst_hyprrounding.moc"
