#include "sshaskpass.h"

#include "i18n.h"
#include "process.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTextStream>

#ifdef QT_QUICK_LIB
#include "config.h"
#include "hyprrounding.h"
#include "palette.h"
#include "sessionlock.h"
#include "systemtheme.h"

#include <QFont>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

#include <cstdio>
#endif

namespace SshAskpass {

namespace {

bool isAskpassProgramName(const QString &argv0) {
    QString name = QFileInfo(argv0).fileName().toLower();
    if (name.endsWith(QLatin1String(".exe")))
        name.chop(4);
    return name == QLatin1String("omapass-ssh-askpass")
        || name == QLatin1String("ssh-askpass");
}

bool hasAskpassFlag(const QStringList &args) {
    return args.size() > 1 && args.at(1) == QLatin1String("--ssh-askpass");
}

QString envDPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.config/environment.d/90-omapass-ssh-askpass.conf");
}

QString uwsmEnvPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.config/uwsm/env.d/20-omapass-ssh-askpass");
}

QString localWrapperPath(const QString &homeDir) {
    return homeDir + QStringLiteral("/.local/bin/omapass-ssh-askpass");
}

QString shellSingleQuote(QString value) {
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

bool writeTextFile(const QString &path, const QString &content, QFileDevice::Permissions perms,
                   QString *error) {
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
    file.close();
    if (perms != QFileDevice::Permissions())
        QFile::setPermissions(path, perms);
    return true;
}

bool detectUwsm(const QString &homeDir) {
    return QDir(homeDir + QStringLiteral("/.config/uwsm")).exists()
        || !qEnvironmentVariable("UWSM_FINALIZE_VARNAMES").isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("uwsm")).isEmpty();
}

QString resolveAskpassCommand(const QString &homeDir, QString *error) {
    const QString appImage = qEnvironmentVariable("APPIMAGE").trimmed();
    if (!appImage.isEmpty() && QFileInfo::exists(appImage)) {
        const QString wrapper = localWrapperPath(homeDir);
        const QString script = QStringLiteral("#!/bin/sh\n# Managed by omapass\nexec ")
            + shellSingleQuote(appImage) + QStringLiteral(" --ssh-askpass \"$@\"\n");
        if (!writeTextFile(wrapper, script,
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                               | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                               | QFileDevice::ReadOther | QFileDevice::ExeOther,
                           error))
            return {};
        return wrapper;
    }

    const QString appPath = QCoreApplication::applicationFilePath();
    const QString sibling = QFileInfo(appPath).absolutePath() + QStringLiteral("/omapass-ssh-askpass");
    if (QFileInfo::exists(sibling))
        return sibling;

    const QString systemBin = QStringLiteral("/usr/bin/omapass-ssh-askpass");
    if (QFileInfo::exists(systemBin))
        return systemBin;

    // Local/dev build: create a wrapper in ~/.local/bin pointing at this binary.
    const QString wrapper = localWrapperPath(homeDir);
    const QString script = QStringLiteral("#!/bin/sh\n# Managed by omapass\nexec ")
        + shellSingleQuote(appPath) + QStringLiteral(" --ssh-askpass \"$@\"\n");
    if (!writeTextFile(wrapper, script,
                       QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                           | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                           | QFileDevice::ReadOther | QFileDevice::ExeOther,
                       error))
        return {};
    return wrapper;
}

}

bool integrationSupported() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

bool isInstalled() {
    return isInstalled(QDir::homePath());
}

bool isInstalled(const QString &homeDir) {
    return QFileInfo::exists(envDPath(homeDir)) || QFileInfo::exists(uwsmEnvPath(homeDir));
}

bool install(QStringList *writtenFiles, QString *error) {
    if (!integrationSupported()) {
        if (error)
            *error = I18n::t(QStringLiteral("ssh.askpass_unsupported"));
        return false;
    }
    const QString homeDir = QDir::homePath();
    const QString executable = resolveAskpassCommand(homeDir, error);
    if (executable.isEmpty())
        return false;
    return install(homeDir, executable, detectUwsm(homeDir), true, writtenFiles, error);
}

bool install(const QString &homeDir, const QString &executablePath, bool includeUwsm,
             bool updateSystemd, QStringList *writtenFiles, QString *error) {
    QStringList written;

    const QString envD = envDPath(homeDir);
    const QString envDContent = QStringLiteral("# Managed by omapass\nSSH_ASKPASS=")
        + executablePath + QStringLiteral("\nSSH_ASKPASS_REQUIRE=prefer\n");
    if (!writeTextFile(envD, envDContent, {}, error))
        return false;
    written.append(envD);

    if (includeUwsm) {
        const QString uwsm = uwsmEnvPath(homeDir);
        const QString uwsmContent = QStringLiteral("# Managed by omapass\nexport SSH_ASKPASS=")
            + shellSingleQuote(executablePath)
            + QStringLiteral("\nexport SSH_ASKPASS_REQUIRE=prefer\n");
        if (!writeTextFile(uwsm, uwsmContent, {}, error))
            return false;
        written.append(uwsm);
    }

    if (updateSystemd) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            runProcess(systemctl,
                       {QStringLiteral("--user"), QStringLiteral("set-environment"),
                        QStringLiteral("SSH_ASKPASS=") + executablePath,
                        QStringLiteral("SSH_ASKPASS_REQUIRE=prefer")},
                       {}, {}, 2000);
        }
    }

    if (writtenFiles)
        *writtenFiles = written;
    return true;
}

bool uninstall(QStringList *removedFiles, QString *error) {
    return uninstall(QDir::homePath(), true, removedFiles, error);
}

bool uninstall(const QString &homeDir, bool updateSystemd, QStringList *removedFiles, QString *error) {
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

    if (!tryRemove(envDPath(homeDir)) || !tryRemove(uwsmEnvPath(homeDir)))
        return false;

    const QString wrapper = localWrapperPath(homeDir);
    if (QFileInfo::exists(wrapper)) {
        QFile file(wrapper);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)
            && file.read(256).contains("# Managed by omapass")) {
            file.close();
            if (!tryRemove(wrapper))
                return false;
        }
    }

    if (updateSystemd) {
        const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            runProcess(systemctl,
                       {QStringLiteral("--user"), QStringLiteral("unset-environment"),
                        QStringLiteral("SSH_ASKPASS"), QStringLiteral("SSH_ASKPASS_REQUIRE")},
                       {}, {}, 2000);
        }
    }

    if (removedFiles)
        *removedFiles = removed;
    return true;
}

bool isInvocation(const QStringList &args) {
    if (args.isEmpty())
        return false;
    return isAskpassProgramName(args.at(0)) || hasAskpassFlag(args);
}

Request parseRequest(const QStringList &args, const QString &promptTypeEnv) {
    Request request;
    const QString kind = promptTypeEnv.trimmed().toLower();
    if (kind == QLatin1String("confirm"))
        request.mode = Mode::Confirm;
    else if (kind == QLatin1String("none"))
        request.mode = Mode::Info;
    else
        request.mode = Mode::Passphrase;

    const int firstPromptArg = hasAskpassFlag(args) ? 2 : 1;
    if (args.size() > firstPromptArg)
        request.prompt = args.mid(firstPromptArg).join(QLatin1Char(' '));
    return request;
}

Controller::Controller(const Request &request, QObject *parent)
    : QObject(parent), m_request(request) {}

QString Controller::mode() const {
    switch (m_request.mode) {
    case Mode::Confirm:
        return QStringLiteral("confirm");
    case Mode::Info:
        return QStringLiteral("info");
    case Mode::Passphrase:
        return QStringLiteral("passphrase");
    }
    return QStringLiteral("passphrase");
}

void Controller::clearSecret() {
    m_secret.fill(QChar(u'\0'));
    m_secret.clear();
}

void Controller::accept(const QString &secret) {
    m_accepted = true;
    m_secret = secret;
    QGuiApplication::quit();
}

void Controller::reject() {
    m_accepted = false;
    clearSecret();
    QGuiApplication::quit();
}

#ifdef QT_QUICK_LIB
int run(int argc, char *argv[], const QStringList &args) {
    const AppConfig config = Config::load();
    I18n::load(config.language);

    const Request request = parseRequest(args, qEnvironmentVariable("SSH_ASKPASS_PROMPT"));

    QGuiApplication::setDesktopFileName(QStringLiteral("omapass"));

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omapass"));
    app.setOrganizationName(QStringLiteral("omapass"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omapass"),
                                       QIcon(QStringLiteral(":/omapass.png"))));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QQuickWindow::setDefaultAlphaBuffer(true);
#endif

    SystemTheme systemTheme(&app);
    Palette palette(config.themeOverrides, &app);
    palette.setSystemDarkMode(systemTheme.darkMode());
    HyprRounding rounding(&app);
    QObject::connect(&rounding, &HyprRounding::radiusChanged, &palette, &Palette::setRadius);
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &palette,
                     &Palette::setSystemDarkMode);

    const QFont interfaceFont = app.font();
    const qreal basePointSize = interfaceFont.pointSizeF() > 0 ? interfaceFont.pointSizeF() : 11.0;
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &app,
                     [applyInterfaceFont](qreal textScale) { applyInterfaceFont(textScale); });

    Controller controller(request, &app);
    Strings strings;

    SessionLockWatcher sessionLock(&app);
    QObject::connect(&sessionLock, &SessionLockWatcher::locked, &controller, &Controller::reject);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });

    engine.rootContext()->setContextProperty(QStringLiteral("askpass"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &palette);
    engine.rootContext()->setContextProperty(QStringLiteral("systemTheme"), &systemTheme);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &strings);

    engine.load(QUrl(QStringLiteral("qrc:/SshAskpassWindow.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the SSH askpass window; resource available:"
                    << QFile::exists(QStringLiteral(":/SshAskpassWindow.qml"));
        return 1;
    }

    app.exec();

    if (!controller.accepted())
        return 1;

    if (request.mode == Mode::Passphrase) {
        QByteArray utf8 = controller.secret().toUtf8();
        utf8.append('\n');
        const bool ok = std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout)
                            == static_cast<size_t>(utf8.size())
                        && std::fflush(stdout) == 0;
        utf8.fill('\0');
        controller.clearSecret();
        return ok ? 0 : 1;
    }

    return 0;
}
#endif

}
