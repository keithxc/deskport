// SPDX-License-Identifier: GPL-3.0-or-later
//
// Seamless Lab: a standalone debug client for the experimental Seamless mode.
// It runs the Linux sidecar through an existing SSH login and shows each
// remote top-level as a local native window. It does not read or modify any
// DeskPort/Moonlight settings, pairing data or services.

#include "seamlessclientsession.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QMutex>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <cmath>
#include <cstdio>

namespace {
QFile* logFile = nullptr;
QMutex logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    static const char* const levels[] = {"debug", "warning", "critical", "fatal", "info"};
    const char* level = type >= 0 && type <= 4 ? levels[type] : "log";
    const QByteArray line = QStringLiteral("%1 [%2] %3\n")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                     QString::fromLatin1(level), message)
                                .toUtf8();
    QMutexLocker locker(&logMutex);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    if (logFile) {
        logFile->write(line);
        logFile->flush();
    }
}

QString shellQuote(const QString& value)
{
    if (!value.isEmpty() &&
        value.indexOf(QRegularExpression(QStringLiteral("[^A-Za-z0-9_./:=@%+,-]"))) < 0)
        return value;
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}
} // namespace

int main(int argc, char* argv[])
{
    // Physical Control reaches the remote application as Control; Command
    // becomes the Linux Super/Meta key.
    QGuiApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
    QApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("SeamlessLab"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("seamlesslab.invalid"));
    QGuiApplication::setApplicationName(QStringLiteral("SeamlessLab"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0-m3"));
    application.setQuitOnLastWindowClosed(false);

    const QString logDir = QDir::home().filePath(
#ifdef Q_OS_MACOS
        QStringLiteral("Library/Logs/SeamlessLab")
#else
        QStringLiteral(".local/state/seamless-lab")
#endif
    );
    QDir().mkpath(logDir);
    QFile log(QDir(logDir).filePath(QStringLiteral("seamless-lab.log")));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        logFile = &log;
    qInstallMessageHandler(messageHandler);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Debug client for the experimental Seamless sidecar over SSH.\n"
        "Options given on the command line are remembered for later launches."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption hostOption(QStringLiteral("host"),
        QStringLiteral("SSH destination running the sidecar."), QStringLiteral("destination"));
    const QCommandLineOption serverOption(QStringLiteral("server"),
        QStringLiteral("Remote sidecar command (default: seamless-lab-server)."),
        QStringLiteral("command"));
    const QCommandLineOption scaleOption(QStringLiteral("scale"),
        QStringLiteral("Remote output scale (default: local screen ratio)."), QStringLiteral("n"));
    const QCommandLineOption sshOption(QStringLiteral("ssh"),
        QStringLiteral("Local ssh executable (default: /usr/bin/ssh)."), QStringLiteral("path"));
    parser.addOption(hostOption);
    parser.addOption(serverOption);
    parser.addOption(scaleOption);
    parser.addOption(sshOption);
    const QCommandLineOption launchOption(QStringLiteral("launch"),
        QStringLiteral("Start this launcher app ID once connected (repeatable; not remembered)."),
        QStringLiteral("id"));
    parser.addOption(launchOption);
    parser.addPositionalArgument(QStringLiteral("application"),
        QStringLiteral("Optional remote application argv started with the session; "
                       "otherwise use the launcher."), QStringLiteral("[-- app args...]"));
    parser.process(application);

    QSettings settings;
    const auto setting = [&](const QCommandLineOption& option, const QString& key,
                             const QString& fallback) {
        if (parser.isSet(option)) {
            settings.setValue(key, parser.value(option));
            return parser.value(option);
        }
        return settings.value(key, fallback).toString();
    };
    const QString host = setting(hostOption, QStringLiteral("host"), QString());
    const QString server = setting(serverOption, QStringLiteral("server"),
                                   QStringLiteral("seamless-lab-server"));
    const QString sshProgram = setting(sshOption, QStringLiteral("ssh"), QStringLiteral("/usr/bin/ssh"));
    // The application is per launch now; older builds remembered it.
    settings.remove(QStringLiteral("application"));
    const QStringList appArgv = parser.positionalArguments();
    int scale = qBound(1, static_cast<int>(std::lround(
                              application.primaryScreen() ? application.primaryScreen()->devicePixelRatio()
                                                          : 1.0)), 4);
    if (parser.isSet(scaleOption))
        scale = qBound(1, parser.value(scaleOption).toInt(), 4);

    if (host.isEmpty()) {
        qCritical("No SSH destination configured. Run once with --host user@machine.");
        return 2;
    }

    QStringList remote{server, QStringLiteral("--stream"), QStringLiteral("--scale"),
                       QString::number(scale)};
    if (!appArgv.isEmpty()) {
        remote.append(QStringLiteral("--"));
        for (const QString& argument : appArgv)
            remote.append(shellQuote(argument));
    }
    const QStringList sshArguments{
        QStringLiteral("-T"),
        QStringLiteral("-e"), QStringLiteral("none"),
        QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=10"),
        QStringLiteral("-o"), QStringLiteral("Compression=no"),
        host,
        QStringLiteral("--"),
        // The server command comes from local configuration and may rely on
        // remote shell expansion (for example ~/...); application arguments
        // are quoted.
        remote.join(QLatin1Char(' ')),
    };

    SeamlessClientSession session;

    // Launcher: the server advertises an allowlist of application IDs; the
    // client can only ask for one of them. Every launched application shows
    // its windows as separate native windows, so switching uses the local
    // window manager (Command-` or Mission Control on macOS).
    QWidget launcher;
    launcher.setWindowTitle(QStringLiteral("Seamless Lab — %1").arg(host));
    auto* layout = new QVBoxLayout(&launcher);
    auto* status = new QLabel(QStringLiteral("Connecting to %1…").arg(host));
    auto* apps = new QListWidget;
    layout->addWidget(status);
    layout->addWidget(apps);
    // A small fixed list; without a fixed size macOS opened it near full screen.
    launcher.setFixedSize(320, 260);
    QMenuBar menuBar;
    QMenu* appMenu = menuBar.addMenu(QStringLiteral("Apps"));
#ifdef Q_OS_MACOS
    QMenu dockMenu;
    dockMenu.setAsDockMenu();
#endif
    const auto launch = [&](const QString& id) {
        status->setText(QStringLiteral("Starting %1…").arg(id));
        session.launch(id);
    };
    QObject::connect(apps, &QListWidget::itemActivated, &launcher, [&](QListWidgetItem* item) {
        launch(item->data(Qt::UserRole).toString());
    });

    QObject::connect(&session, &SeamlessClientSession::ready, &application,
                     [&](const QJsonObject& hello) {
                         qInfo("Seamless session ready");
                         const QJsonArray ids = hello.value(QStringLiteral("apps")).toArray();
                         int index = 0;
                         for (const QJsonValue& value : ids) {
                             const QString id = value.toString().left(32);
                             if (id.isEmpty())
                                 continue;
                             auto* item = new QListWidgetItem(id, apps);
                             item->setData(Qt::UserRole, id);
                             QAction* action = appMenu->addAction(id, &launcher, [&, id] { launch(id); });
                             if (++index <= 9)
                                 action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | (Qt::Key_0 + index)));
#ifdef Q_OS_MACOS
                             dockMenu.addAction(id, &launcher, [&, id] { launch(id); });
#endif
                         }
                         status->setText(ids.isEmpty()
                                             ? QStringLiteral("Connected; the server has no launcher apps.")
                                             : QStringLiteral("Connected. Double-click an app to start it."));
                         if (!ids.isEmpty() || appArgv.isEmpty()) {
                             launcher.show();
                             launcher.raise();
                         }
                         for (const QString& id : parser.values(launchOption))
                             launch(id);
                     });
    QObject::connect(&session, &SeamlessClientSession::appEvent, &launcher,
                     [&](const QJsonObject& event) {
                         const QString type = event.value(QStringLiteral("type")).toString();
                         const QString app = event.value(QStringLiteral("app")).toString().left(32);
                         if (type == QStringLiteral("app-start"))
                             status->setText(QStringLiteral("%1 started").arg(app));
                         else if (type == QStringLiteral("app-exit"))
                             status->setText(QStringLiteral("%1 exited").arg(app));
                         else
                             status->setText(QStringLiteral("%1 rejected: %2").arg(
                                 app, event.value(QStringLiteral("reason")).toString().left(64)));
                     });
    QObject::connect(&session, &SeamlessClientSession::finished, &application,
                     [&](const QString& reason) {
                         const auto& stats = session.stats();
                         qInfo("Session ended: %s (frames=%llu rects=%llu encoded=%llu decoded=%llu)",
                               qPrintable(reason), static_cast<unsigned long long>(stats.frames),
                               static_cast<unsigned long long>(stats.rects),
                               static_cast<unsigned long long>(stats.encodedBytes),
                               static_cast<unsigned long long>(stats.decodedBytes));
                         QTimer::singleShot(0, &application, &QGuiApplication::quit);
                     });
    QObject::connect(&application, &QGuiApplication::aboutToQuit, &session,
                     [&session] { session.stop(); });

    session.start(sshProgram, sshArguments);
    const int code = application.exec();
    qInstallMessageHandler(nullptr);
    logFile = nullptr;
    return code;
}
