#include "backend/inputtestprofile.h"
int runCanvasSelfTest(int argc, char** argv);
// Qt owns the application entry point. This must precede every header that
// can include SDL, including shortcut and session headers.
#define SDL_MAIN_HANDLED

#include "backend/diagnostics.h"
#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "clipboard/agent.h"
#include <QTemporaryDir>
#include <QTimer>
#include <QNetworkReply>
#include <QTcpServer>
#include "backend/hostmanager.h"
#include "backend/browserhost.h"
#include "backend/multisessions.h"
#include "gui/sessioncanvas.h"
#include "backend/globalshortcut.h"
#include "streaming/shortcutstate.h"
#include "streaming/canvasbridge.h"
#include "backend/trafficbudget.h"
#include "backend/sessionworker.h"
#include "backend/peermanager.h"
#include "backend/singleinstance.h"
#include "cli/hostcontrol.h"
#include "cli/hostsetup.h"
#include "cli/hostdaemon.h"
#include "version.h"
#ifdef Q_OS_MACOS
#include "backend/macdock.h"
#include "backend/mactitlebar.h"
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#ifdef Q_OS_LINUX
#include <QVulkanInstance>
#endif
#include <QMutex>
#include <QtDebug>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QStyleHints>
#include <QFont>
#include <QCursor>
#include <QElapsedTimer>
#include <QTemporaryFile>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QProcess>
#include <QDir>
#include <QFile>
#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "backend/sleepmonitor.h"
#ifdef Q_OS_LINUX
#include "backend/serviceconfig.h"
#endif

#ifdef Q_OS_UNIX
static int s_TerminationSocket[2] = {-1, -1};
static void forwardTerminationSignal(int)
{
    const char byte = 1;
    (void)!write(s_TerminationSocket[1], &byte, 1);
}
#endif

#include <SDL.h>

#ifdef HAVE_FFMPEG
#include "streaming/video/ffmpeg.h"
#endif

#if defined(Q_OS_WIN32) && defined(HAVE_ANTIHOOKING)
#include "antihookingprotection.h"
#endif

#if defined(Q_OS_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwmapi.h>
#elif defined(Q_OS_LINUX)
#include <openssl/ssl.h>
#endif

#include "cli/listapps.h"
#include "cli/quitstream.h"
#include "cli/startstream.h"
#include "cli/pair.h"
#include "cli/commandlineparser.h"
#include "path.h"
#include "utils.h"
#include "gui/computermodel.h"
#include "gui/manual.h"
#include "gui/appmodel.h"
#include "backend/autoupdatechecker.h"
#include "backend/computermanager.h"
#include "backend/systemproperties.h"
#include "streaming/session.h"
#include "streaming/windowactivation.h"
#include "settings/streamingpreferences.h"
#include "gui/sdlgamepadkeynavigation.h"

#ifdef Q_OS_WIN
// A resident window may treat ordinary close/quit as hide. Restart Manager's
// confirmed shutdown must instead take the same cleanup path as tray Quit.
class WindowsInstallShutdown final : public QAbstractNativeEventFilter {
public:
    explicit WindowsInstallShutdown(HostManager& host) : m_Host(host) {}
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
        auto msg = static_cast<MSG*>(message);
        if (!(msg->lParam & ENDSESSION_CLOSEAPP)) return false;
        if (msg->message == WM_QUERYENDSESSION) {
            *result = TRUE;
            return true;
        }
        if (msg->message == WM_ENDSESSION) {
            if (msg->wParam) m_Host.requestExit();
            *result = 0;
            return true;
        }
        return false;
    }
private:
    HostManager& m_Host;
};

// Windows defaults every Win32 title bar to light colors for compatibility.
// DWMWA_USE_IMMERSIVE_DARK_MODE is documented by Microsoft as attribute 20;
// older SDK headers may omit the enum, while DwmSetWindowAttribute simply
// rejects it on unsupported Windows versions.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
static void updateWindowsTitleBar(QWindow* window, bool dark)
{
    if (!window) return;
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()),
                          DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}
#endif

#if defined(Q_OS_WIN32)
#define IS_UNSPECIFIED_HANDLE(x) ((x) == INVALID_HANDLE_VALUE || (x) == NULL)

#endif

static QElapsedTimer s_LoggerTime;
static bool s_SuppressVerboseOutput;
void logToLoggerStream(QString& message)
{
    Diagnostics::instance().record("client", message);

    // Explicit troubleshooting opt-in only, like DESKPORT_HOST_LOG: a private
    // file receives raw client text (addresses and paths included), capped at
    // 32 MiB. Session workers inherit it, so each line names its process.
    static const QString path = qEnvironmentVariable("DESKPORT_CLIENT_LOG");
    if (path.isEmpty()) return;
    static QMutex lock;
    QMutexLocker locker(&lock);
    static QFile* file = nullptr;
    if (!file) {
        file = new QFile(path);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append) ||
            !file->setPermissions(QFile::ReadOwner | QFile::WriteOwner)) { file->close(); return; }
    }
    if (!file->isOpen() || file->size() > 32 * 1024 * 1024) return;
    file->write(QByteArray::number(QCoreApplication::applicationPid()) + " " + message.toUtf8());
    file->flush();
}

void sdlLogToDiskHandler(void*, int category, SDL_LogPriority priority, const char* message)
{
    QString priorityTxt;

    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Verbose";
        break;
    case SDL_LOG_PRIORITY_DEBUG:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Debug";
        break;
    case SDL_LOG_PRIORITY_INFO:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Info";
        break;
    case SDL_LOG_PRIORITY_WARN:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Warn";
        break;
    case SDL_LOG_PRIORITY_ERROR:
        priorityTxt = "Error";
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        priorityTxt = "Critical";
        break;
    default:
        priorityTxt = "Unknown";
        break;
    }

    QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
    QString txt = QString("%1 - SDL %2 (%3): %4\n").arg(logTime.toString()).arg(priorityTxt).arg(category).arg(message);

    logToLoggerStream(txt);
}

void qtLogToDiskHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    QString typeTxt;

    switch (type) {
    case QtDebugMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Debug";
        break;
    case QtInfoMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Info";
        break;
    case QtWarningMsg:
        if (s_SuppressVerboseOutput) {
            return;
        }
        typeTxt = "Warning";
        break;
    case QtCriticalMsg:
        typeTxt = "Critical";
        break;
    case QtFatalMsg:
        typeTxt = "Fatal";
        break;
    }

    QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
    QString txt = QString("%1 - Qt %2: %3\n").arg(logTime.toString()).arg(typeTxt).arg(msg);

    logToLoggerStream(txt);
}

#ifdef HAVE_FFMPEG

void ffmpegLogToDiskHandler(void* ptr, int level, const char* fmt, va_list vl)
{
    char lineBuffer[1024];
    static int printPrefix = 1;

    if ((level & 0xFF) > av_log_get_level()) {
        return;
    }
    else if ((level & 0xFF) > AV_LOG_WARNING && s_SuppressVerboseOutput) {
        return;
    }

    // We need to use the *previous* printPrefix value to determine whether to
    // print the prefix this time. av_log_format_line() will set the printPrefix
    // value to indicate whether the prefix should be printed *next time*.
    bool shouldPrefixThisMessage = printPrefix != 0;

    av_log_format_line(ptr, level, fmt, vl, lineBuffer, sizeof(lineBuffer), &printPrefix);

    if (shouldPrefixThisMessage) {
        QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
        QString txt = QString("%1 - FFmpeg: %2").arg(logTime.toString()).arg(lineBuffer);
        logToLoggerStream(txt);
    }
    else {
        QString txt = QString(lineBuffer);
        logToLoggerStream(txt);
    }
}

#endif

// Both service managers we install deliberately leave a clean quit alone
// (launchd's KeepAlive.SuccessfulExit=false, systemd's Restart=on-failure), so a
// restart is handed to a detached shell that waits for this process to
// disappear -- releasing the single instance lock and the host's ports -- and
// only then starts DeskPort again. The relaunch runs the binary that is on disk
// now, which is the point: an upgrade cannot reach the running process.
static void relaunchAfterExit()
{
#ifdef Q_OS_UNIX
    const auto quote = [](const QString& value) -> QString {
        QString quoted = value; quoted.replace("'", "'\\''"); return "'" + quoted + "'";
    };
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) arguments.removeFirst();
    QString options;
    for (const QString& argument : arguments) options += " " + quote(argument);

    QString relaunch;
#ifdef Q_OS_MACOS
    const QString label = QStringLiteral("io.github.keithxc.DeskPort");
    // Prefer the login agent, so the new process stays launchd-managed.
    if (QFile::exists(QDir::homePath() + "/Library/LaunchAgents/" + label + ".plist"))
        relaunch = QString("launchctl kickstart gui/%1/%2 || ").arg(getuid()).arg(label);
    const QString bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
    relaunch += "open -n -a " + quote(bundle);
    if (!options.isEmpty()) relaunch += " --args" + options;
#else
    if (QFile::exists(DeskPortService::unitPath()))
        relaunch = "systemctl --user restart io.github.keithxc.DeskPort.service || ";
    relaunch += quote(QCoreApplication::applicationFilePath()) + options;
#endif
    const QString command = QString("while kill -0 %1 2>/dev/null; do sleep 0.2; done; %2")
            .arg(QCoreApplication::applicationPid()).arg(relaunch);
    if (!QProcess::startDetached("/bin/sh", {"-c", command}))
        qCritical() << "Could not schedule the restart";
#elif defined(Q_OS_WIN)
    QStringList arguments{"relaunch", QString::number(QCoreApplication::applicationPid()), QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
    arguments.append(QCoreApplication::arguments().mid(1));
    if (!QProcess::startDetached(QCoreApplication::applicationDirPath() + "/deskport-maintenance.exe", arguments))
        qCritical() << "Could not schedule the Windows restart";
#else
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
#endif
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // Qt 6.4's Wayland plugin defaults to the unversioned development library.
    // Installed systems and portable packages provide the runtime SONAME.
    if (!qEnvironmentVariableIsSet("QT_VULKAN_LIB"))
        qputenv("QT_VULKAN_LIB", "libvulkan.so.1");
#endif
    if (argc == 2 && QByteArray(argv[1]) == "--canvas-self-test") return runCanvasSelfTest(argc, argv);
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--clipboard-helper") return runClipboardHelper(argc, argv);
    }
    bool isolatedSessionTest = false;
    for (int i = 1; i < argc; ++i) isolatedSessionTest |= QByteArray(argv[i]) == "--host-session-test";
    if (isolatedSessionTest) {
        QFile marker(QDir::currentPath()+"/SESSION_TEST_ONLY");
        if (!marker.open(QIODevice::ReadOnly) || marker.readAll() != "DeskPort isolated session test\n" ||
            !QFile::exists("portable.dat")) {
            fprintf(stderr, "Use scripts/run-isolated-session-host.sh with a disposable test directory.\n");
            return 2;
        }
    }
    SDL_SetMainReady();

    // Set the app version for the QCommandLineParser's showVersion() command
    QCoreApplication::setApplicationVersion(VERSION_STR);

    // Set these here to allow us to use the default QSettings constructor.
    // These also ensure that our cache directory is named correctly. As such,
    // it is critical that these be called before Path::initialize().
    QCoreApplication::setOrganizationName("DeskPort");
    QCoreApplication::setOrganizationDomain("deskport.keithxc.github.io");
    QCoreApplication::setApplicationName(isolatedSessionTest ? "DeskPortSessionTest" : "DeskPort");

    const auto inputTestProfile = InputTestProfile::root();
    if (!inputTestProfile.isEmpty()) {
        if (!InputTestProfile::valid() || !QDir::setCurrent(inputTestProfile)) {
            qCritical() << "Input test profile requires an absolute directory and .deskport-input-test marker";
            return 2;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, inputTestProfile + "/config");
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, inputTestProfile + "/config");
    }

    QStringList commandArguments;
    for (int i = 0; i < argc; ++i) commandArguments.append(QString::fromLocal8Bit(argv[i]));
    if (!inputTestProfile.isEmpty() &&
        (commandArguments.value(1) == "host" || DeskPortCli::isSetupCommand(commandArguments))) {
        qCritical() << "Input test profiles are client-only";
        return 2;
    }
    const bool basicQuery = argc == 2 && (commandArguments[1] == "--version" || commandArguments[1] == "-v" ||
                                        commandArguments[1] == "--help" || commandArguments[1] == "-h");
    const int optionEnd = commandArguments.indexOf(QStringLiteral("--"));
    const QStringList hostOptions = optionEnd < 0 ? commandArguments : commandArguments.mid(0, optionEnd);
    const bool hostHelp = commandArguments.value(1) == "host" &&
        (hostOptions.contains("--help") || hostOptions.contains("-h"));
    if (basicQuery || hostHelp || DeskPortCli::isControlCommand(commandArguments) || DeskPortCli::isSetupCommand(commandArguments)) {
#ifdef Q_OS_WIN
        const HANDLE cliOut = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE cliErr = GetStdHandle(STD_ERROR_HANDLE);
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (!cliOut || cliOut == INVALID_HANDLE_VALUE)
                freopen("CONOUT$", "w", stdout);
            if (!cliErr || cliErr == INVALID_HANDLE_VALUE)
                freopen("CONOUT$", "w", stderr);
        }
#endif
        QCoreApplication cli(argc, argv);
        if (hostHelp) {
            fputs("Usage: deskport host run [--mode desktop|seamless] [--no-share] [--no-web-server] [-- application [arguments...]]\n"
                  "Run the Linux host in the foreground without a window.\n"
                  "Desktop is the default and retains the existing full-display behavior.\n"
                  "Seamless is experimental, Linux-only, and currently provides local compositor lifecycle/capture plumbing.\n"
                  "--no-share starts management with sharing disabled.\n"
                  "--no-web-server disables browser HTTPS access for this run.\n"
                  "Desktop Mode requires an existing compositor, capture, input, audio and encoder.\n"
                  "An application after -- is launched only inside the Seamless compositor.\n"
                  "Use deskport doctor to inspect prerequisites.\n", stdout);
            return 0;
        }
        if (DeskPortCli::isControlCommand(commandArguments)) return DeskPortCli::runControlCommand(commandArguments);
        if (DeskPortCli::isSetupCommand(commandArguments)) return DeskPortCli::runSetupCommand(commandArguments);
        GlobalCommandLineParser parser;
        parser.parse(cli.arguments());
        return 0;
    }

    if (!inputTestProfile.isEmpty()) Path::initialize(true);
    else if (QFile(QDir::currentPath() + "/portable.dat").exists()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::currentPath());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, QDir::currentPath());

        // Initialize paths for portable mode
        Path::initialize(true);
    }
    else {
        // Initialize paths for standard installation
        Path::initialize(false);
    }

    if (commandArguments.value(1) == "host") return DeskPortCli::runHostDaemon(argc, argv);

    // Override the default QML cache directory with the one we chose
    if (qEnvironmentVariableIsEmpty("QML_DISK_CACHE_PATH")) {
        qputenv("QML_DISK_CACHE_PATH", Path::getQmlCacheDir().toUtf8());
    }

#ifdef Q_OS_WIN32
    // Grab the original std handles before we potentially redirect them later
    HANDLE oldConOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE oldConErr = GetStdHandle(STD_ERROR_HANDLE);
#endif

    Diagnostics::instance(); // Initialize before installing a handler that uses it.
    s_LoggerTime.start();
    qInstallMessageHandler(qtLogToDiskHandler);
    SDL_LogSetOutputFunction(sdlLogToDiskHandler, nullptr);

#ifdef HAVE_FFMPEG
    av_log_set_callback(ffmpegLogToDiskHandler);
#endif

    // Memory dumps may contain credentials or private input. Do not create them.

#if defined(Q_OS_WIN32) && defined(HAVE_ANTIHOOKING)
    // Force AntiHooking.dll to be statically imported and loaded
    // by ntdll on Win32 platforms by calling a dummy function.
    AntiHookingDummyImport();
#elif defined(Q_OS_LINUX)
    // Force libssl.so to be directly linked to our binary, so
    // linuxdeployqt can find it and include it in our AppImage.
    // QtNetwork will pull it in via dlopen().
    SSL_free(nullptr);
#endif

    // We keep this at function scope to ensure it stays around while we're running,
    // becaue the Qt QPA will need to read it. Since the temporary file is only
    // created when open() is called, this doesn't do any harm for other platforms.
    QTemporaryFile eglfsConfigFile("eglfs_override_XXXXXX.conf");

    // Avoid using High DPI on EGLFS. It breaks font rendering.
    // https://bugreports.qt.io/browse/QTBUG-64377
    //
    // NB: We can't use QGuiApplication::platformName() here because it is only
    // set once the QGuiApplication is created, which is too late to enable High DPI :(
    if (WMUtils::isRunningWindowManager()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // Enable High DPI support on Qt 5.x. It is always enabled on Qt 6.0
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        // Enable fractional High DPI scaling on Qt 5.14 and later
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    }
    else {
#ifndef STEAM_LINK
        if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
            qInfo() << "Unable to detect Wayland or X11, so EGLFS will be used by default. Set QT_QPA_PLATFORM to override this.";
            qputenv("QT_QPA_PLATFORM", "eglfs");

            if (!qEnvironmentVariableIsSet("QT_QPA_EGLFS_ALWAYS_SET_MODE")) {
                qInfo() << "Setting display mode by default. Set QT_QPA_EGLFS_ALWAYS_SET_MODE=0 to override this.";

                // The UI doesn't appear on RetroPie without this option.
                qputenv("QT_QPA_EGLFS_ALWAYS_SET_MODE", "1");
            }

            if (!QFile("/dev/dri").exists()) {
                qWarning() << "Unable to find a KMSDRM display device!";
                qWarning() << "On the Raspberry Pi, you must enable the 'fake KMS' driver in raspi-config to use Moonlight outside of the GUI environment.";
            }
            else if (!qEnvironmentVariableIsSet("QT_QPA_EGLFS_KMS_CONFIG")) {
                // HACK: Remove this when Qt is fixed to properly check for display support before picking a card
                QString cardOverride = WMUtils::getDrmCardOverride();
                if (!cardOverride.isEmpty()) {
                    if (eglfsConfigFile.open()) {
                        qInfo() << "Overriding default Qt EGLFS card selection to" << cardOverride;
                        QTextStream(&eglfsConfigFile) << "{ \"device\": \"" << cardOverride << "\" }";
                        qputenv("QT_QPA_EGLFS_KMS_CONFIG", eglfsConfigFile.fileName().toUtf8());
                    }
                }
            }
        }

        // EGLFS uses OpenGLES 2.0, so we will too. Some embedded platforms may not
        // even have working OpenGL implementations, so GLES is the only option.
        // See https://github.com/moonlight-stream/moonlight-qt/issues/868
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
    }

#if !defined(Q_PROCESSOR_X86) && defined(SDL_HINT_VIDEO_X11_FORCE_EGL)
    // Some ARM and RISC-V embedded devices don't have working GLX which can cause
    // SDL to fail to find a working OpenGL implementation at all. Let's force EGL
    // on non-x86 platforms, since GLX is deprecated anyway.
    SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "1");
#endif

#ifdef Q_OS_MACOS
    // This avoids using the default keychain for SSL, which may cause
    // password prompts on macOS.
    qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");
#endif

#if defined(Q_OS_WIN32) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (!qEnvironmentVariableIsSet("QT_OPENGL")) {
        // On Windows, use ANGLE so we don't have to load OpenGL
        // user-mode drivers into our app. OGL drivers (especially Intel)
        // seem to crash Moonlight far more often than DirectX.
        qputenv("QT_OPENGL", "angle");
    }
#endif

#if !defined(Q_OS_WIN32) || QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Moonlight requires the non-threaded renderer because we depend
    // on being able to control the render thread by blocking in the
    // main thread (and pumping events from the main thread when needed).
    // That doesn't work with the threaded renderer which causes all
    // sorts of odd behavior depending on the platform.
    //
    // NB: Windows defaults to the "windows" non-threaded render loop on
    // Qt 5 and the threaded render loop on Qt 6.
    //
    // DeskPort: only the command-line stream mode still runs a session inside
    // the Qt UI process. The device shell restores Qt's default below, once the
    // command line is known, so a blocking swap cannot stall its event loop.
    const bool renderLoopOverridden = qEnvironmentVariableIsSet("QSG_RENDER_LOOP");
    if (!renderLoopOverridden) qputenv("QSG_RENDER_LOOP", "basic");
#else
    const bool renderLoopOverridden = true;
#endif

#if defined(Q_OS_DARWIN) && defined(QT_DEBUG)
    // Enable Metal valiation for debug builds
    qputenv("MTL_DEBUG_LAYER", "1");
    qputenv("MTL_SHADER_VALIDATION", "1");
#endif

    // We don't want system proxies to apply to us
    QNetworkProxyFactory::setUseSystemConfiguration(false);

    // Clear any default application proxy
    QNetworkProxy noProxy(QNetworkProxy::NoProxy);
    QNetworkProxy::setApplicationProxy(noProxy);

    // Register custom metatypes for use in signals
    qRegisterMetaType<NvApp>("NvApp");

    // Allow the display to sleep by default. We will manually use SDL_DisableScreenSaver()
    // and SDL_EnableScreenSaver() when appropriate. This hint must be set before
    // initializing the SDL video subsystem to have any effect.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    // We use MMAL to render on Raspberry Pi, so we do not require DRM master.
    SDL_SetHint("SDL_KMSDRM_REQUIRE_DRM_MASTER", "0");

    // Use Direct3D 9Ex to avoid a deadlock caused by the D3D device being reset when
    // the user triggers a UAC prompt. This option controls the software/SDL renderer.
    // The DXVA2 renderer uses Direct3D 9Ex itself directly.
    SDL_SetHint("SDL_WINDOWS_USE_D3D9EX", "1");

    // SDL would turn SIGTERM/SIGINT into SDL_QUIT, which only hides a session
    // window and is never read outside one. main() handles them instead.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_TIMER) failed: %s",
                     SDL_GetError());
        return -1;
    }

#ifdef STEAM_LINK
    // Steam Link requires that we initialize video before creating our
    // QGuiApplication in order to configure the framebuffer correctly.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return -1;
    }
#endif

    // Use atexit() to ensure SDL_Quit() is called. This avoids
    // racing with object destruction where SDL may be used.
    atexit(SDL_Quit);

    // Avoid the default behavior of changing the timer resolution to 1 ms.
    // We don't want this all the time that Moonlight is open. We will set
    // it manually when we start streaming.
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

    // Disable minimize on focus loss by default. Users seem to want this off by default.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    // SDL 2.0.12 changes the default behavior to use the button label rather than the button
    // position as most other software does. Set this back to 0 to stay consistent with prior
    // releases of Moonlight.
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");

    // Disable relative mouse scaling to renderer size or logical DPI. We want to send
    // the mouse motion exactly how it was given to us.
    SDL_SetHint("SDL_MOUSE_RELATIVE_SCALING", "0");

    // Set our app name for SDL to use with PulseAudio and PipeWire. This matches what we
    // provide as our app name to libsoundio too. On SDL 2.0.18+, SDL_APP_NAME is also used
    // for screensaver inhibitor reporting.
    SDL_SetHint("SDL_AUDIO_DEVICE_APP_NAME", "DeskPort");
    SDL_SetHint("SDL_APP_NAME", "DeskPort");

    // We handle capturing the mouse ourselves when it leaves the window, so we don't need
    // SDL doing it for us behind our backs.
    SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0");

    // SDL will try to lock the mouse cursor on Wayland if it's not visible in order to
    // support applications that assume they can warp the cursor (which isn't possible
    // on Wayland). We don't want this behavior because it interferes with seamless mouse
    // mode when toggling between windowed and fullscreen modes by unexpectedly locking
    // the mouse cursor.
    SDL_SetHint("SDL_VIDEO_WAYLAND_EMULATE_MOUSE_WARP", "0");

#ifdef QT_DEBUG
    // Allow thread naming using exceptions on debug builds. SDL doesn't use SEH
    // when throwing the exceptions, so we don't enable it for release builds out
    // of caution.
    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "0");
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // --version and --help are answered by the command line parser, which only
    // runs once QApplication exists. Qt aborts when it cannot load a platform
    // plugin, so those queries died over SSH. Answer them offscreen instead.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY") &&
            !qEnvironmentVariableIsSet("DISPLAY")) {
        for (int i = 1; i < argc; i++) {
            const QByteArray argument(argv[i]);
            if (argument == "--version" || argument == "-v" || argument == "--help" || argument == "-h") {
                qputenv("QT_QPA_PLATFORM", "offscreen");
                break;
            }
        }
    }
#endif

    QApplication app(argc, argv);
    Diagnostics::instance().startMaintenance();
    if (isolatedSessionTest) {
        QFile certFile("test-cert.pem"), keyFile("test-key.pem");
        if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) return 2;
        const auto cert = certFile.readAll(), key = keyFile.readAll();
        if (QSslCertificate(cert).isNull() || QSslKey(key, QSsl::Rsa).isNull()) return 2;
        QLockFile testLock(QDir::currentPath()+"/session-test.lock");
        if (!testLock.tryLock(0)) return 2;
        HostManager host(nullptr, QDir::currentPath()+"/host");
        const int testBindingPort = qEnvironmentVariableIntValue("DESKPORT_TEST_BINDING_PORT");
        if (testBindingPort && (testBindingPort < 1024 || testBindingPort > 65535)) return 2;
        PeerManager peers(&host, cert, key, QDir::currentPath()+"/binding", quint16(testBindingPort));
        BrowserHost browserHost(&host, nullptr, QDir::currentPath()+"/browser");
        browserHost.start();
        QDialog window;
        window.setWindowTitle("DeskPort temporary session test");
        auto layout = new QVBoxLayout(&window);
        auto status = new QLabel(&window);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto pending = new QLabel(&window);
        pending->setWordWrap(true);
        auto approve = new QPushButton("Approve this device", &window);
        auto reject = new QPushButton("Reject this device", &window);
        auto stop = new QPushButton("Stop temporary host", &window);
        layout->addWidget(status); layout->addWidget(pending);
        layout->addWidget(approve); layout->addWidget(reject); layout->addWidget(stop);
        bool changingTestPort = false;
        auto refresh = [&] {
            if (!testBindingPort && !changingTestPort && host.basePort() != DeskPortNetwork::DefaultBasePort && peers.port() != host.basePort()+2) {
                changingTestPort = true;
                peers.setConnectionPort(host.basePort()+2);
                changingTestPort = false;
            }
            status->setText(QString("Isolated host; extended workspace only; expires after 15 minutes.\nBinding port: %1   Video base port: %2\n%3\n%4")
                            .arg(peers.port()).arg(host.basePort()).arg(host.status(), peers.status()));
            pending->setText(peers.pendingName());
            if (browserHost.listening())
                pending->setText(pending->text() + "\nBrowser: " + browserHost.urls().join("  ") +
                                 "\nAccess code: " + browserHost.accessCode());
            approve->setEnabled(!peers.requestId().isEmpty()); reject->setEnabled(!peers.requestId().isEmpty());
        };
        QObject::connect(&peers, &PeerManager::changed, &window, refresh);
        QObject::connect(&host, &HostManager::changed, &window, refresh);
        QObject::connect(&browserHost, &BrowserHost::changed, &window, refresh);
        QObject::connect(approve, &QPushButton::clicked, &window, [&] { peers.approve(peers.requestId()); });
        QObject::connect(reject, &QPushButton::clicked, &window, [&] { peers.reject(peers.requestId()); });
        QObject::connect(stop, &QPushButton::clicked, &app, &QApplication::quit);
        QObject::connect(&app, &QApplication::aboutToQuit, &host, &HostManager::stop);
        QTimer::singleShot(15 * 60 * 1000, &app, &QApplication::quit);
        app.setQuitOnLastWindowClosed(true);
        refresh(); window.show();
        QTimer::singleShot(0, &host, [&host] { host.start(2560, 1440); });
        return app.exec();
    }
    if (app.arguments().contains("--host-self-test")) {
        QTemporaryDir directory;
        if (!directory.isValid()) return 1;
        HostManager host(nullptr, directory.path());
        if (!host.available()) return 3;
        QNetworkAccessManager network;
        network.setProxy(QNetworkProxy::NoProxy);
        QObject::connect(&host, &HostManager::changed, &app, [&host] {
            fprintf(stderr, "%s\n", qPrintable(host.status()));
        });
        QTimer::singleShot(0, &app, [&host] { host.start(2560, 1440); });
        QTimer::singleShot(8000, &app, [&] {
            if (!host.canPair()) { host.stop(); app.exit(1); return; }
            auto reply = network.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/serverinfo").arg(host.basePort()))));
            QObject::connect(reply, &QNetworkReply::finished, &app, [&, reply] {
                const bool ready = reply->error() == QNetworkReply::NoError && reply->readAll().contains("<hostname>DeskPort</hostname>");
                fprintf(stderr, "%s: bundled host and native virtual display startup\n", ready ? "PASS" : "FAIL");
                host.stop(); app.exit(ready ? 0 : 1); reply->deleteLater();
            });
        });
        QTimer::singleShot(15000, &app, [&] { host.stop(); app.exit(1); });
        return app.exec();
    }


#ifndef STEAM_LINK
    // Force use of the KMSDRM backend for SDL when using Qt platform plugins
    // that directly draw to the display without a windowing system.
    if (QGuiApplication::platformName() == "eglfs" || QGuiApplication::platformName() == "linuxfb") {
        qputenv("SDL_VIDEODRIVER", "kmsdrm");
    }
#endif

    // Every worker also subscribes: shell IPC is not the only input safety path.
    ScreenLockMonitor screenLock;
    SleepMonitor sleepMonitor;
    QObject::connect(&screenLock, &ScreenLockMonitor::changed, &app, [](bool locked) {
        if (locked && Session::get()) Session::get()->releaseForClientLock();
    });
    QTimer sleepReleaseTimer;
    QElapsedTimer sleepWait;
    QObject::connect(&sleepReleaseTimer, &QTimer::timeout, &app, [&] {
        if (Session::get() && Session::get()->inputDispatching() && sleepWait.elapsed() < 3000) return;
        sleepReleaseTimer.stop(); sleepMonitor.release();
    });
    // Cancel the old deadline on wake; it must never close the newly acquired
    // awake inhibitor or interfere with a subsequent quick suspend.
    QObject::connect(&sleepMonitor, &SleepMonitor::resumed, &sleepReleaseTimer, &QTimer::stop);
    QObject::connect(&sleepMonitor, &SleepMonitor::sleeping, &app, [&] {
        if (Session::get()) Session::get()->endForSystemSleep();
        sleepWait.start(); sleepReleaseTimer.start(20);
    });
    if (app.arguments().value(1) == "--session-worker") {
        const auto endpoint = qEnvironmentVariable("DESKPORT_SESSION_ENDPOINT");
        const auto token = qEnvironmentVariable("DESKPORT_SESSION_TOKEN");
        CanvasBridge::setVideoTransport(qEnvironmentVariable("DESKPORT_VIDEO_ENDPOINT"), token);
        qunsetenv("DESKPORT_SESSION_ENDPOINT"); qunsetenv("DESKPORT_SESSION_TOKEN");
        if (endpoint.isEmpty() || token.isEmpty() || app.arguments().size() != 4) return 2;
        app.setQuitOnLastWindowClosed(false);
        SessionWorker worker(endpoint, token, app.arguments().at(2), app.arguments().at(3));
        return worker.run();
    }

    GlobalCommandLineParser parser;
    GlobalCommandLineParser::ParseResult commandLineParserResult = parser.parse([&app] { auto args = app.arguments(); args.removeAll("--background"); args.removeAll("--share"); args.removeAll("--no-host-autostart"); args.removeAll("--no-web-server"); return args; }());
    SingleInstance instance;
    bool pendingActivation = false;
    instance.activate = [&] { pendingActivation = true; };
    if ((commandLineParserResult == GlobalCommandLineParser::NormalStartRequested ||
         commandLineParserResult == GlobalCommandLineParser::StreamRequested) && !instance.start(inputTestProfile, !app.arguments().contains("--background"))) {
        if (!instance.delivered) qWarning() << "DeskPort is already starting or its activation endpoint is unavailable";
        return instance.delivered ? 0 : 1;
    }
    switch (commandLineParserResult) {
    case GlobalCommandLineParser::ListRequested:
        // Don't log to the console since it will jumble the command output
        s_SuppressVerboseOutput = true;
#ifdef Q_OS_WIN32
        // If we don't have stdout or stderr handles (which will normally be the case
        // since we're a /SUBSYSTEM:WINDOWS app), attach to our parent console and use
        // that for stdout and stderr.
        //
        // If we do have stdout or stderr handles, that means the user has used standard
        // handle redirection. In that case, we don't want to override those handles.
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            // If we didn't have an old stdout/stderr handle, use the new CONOUT$ handle
            if (IS_UNSPECIFIED_HANDLE(oldConOut)) {
                freopen("CONOUT$", "w", stdout);
                setvbuf(stdout, NULL, _IONBF, 0);
            }
            if (IS_UNSPECIFIED_HANDLE(oldConErr)) {
                freopen("CONOUT$", "w", stderr);
                setvbuf(stderr, NULL, _IONBF, 0);
            }
        }
#endif
        break;
    default:
        break;
    }

    SDL_version compileVersion;
    SDL_VERSION(&compileVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Compiled with SDL %d.%d.%d",
                compileVersion.major, compileVersion.minor, compileVersion.patch);

    SDL_version runtimeVersion;
    SDL_GetVersion(&runtimeVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Running with SDL %d.%d.%d",
                runtimeVersion.major, runtimeVersion.minor, runtimeVersion.patch);

    // Apply the initial translation based on user preference
    StreamingPreferences::get()->retranslate();

    // Trickily declare the translation for dialog buttons
    QCoreApplication::translate("QPlatformTheme", "&Yes");
    QCoreApplication::translate("QPlatformTheme", "&No");
    QCoreApplication::translate("QPlatformTheme", "OK");
    QCoreApplication::translate("QPlatformTheme", "Help");
    QCoreApplication::translate("QPlatformTheme", "Cancel");

    // After the QGuiApplication is created, the platform stuff will be initialized
    // and we can set the SDL video driver to match Qt.
    if (WMUtils::isRunningWayland() && QGuiApplication::platformName() == "xcb") {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected XWayland. This will probably break hardware decoding! Try running with QT_QPA_PLATFORM=wayland or switch to X11.");
        qputenv("SDL_VIDEODRIVER", "x11");
    }
    else if (QGuiApplication::platformName().startsWith("wayland")) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Detected Wayland");
        qputenv("SDL_VIDEODRIVER", "wayland");
    }

#ifdef STEAM_LINK
    // Qt 5.9 from the Steam Link SDK is not able to load any fonts
    // since the Steam Link doesn't include any of the ones it looks
    // for. We know it has NotoSans so we will explicitly ask for that.
    if (app.font().family().isEmpty()) {
        qWarning() << "SL HACK: No default font - using NotoSans";

        QFont fon("NotoSans");
        app.setFont(fon);
    }

    // Move the mouse to the bottom right so it's invisible when using
    // gamepad-only navigation.
    QCursor().setPos(0xFFFF, 0xFFFF);
#elif !SDL_VERSION_ATLEAST(2, 0, 11) && defined(Q_OS_LINUX) && (defined(__arm__) || defined(__aarch64__))
    if (qgetenv("SDL_VIDEO_GL_DRIVER").isEmpty() && QGuiApplication::platformName() == "eglfs") {
        // Look for Raspberry Pi GLES libraries. SDL 2.0.10 and earlier needs some help finding
        // the correct libraries for the KMSDRM backend if not compiled with the RPI backend enabled.
        if (SDL_LoadObject("libbrcmGLESv2.so") != nullptr) {
            qputenv("SDL_VIDEO_GL_DRIVER", "libbrcmGLESv2.so");
        }
        else if (SDL_LoadObject("/opt/vc/lib/libbrcmGLESv2.so") != nullptr) {
            qputenv("SDL_VIDEO_GL_DRIVER", "/opt/vc/lib/libbrcmGLESv2.so");
        }
    }
#endif

#ifndef Q_OS_DARWIN
    // Keep the native layered macOS icon and its system-selected appearance.
    app.setWindowIcon(QIcon(":/res/deskport.svg"));
#endif
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    // Every icon is a bundled resource. Qt Quick still consults the desktop
    // icon theme for each icon load and recolor; on NixOS/KDE that is ~90
    // XDG data directories, which made a theme switch block for seconds.
    // A non-empty list keeps Qt from restoring the system defaults.
    QIcon::setThemeSearchPaths({QStringLiteral(":/icons")});
    QIcon::setFallbackSearchPaths({QStringLiteral(":/icons")});
#endif

    // This is necessary to show our icon correctly on Wayland
    app.setDesktopFileName("io.github.keithxc.DeskPort");
    qputenv("SDL_VIDEO_WAYLAND_WMCLASS", "io.github.keithxc.DeskPort");
    qputenv("SDL_VIDEO_X11_WMCLASS", "io.github.keithxc.DeskPort");

    // Register our C++ types for QML
    qmlRegisterType<ComputerModel>("ComputerModel", 1, 0, "ComputerModel");
    qmlRegisterType<AppModel>("AppModel", 1, 0, "AppModel");
    qmlRegisterUncreatableType<Session>("Session", 1, 0, "Session", "Session cannot be created from QML");
    qmlRegisterSingletonType<ComputerManager>("ComputerManager", 1, 0,
                                              "ComputerManager",
                                              [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                  return new ComputerManager(StreamingPreferences::get(qmlEngine));
                                              });
    qmlRegisterSingletonType<AutoUpdateChecker>("AutoUpdateChecker", 1, 0,
                                                "AutoUpdateChecker",
                                                [](QQmlEngine*, QJSEngine*) -> QObject* {
                                                    return new AutoUpdateChecker();
                                                });
    qmlRegisterSingletonType<Manual>("Manual", 1, 0, "Manual",
                                     [](QQmlEngine*, QJSEngine*) -> QObject* { return new Manual(); });
    const int systemPropertiesType = qmlRegisterSingletonType<SystemProperties>("SystemProperties", 1, 0,
                                               "SystemProperties",
                                               [](QQmlEngine*, QJSEngine*) -> QObject* {
                                                   return new SystemProperties();
                                               });
    qmlRegisterSingletonType<SdlGamepadKeyNavigation>("SdlGamepadKeyNavigation", 1, 0,
                                                      "SdlGamepadKeyNavigation",
                                                      [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                          return new SdlGamepadKeyNavigation(StreamingPreferences::get(qmlEngine));
                                                      });
    qmlRegisterSingletonType<StreamingPreferences>("StreamingPreferences", 1, 0,
                                                   "StreamingPreferences",
                                                   [](QQmlEngine* qmlEngine, QJSEngine*) -> QObject* {
                                                       return StreamingPreferences::get(qmlEngine);
                                                   });

    // Create the identity manager on the main thread
    IdentityManager::get();

    // We require the Material theme
    QQuickStyle::setStyle("Material");

    // Our icons are styled for a dark theme, so we do not allow the user to override this
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");

    // These are defaults that we allow the user to override
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_ACCENT")) {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "Purple");
    }
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT")) {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    }

    MultiSessions* multiSessions = nullptr;
    HostManager hostManager(nullptr, inputTestProfile.isEmpty() ? QString() : inputTestProfile + "/data/host");
#ifdef Q_OS_WIN
    WindowsInstallShutdown installShutdown(hostManager);
    app.installNativeEventFilter(&installShutdown);
#endif
    const bool resident = commandLineParserResult == GlobalCommandLineParser::NormalStartRequested;
    hostManager.setResident(resident);
    if (resident) app.setQuitOnLastWindowClosed(false);
    QObject::connect(&hostManager, &HostManager::fullscreenRequested, &app, [&] {
        if (multiSessions && multiSessions->busy()) { multiSessions->fullscreen(); return; }
        if (Session::get()) {
            SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortFullscreen;
            SDL_PushEvent(&event);
        }
    });
    QObject::connect(&hostManager, &HostManager::reconnectRequested, &app, [&] {
        if (multiSessions && multiSessions->busy()) { multiSessions->reconnect(multiSessions->selectedId()); return; }
        if (Session::get()) Session::get()->requestReconnect();
    });
    QObject::connect(&hostManager, &HostManager::disconnectRequested, &app, [&] {
        if (multiSessions && multiSessions->busy()) { multiSessions->disconnectSession(multiSessions->selectedId()); return; }
        if (Session::get()) {
            Session::get()->cancelRecovery();
            SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortEndSession;
            SDL_PushEvent(&event);
        }
    });
    QObject::connect(&hostManager, &HostManager::exitRequested, &app, [&] {
        if (multiSessions) multiSessions->shutdown();
        auto timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app, [timer, &app, &hostManager, &multiSessions] {
            if (Session::get()) {
                Session::get()->cancelRecovery();
                SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortEndSession;
                SDL_PushEvent(&event);
            }
            // Keep Qt and the tray alive while both independent owners drain.
            // Their destructors should not be the normal shutdown mechanism.
            if (!SessionLifetime::busy() && !hostManager.running() && (!multiSessions || !multiSessions->busy())) {
                timer->stop(); timer->deleteLater(); app.quit();
            }
        });
        timer->start(50);
    });
#ifdef Q_OS_UNIX
    // Logout, shutdown and `systemctl stop` must end the session and host
    // processes instead of waiting for systemd's SIGKILL.
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s_TerminationSocket) == 0) {
        auto notifier = new QSocketNotifier(s_TerminationSocket[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&hostManager] {
            char byte;
            (void)!read(s_TerminationSocket[0], &byte, 1);
            qInfo() << "Termination signal received; exiting";
            hostManager.requestExit();
        });
        struct sigaction action {};
        action.sa_handler = forwardTerminationSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
    }
#endif
    PeerManager peerManager(&hostManager, IdentityManager::get()->getCertificate(), IdentityManager::get()->getPrivateKey(),
        inputTestProfile.isEmpty() ? QString() : inputTestProfile + "/data/binding", 48991, QHostAddress::AnyIPv4,
        inputTestProfile.isEmpty() ? PeerManager::Mode::PlatformDefault : PeerManager::Mode::ClientOnly);
    DeskPortCli::ControlServer controlServer(&hostManager, &peerManager, nullptr, inputTestProfile);
    BrowserHost browserHost(&hostManager, nullptr, inputTestProfile.isEmpty() ? QString() : inputTestProfile + "/data/browser");
    if (resident && inputTestProfile.isEmpty() && !app.arguments().contains("--no-web-server")) browserHost.start();
    controlServer.setBrowserInfo([&browserHost] { return browserHost.localInfo(); });
    controlServer.setBrowserAuthenticator([&browserHost] { return browserHost.listening() ? browserHost.authenticatorUri() : QString(); },
                                    [&browserHost] { return browserHost.resetAuthenticator(); });
    controlServer.setBrowserPairings([&browserHost] { return QJsonArray::fromVariantList(browserHost.pairedBrowsers()); },
                                    [&browserHost](const QString& id) { return browserHost.revokeBrowser(id); });
    if (resident && !controlServer.listen())
        qWarning() << "Cannot open the local CLI endpoint:" << controlServer.errorString();
    qmlRegisterType<SessionCanvas>("DeskPort.Canvas", 1, 0, "SessionCanvas");
    MultiSessions managedSessions(&peerManager, IdentityManager::get()->getCertificate(), IdentityManager::get()->getPrivateKey());
    GlobalShortcut globalShortcut;
    QObject::connect(&globalShortcut, &GlobalShortcut::fallbackAvailabilityChanged, &managedSessions, [&managedSessions, &globalShortcut](bool enabled) {
        ShortcutState::configure(enabled, globalShortcut.sequence());
        managedSessions.setShortcutEnabled(enabled, globalShortcut.sequence());
    });
    QObject::connect(&managedSessions, &MultiSessions::shortcutRequested, &globalShortcut, &GlobalShortcut::fallbackActivated);
    multiSessions = &managedSessions;
    auto lifecycleChanged = [&] {
        managedSessions.setLifecycleBlocked(SleepMonitor::asleep(), ScreenLockMonitor::locked());
    };
    QObject::connect(&sleepMonitor, &SleepMonitor::sleeping, &managedSessions, lifecycleChanged);
    QObject::connect(&sleepMonitor, &SleepMonitor::resumed, &managedSessions, lifecycleChanged);
    QObject::connect(&screenLock, &ScreenLockMonitor::changed, &managedSessions, lifecycleChanged);
    lifecycleChanged();
    QObject::connect(&Diagnostics::instance(), &Diagnostics::changed, &managedSessions, [&managedSessions] {
        managedSessions.setDiagnosticsEnabled(Diagnostics::instance().enabled());
    });
    QObject::connect(&managedSessions, &MultiSessions::changed, &hostManager, [&] { hostManager.updateSessionMenu(managedSessions.sessions()); });
    QObject::connect(&hostManager, &HostManager::sessionSelected, &managedSessions, &MultiSessions::select);
    QObject::connect(&hostManager, &HostManager::sessionDisconnectRequested, &managedSessions, &MultiSessions::disconnectSession);
    QObject::connect(&hostManager, &HostManager::sessionReconnectRequested, &managedSessions, &MultiSessions::reconnect);
    {
        auto* preferences = StreamingPreferences::get();
        QObject::connect(&hostManager, &HostManager::streamTitleBarToggled, preferences, [preferences](bool hidden) {
            if (preferences->hideStreamTitleBar == hidden) return;
            preferences->hideStreamTitleBar = hidden;
            preferences->save();
            emit preferences->hideStreamTitleBarChanged();
        });
        QObject::connect(preferences, &StreamingPreferences::hideStreamTitleBarChanged, &hostManager, [&hostManager, preferences] {
            hostManager.setStreamTitleBarHidden(preferences->hideStreamTitleBar);
        });
        hostManager.setStreamTitleBarHidden(preferences->hideStreamTitleBar);
    }
    QObject::connect(&hostManager, &HostManager::trayMenuAboutToShow, &managedSessions, [&] { hostManager.updateSessionMenu(managedSessions.sessions()); });
    QObject::connect(&peerManager, &PeerManager::deviceRemovalFinished, &managedSessions, [&managedSessions](QString id, bool success) {
        if (success) managedSessions.disconnectSession(id.toLower());
    });
    if (app.arguments().contains("--share")) {
        QTimer::singleShot(0, &hostManager, [&hostManager] { hostManager.start(2560, 1440); });
    }
    // Record stalls independently of transport state; never record UI contents.
    QTimer uiHeartbeat;
    QElapsedTimer uiTick; uiTick.start();
    QObject::connect(&uiHeartbeat, &QTimer::timeout, &app, [&uiTick] {
        const auto elapsed = uiTick.restart();
        if (elapsed > 350)
            qInfo("DeskPort navigation stage=event-loop-delay duration_ms=%lld", static_cast<long long>(elapsed));
    });
    uiHeartbeat.start(100);
    QQmlApplicationEngine engine;
    auto showDevices = [&engine, &pendingActivation, &managedSessions] {
        WindowActivation::setPending({});
        managedSessions.showDevices();
        QElapsedTimer presentation;
        presentation.start();
        qInfo("DeskPort navigation stage=devices-request duration_ms=0");
        if (engine.rootObjects().isEmpty()) { pendingActivation = true; return; }
        QMetaObject::invokeMethod(engine.rootObjects().first(), "showDevices");
        if (auto window = qobject_cast<QWindow*>(engine.rootObjects().first())) {
            if (auto quickWindow = qobject_cast<QQuickWindow*>(window)) {
                // A context object makes this a one-shot connection and also
                // cancels it if the window closes without submitting a frame.
                auto frame = new QObject(quickWindow);
                QObject::connect(quickWindow, &QQuickWindow::frameSwapped, frame, [frame, presentation] {
                    qInfo("DeskPort navigation stage=devices-frame duration_ms=%lld", static_cast<long long>(presentation.elapsed()));
                    delete frame;
                }, Qt::QueuedConnection);
                QTimer::singleShot(5000, frame, &QObject::deleteLater);
            }
            if (window->windowState() == Qt::WindowMinimized) window->showNormal();
            else window->setVisible(true);
            window->raise(); window->requestActivate();
#ifdef Q_OS_MACOS
            // Ordering a window front does not activate an accessory application.
            deskPortActivateApplication();
#endif
        }
        qInfo("DeskPort navigation stage=devices-ready duration_ms=%lld", static_cast<long long>(presentation.elapsed()));
        // Window presentation must precede the command to the media owner.
        // The command only releases input and hides video; it never ends a session.
        if (SessionLifetime::busy()) {
            SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortShowDevices;
            SDL_PushEvent(&event);
        }
    };
    auto recallViewer = [&engine, &showDevices, &managedSessions] {
        if (managedSessions.busy() && managedSessions.recall()) return;
        QVariant handled;
        if (!engine.rootObjects().isEmpty())
            QMetaObject::invokeMethod(engine.rootObjects().first(), "prepareViewerRecall", Q_RETURN_ARG(QVariant, handled));
        if (handled.toBool()) {
            if (SessionLifetime::busy()) {
                SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortRecallWindow;
                SDL_PushEvent(&event);
            }
            return;
        }
        showDevices();
    };
    QObject::connect(&hostManager, &HostManager::hideRequested, &app, [&] {
        WindowActivation::setPending({});
        if (managedSessions.busy()) managedSessions.showDevices();
        if (Session::get()) {
            SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortHideWindow;
            SDL_PushEvent(&event);
        } else if (!engine.rootObjects().isEmpty()) {
            if (auto window = qobject_cast<QWindow*>(engine.rootObjects().first())) window->hide();
        }
    });
    // The tray's left button is the one-action route to the window: it shows the
    // remote desktop when one is connected, otherwise it toggles the device list.
    // A click hides the window only when it is the one in front. A visible
    // window behind other applications is brought forward instead, so a single
    // click always shows it.
    // Clicking a panel icon can take focus from the window just before the
    // click is reported, so a window that was active a moment ago still counts.
    QElapsedTimer frontLeft;
    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &app, [&frontLeft](QWindow* focus) {
        if (!focus) frontLeft.start();
    });
    const auto windowInFront = [&frontLeft](QWindow* window, bool trayClick) {
        if (!window || !window->isVisible() || window->windowState() == Qt::WindowMinimized) return false;
        const auto focus = QGuiApplication::focusWindow();
        if (focus && window->isAncestorOf(focus)) return true;
#ifdef HAS_X11
        if (!trayClick) return WindowActivation::shortcutWindowIsActive(window);
#endif
        return window->isActive() || (trayClick && frontLeft.isValid() && frontLeft.elapsed() < 600);
    };
    auto toggleWindow = [&engine, &showDevices, &recallViewer, &managedSessions, windowInFront](bool trayClick, const QByteArray& token) {
        auto window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QWindow*>(engine.rootObjects().first());
        const auto prepareQtRecall = [=] {
            if (!trayClick) WindowActivation::prepareShortcutRecall(window, token);
        };
        // Qt raises its own window with the shell's activation token from the
        // environment. An SDL viewer window in this process or in a session
        // worker needs the token handed over explicitly.
        // The retained canvas belongs to this process, including its loading
        // page. A worker cannot report its focus until video is ready, so using
        // that report here makes every connecting-window toggle a recall.
        const bool viewerInFront = managedSessions.canvasEnabled()
            ? managedSessions.canvasVisible() && windowInFront(window, trayClick)
            : managedSessions.viewerInFront(trayClick);
        if (managedSessions.busy() && viewerInFront) {
            // Hiding the viewer releases its input; the sessions stay connected.
            managedSessions.showDevices();
            if (managedSessions.canvasEnabled() && window) window->hide();
        } else if (managedSessions.busy()) {
            // Go straight to the last viewed desktop, never through the device
            // list; the list stays available from the tray menu.
            if (managedSessions.canvasEnabled()) prepareQtRecall();
            managedSessions.setActivationToken(token, !trayClick);
            if (!managedSessions.recall()) {
                managedSessions.setActivationToken({});
                if (!windowInFront(window, trayClick)) { prepareQtRecall(); showDevices(); }
                else window->hide();
            }
        } else if (SessionLifetime::busy() && (!Session::get() || !Session::get()->viewerReady())) {
            WindowActivation::setPending(token, !trayClick);
            prepareQtRecall();
            recallViewer();
        } else if (Session::get()) {
            WindowActivation::setPending(token, !trayClick);
            SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = trayClick ? DeskPortToggleWindow : DeskPortShortcutToggleWindow;
            SDL_PushEvent(&event);
        } else {
            if (windowInFront(window, trayClick)) window->hide();
            else { prepareQtRecall(); showDevices(); }
        }
        // A token is valid for one activation only; never keep it for a later one.
        qunsetenv("XDG_ACTIVATION_TOKEN");
    };
    QObject::connect(&managedSessions, &MultiSessions::devicesRequested, &engine, [&engine] {
        if (!engine.rootObjects().isEmpty()) QMetaObject::invokeMethod(engine.rootObjects().first(), "presentDevices");
    });
    QObject::connect(&managedSessions, &MultiSessions::viewerShown, &engine, [&engine, &managedSessions] {
        if (!engine.rootObjects().isEmpty()) if(auto window=qobject_cast<QWindow*>(engine.rootObjects().first())) {
            if (!managedSessions.canvasEnabled()) window->hide();
        }
    });
    engine.rootContext()->setContextProperty("sessionManager", &managedSessions);
    // Daily traffic budget: worker sessions report through the shell; an
    // in-process session is sampled here once a second.
    auto& trafficBudget = TrafficBudget::instance();
    engine.rootContext()->setContextProperty("trafficBudget", &trafficBudget);
    QObject::connect(&managedSessions, &MultiSessions::sessionTraffic, &trafficBudget,
                     [&trafficBudget](QString id, double total) { trafficBudget.account(id, qint64(total)); });
    QObject::connect(&managedSessions, &MultiSessions::sessionClosed, &trafficBudget,
                     [&trafficBudget](QString id) { trafficBudget.forget(id); });
    auto* inProcessTraffic = new QTimer(&app);
    QObject::connect(inProcessTraffic, &QTimer::timeout, &trafficBudget, [&trafficBudget] {
        if (auto* session = Session::get()) {
            const auto traffic = session->traffic();
            trafficBudget.account(QStringLiteral("in-process"),
                                  qint64(traffic.value("received").toDouble() + traffic.value("sent").toDouble()));
        }
    });
    inProcessTraffic->start(1000);
    instance.activate = showDevices;
    QObject::connect(&hostManager, &HostManager::openRequested, &app, showDevices);
    QObject::connect(&hostManager, &HostManager::showDevicesRequested, &app, showDevices);
    const auto showOperation = [&engine, showDevices](const QString& text) {
        showDevices();
        if (!engine.rootObjects().isEmpty())
            QMetaObject::invokeMethod(engine.rootObjects().first(), "showOperation", Q_ARG(QVariant, text));
    };
    QObject::connect(&hostManager, &HostManager::operationRequested, &app, showOperation);
    QObject::connect(&hostManager, &HostManager::disconnectRequested, &app, [&engine, showOperation] {
        auto session = Session::get();
        if (!session) return;
        showOperation(QCoreApplication::translate("MainWindow", "Disconnecting…"));
        QObject::connect(session, &Session::transportCleanupFinished, &engine, [&engine] {
            if (!engine.rootObjects().isEmpty())
                QMetaObject::invokeMethod(engine.rootObjects().first(), "finishOperation");
        });
    });
    QObject::connect(&hostManager, &HostManager::viewerRecallRequested, &app, recallViewer);
    QObject::connect(&hostManager, &HostManager::toggleWindowRequested, &app, [toggleWindow] {
        toggleWindow(true, qgetenv("XDG_ACTIVATION_TOKEN"));
    });
    QObject::connect(&globalShortcut, &GlobalShortcut::activated, &app, [toggleWindow](const QByteArray& token) {
        // Qt consumes the token for its own window; SDL workers receive it via IPC.
        if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token);
        toggleWindow(false, token);
    });
    engine.rootContext()->setContextProperty("globalShortcut", &globalShortcut);
#ifdef Q_OS_DARWIN
    MacTitleBar macTitleBar;
#endif
    engine.rootContext()->setContextProperty("diagnostics", &Diagnostics::instance());
    engine.rootContext()->setContextProperty("hostManager", &hostManager);
    engine.rootContext()->setContextProperty("browserHost", &browserHost);
    engine.rootContext()->setContextProperty("peerManager", &peerManager);
    const bool showAfterRestart = QSettings().value("ui/showAfterRestart", false).toBool();
    QSettings().remove("ui/showAfterRestart");
    engine.rootContext()->setContextProperty("startInBackground", app.arguments().contains("--background") && !showAfterRestart);
    engine.rootContext()->setContextProperty("startSharingPage", app.arguments().contains("--share"));
    QString initialView;
    bool hasGUI = true;

    switch (commandLineParserResult) {
    case GlobalCommandLineParser::NormalStartRequested:
        initialView = "qrc:/gui/PcView.qml";
#ifdef Q_OS_LINUX
        if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND")) {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
            if (QGuiApplication::platformName().startsWith("wayland")) {
                // Old Qt retains a stale Vulkan surface after hiding a Quick
                // window. The video child owns its own Vulkan instance and
                // remap handling; keep the shell on OpenGL on these versions.
                QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
            } else
#endif
            {
                // Keep Devices and connection errors available without an ICD.
                QVulkanInstance probe;
                if (probe.create()) QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
                else qWarning() << "Vulkan is unavailable; using the default shell graphics backend";
            }
        }
#endif
        // No Qt Quick window exists before this point.
        if (!renderLoopOverridden) qunsetenv("QSG_RENDER_LOOP");
        break;
    case GlobalCommandLineParser::StreamRequested:
        {
            initialView = "qrc:/gui/CliStartStreamSegue.qml";
            StreamingPreferences* preferences = StreamingPreferences::get();
            StreamCommandLineParser streamParser;
            streamParser.parse(app.arguments(), preferences);
            QString host    = streamParser.getHost();
            QString appName = streamParser.getAppName();
            auto launcher   = new CliStartStream::Launcher(host, appName, preferences, &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::QuitRequested:
        {
            initialView = "qrc:/gui/CliQuitStreamSegue.qml";
            QuitCommandLineParser quitParser;
            quitParser.parse(app.arguments());
            auto launcher = new CliQuitStream::Launcher(quitParser.getHost(), &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::PairRequested:
        {
            initialView = "qrc:/gui/CliPair.qml";
            PairCommandLineParser pairParser;
            pairParser.parse(app.arguments());
            auto launcher = new CliPair::Launcher(pairParser.getHost(), pairParser.getPredefinedPin(), &app);
            engine.rootContext()->setContextProperty("launcher", launcher);
            break;
        }
    case GlobalCommandLineParser::ListRequested:
        {
            ListCommandLineParser listParser;
            listParser.parse(app.arguments());
            auto launcher = new CliListApps::Launcher(listParser.getHost(), listParser, &app);
            launcher->execute(new ComputerManager(StreamingPreferences::get()));
            hasGUI = false;
            break;
        }
    }

    if (hasGUI) {
        engine.rootContext()->setContextProperty("initialView", initialView);

        // Load the main.qml file
        engine.load(QUrl(QStringLiteral("qrc:/gui/main.qml")));
        if (engine.rootObjects().isEmpty())
            return -1;
        managedSessions.setCanvasWindow(qobject_cast<QWindow*>(engine.rootObjects().first()));
#ifdef Q_OS_WIN
        if (auto window = qobject_cast<QWindow*>(engine.rootObjects().first())) {
            auto* preferences = StreamingPreferences::get();
            auto* appearance = engine.singletonInstance<SystemProperties*>(systemPropertiesType);
            const auto applyTitleBar = [window, preferences, appearance] {
                const bool dark = preferences->uiTheme == 2 ||
                    (preferences->uiTheme == 0 && appearance->systemDark());
                updateWindowsTitleBar(window, dark);
            };
            QObject::connect(preferences, &StreamingPreferences::uiThemeChanged, window, applyTitleBar);
            QObject::connect(appearance, &SystemProperties::appearanceChanged, window, applyTitleBar);
            QObject::connect(window, &QWindow::visibleChanged, window, applyTitleBar);
            applyTitleBar();
        }
#endif
#ifdef Q_OS_DARWIN
        macTitleBar.attach(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
        QObject::connect(&managedSessions, &MultiSessions::changed, &macTitleBar, [&] {
            macTitleBar.setCanvasVisible(managedSessions.canvasVisible());
        });
        macTitleBar.setCanvasVisible(managedSessions.canvasVisible());
        auto* titleBarPreferences = StreamingPreferences::get();
        QObject::connect(titleBarPreferences, &StreamingPreferences::hideStreamTitleBarChanged, &macTitleBar, [&, titleBarPreferences] {
            macTitleBar.setStreamTitleBarHidden(titleBarPreferences->hideStreamTitleBar);
        });
        macTitleBar.setStreamTitleBarHidden(titleBarPreferences->hideStreamTitleBar);
#endif
        if (pendingActivation) showDevices();
    }

    if (resident) {
        auto* preferences = StreamingPreferences::get();
        QObject::connect(preferences, &StreamingPreferences::globalShortcutSequenceChanged,
                         &globalShortcut, [preferences, &globalShortcut] {
            globalShortcut.setSequence(preferences->globalShortcutSequence);
        });
        QObject::connect(preferences, &StreamingPreferences::globalShortcutEnabledChanged,
                         &globalShortcut, [preferences, &globalShortcut] {
            globalShortcut.setEnabled(preferences->globalShortcutEnabled);
        });
        globalShortcut.setSequence(preferences->globalShortcutSequence);
        globalShortcut.setEnabled(preferences->globalShortcutEnabled);
        QTimer::singleShot(0, &globalShortcut, &GlobalShortcut::start);
    }
    int err = app.exec();

    if (hostManager.restarting()) relaunchAfterExit();

    // Give worker tasks time to properly exit. Fixes PendingQuitTask
    // sometimes freezing and blocking process exit.
    QThreadPool::globalInstance()->waitForDone(30000);

#ifdef Q_OS_WIN32
    // Without an explicit flush, console redirection for the list command
    // doesn't work reliably (sometimes the target file contains no text).
    fflush(stderr);
    fflush(stdout);
#endif

    return err;
}
