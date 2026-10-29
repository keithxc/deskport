// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlesshost.h"
#include "streamchannel.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#include <fcntl.h>
#include <unistd.h>

namespace {
int signalPipe[2] = {-1, -1};

void signalHandler(int signalNumber)
{
    const unsigned char value = static_cast<unsigned char>(signalNumber);
    if (signalPipe[1] >= 0)
        (void)!::write(signalPipe[1], &value, sizeof(value));
}

class SignalWatcher final : public QObject {
public:
    explicit SignalWatcher(std::function<void(int)> callback, QObject* parent = nullptr)
        : QObject(parent), m_Callback(std::move(callback))
    {
    }

    bool install(QString* error)
    {
        if (::pipe(signalPipe) != 0) {
            *error = QStringLiteral("pipe failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        for (const int descriptor : signalPipe) {
            const int flags = ::fcntl(descriptor, F_GETFL, 0);
            const int fdFlags = ::fcntl(descriptor, F_GETFD, 0);
            if (flags < 0 || fdFlags < 0 ||
                ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0 ||
                ::fcntl(descriptor, F_SETFD, fdFlags | FD_CLOEXEC) != 0) {
                *error = QStringLiteral("fcntl failed: %1")
                             .arg(QString::fromLocal8Bit(std::strerror(errno)));
                closeDescriptors();
                return false;
            }
        }

        struct sigaction action {};
        action.sa_handler = signalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (::sigaction(SIGTERM, &action, &m_OldTerm) != 0 ||
            ::sigaction(SIGINT, &action, &m_OldInt) != 0) {
            *error = QStringLiteral("sigaction failed: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
            closeDescriptors();
            return false;
        }
        struct sigaction ignorePipe {};
        ignorePipe.sa_handler = SIG_IGN;
        sigemptyset(&ignorePipe.sa_mask);
        if (::sigaction(SIGPIPE, &ignorePipe, &m_OldPipe) != 0) {
            *error = QStringLiteral("sigaction(SIGPIPE) failed: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
            ::sigaction(SIGTERM, &m_OldTerm, nullptr);
            ::sigaction(SIGINT, &m_OldInt, nullptr);
            closeDescriptors();
            return false;
        }
        m_Installed = true;
        m_Notifier = std::make_unique<QSocketNotifier>(signalPipe[0], QSocketNotifier::Read);
        connect(m_Notifier.get(), &QSocketNotifier::activated, this,
                [this](QSocketDescriptor, QSocketNotifier::Type) {
                    unsigned char values[32];
                    while (true) {
                        const ssize_t count = ::read(signalPipe[0], values, sizeof(values));
                        if (count <= 0)
                            break;
                        for (ssize_t index = 0; index < count; ++index)
                            m_Callback(static_cast<int>(values[index]));
                    }
                });
        return true;
    }

    ~SignalWatcher() override
    {
        m_Notifier.reset();
        if (m_Installed) {
            ::sigaction(SIGTERM, &m_OldTerm, nullptr);
            ::sigaction(SIGINT, &m_OldInt, nullptr);
            ::sigaction(SIGPIPE, &m_OldPipe, nullptr);
        }
        closeDescriptors();
    }

private:
    void closeDescriptors()
    {
        if (signalPipe[0] >= 0)
            ::close(signalPipe[0]);
        if (signalPipe[1] >= 0)
            ::close(signalPipe[1]);
        signalPipe[0] = -1;
        signalPipe[1] = -1;
    }

    std::function<void(int)> m_Callback;
    std::unique_ptr<QSocketNotifier> m_Notifier;
    struct sigaction m_OldTerm {};
    struct sigaction m_OldInt {};
    struct sigaction m_OldPipe {};
    bool m_Installed = false;
};

enum class ParseResult { Ok, Help, Version, Error };

bool takeOptionValue(const QStringList& arguments, int* index, const QString& option,
                     QString* value, QString* error)
{
    const QString argument = arguments.at(*index);
    if (argument == option) {
        if (*index + 1 >= arguments.size()) {
            *error = QStringLiteral("Missing value for %1").arg(option);
            return false;
        }
        *value = arguments.at(++*index);
        return true;
    }
    const QString prefix = option + QLatin1Char('=');
    if (argument.startsWith(prefix)) {
        *value = argument.mid(prefix.size());
        if (value->isEmpty()) {
            *error = QStringLiteral("Empty value for %1").arg(option);
            return false;
        }
        return true;
    }
    return false;
}

bool parseDimension(const QString& text, const QString& option, int* value, QString* error)
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,5}$"));
    bool ok = false;
    const int parsed = text.toInt(&ok, 10);
    if (!ok || !digits.match(text).hasMatch()) {
        *error = QStringLiteral("%1 requires a decimal integer").arg(option);
        return false;
    }
    *value = parsed;
    return true;
}

ParseResult parseArguments(const QStringList& arguments, SidecarOptions* options, QString* error)
{
    bool sawSocket = false;
    bool sawWidth = false;
    bool sawHeight = false;
    bool sawCaptureDir = false;
    bool sawScale = false;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument == QStringLiteral("--")) {
            options->childArgv = arguments.mid(index + 1);
            if (options->childArgv.isEmpty()) {
                *error = QStringLiteral("-- must be followed by an application argv");
                return ParseResult::Error;
            }
            break;
        }
        if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h"))
            return ParseResult::Help;
        if (argument == QStringLiteral("--version"))
            return ParseResult::Version;
        if (argument == QStringLiteral("--self-test")) {
            if (options->selfTest) {
                *error = QStringLiteral("--self-test was specified more than once");
                return ParseResult::Error;
            }
            options->selfTest = true;
            continue;
        }
        if (argument == QStringLiteral("--app") || argument.startsWith(QStringLiteral("--app="))) {
            QString value;
            if (!takeOptionValue(arguments, &index, QStringLiteral("--app"), &value, error))
                return ParseResult::Error;
            options->apps.append(value);
            continue;
        }
        if (argument == QStringLiteral("--stream")) {
            if (options->stream) {
                *error = QStringLiteral("--stream was specified more than once");
                return ParseResult::Error;
            }
            options->stream = true;
            continue;
        }
        if (argument == QStringLiteral("--test-client")) {
            if (options->testClient) {
                *error = QStringLiteral("--test-client was specified more than once");
                return ParseResult::Error;
            }
            options->testClient = true;
            continue;
        }

        QString value;
        if (argument == QStringLiteral("--socket") || argument.startsWith(QStringLiteral("--socket="))) {
            if (sawSocket || !takeOptionValue(arguments, &index, QStringLiteral("--socket"),
                                              &value, error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("--socket was specified more than once");
                return ParseResult::Error;
            }
            sawSocket = true;
            options->socketName = value;
            continue;
        }
        if (argument == QStringLiteral("--width") || argument.startsWith(QStringLiteral("--width="))) {
            if (sawWidth || !takeOptionValue(arguments, &index, QStringLiteral("--width"),
                                             &value, error) ||
                !parseDimension(value, QStringLiteral("--width"), &options->width, error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("--width was specified more than once");
                return ParseResult::Error;
            }
            sawWidth = true;
            continue;
        }
        if (argument == QStringLiteral("--height") || argument.startsWith(QStringLiteral("--height="))) {
            if (sawHeight || !takeOptionValue(arguments, &index, QStringLiteral("--height"),
                                              &value, error) ||
                !parseDimension(value, QStringLiteral("--height"), &options->height, error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("--height was specified more than once");
                return ParseResult::Error;
            }
            sawHeight = true;
            continue;
        }
        if (argument == QStringLiteral("--scale") || argument.startsWith(QStringLiteral("--scale="))) {
            if (sawScale || !takeOptionValue(arguments, &index, QStringLiteral("--scale"),
                                             &value, error) ||
                !parseDimension(value, QStringLiteral("--scale"), &options->scale, error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("--scale was specified more than once");
                return ParseResult::Error;
            }
            sawScale = true;
            continue;
        }
        if (argument == QStringLiteral("--capture-dir") ||
            argument.startsWith(QStringLiteral("--capture-dir="))) {
            if (sawCaptureDir ||
                !takeOptionValue(arguments, &index, QStringLiteral("--capture-dir"),
                                 &value, error)) {
                if (error->isEmpty())
                    *error = QStringLiteral("--capture-dir was specified more than once");
                return ParseResult::Error;
            }
            sawCaptureDir = true;
            options->captureDir = QDir::cleanPath(value);
            continue;
        }
        *error = QStringLiteral("Unknown option: %1").arg(argument);
        return ParseResult::Error;
    }
    if (options->selfTest && options->testClient) {
        *error = QStringLiteral("--self-test and --test-client are mutually exclusive");
        return ParseResult::Error;
    }
    if (options->testClient && !options->childArgv.isEmpty()) {
        *error = QStringLiteral("--test-client cannot launch another application");
        return ParseResult::Error;
    }
    return ParseResult::Ok;
}

void printHelp()
{
    std::fputs(
        "Usage: deskport-seamless-host [options] [-- application [arguments...]]\n"
        "\n"
        "Headless QtWaylandCompositor M2 sidecar. It emits bounded newline JSON metadata\n"
        "and can hash or save wl_shm frames locally; it does not stream pixels.\n"
        "\n"
        "Options:\n"
        "  --socket NAME       Private Wayland socket name (default: per-process name)\n"
        "  --width PIXELS      Output width, 64..7680 (default: 1280)\n"
        "  --height PIXELS     Output height, 64..4320 (default: 720)\n"
        "  --capture-dir PATH  Existing owner-controlled directory for local PNG captures\n"
        "  --stream            Experimental: binary frame/input stream on stdin/stdout,\n"
        "                      for use through an authenticated SSH session only\n"
        "  --scale N           Output scale factor for HiDPI clients, 1..4 (default: 1)\n"
        "  --app ID=PROGRAM    Stream mode: allow the client to launch PROGRAM by ID\n"
        "                      (repeatable; the client never supplies a command)\n"
        "  --self-test         Run one known-pixel wl_shm lifecycle test\n"
        "  --test-client       Internal known-pixel Wayland client mode\n"
        "  --help              Show this help\n"
        "  --version           Show the control protocol version\n",
        stdout);
}
} // namespace

int main(int argc, char* argv[])
{
    bool testClientMode = false;
    for (int index = 1; index < argc; ++index) {
        if (QByteArray(argv[index]) == QByteArrayLiteral("--test-client")) {
            testClientMode = true;
            break;
        }
    }
    if (testClientMode) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland"));
    } else {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
        qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
        qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("software"));
    }

    QGuiApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("deskport-seamless-host"));
    application.setApplicationVersion(QStringLiteral("m2-control-v1"));
    application.setQuitOnLastWindowClosed(false);

    SidecarOptions options;
    QString error;
    const ParseResult parseResult = parseArguments(application.arguments(), &options, &error);
    if (parseResult == ParseResult::Help) {
        printHelp();
        return 0;
    }
    if (parseResult == ParseResult::Version) {
        std::fputs("deskport-seamless-host m2-control-v1\n", stdout);
        return 0;
    }
    if (parseResult == ParseResult::Error) {
        std::fprintf(stderr, "deskport-seamless-host: %s\n", qPrintable(error));
        return 2;
    }
    if (!validateSidecarOptions(&options, &error)) {
        std::fprintf(stderr, "deskport-seamless-host: %s\n", qPrintable(error));
        return 2;
    }
    if (options.testClient)
        return runKnownPixelTestClient(options);

    JsonEmitter emitter;
    if (!emitter.isOpen()) {
        std::fputs("deskport-seamless-host: failed to open stdout\n", stderr);
        return 1;
    }

    std::unique_ptr<StreamChannel> stream;
    if (options.stream) {
        stream = std::make_unique<StreamChannel>(STDIN_FILENO, STDOUT_FILENO);
        if (!stream->open(&error)) {
            std::fprintf(stderr, "deskport-seamless-host: %s\n", qPrintable(error));
            return 1;
        }
        emitter.setStream(stream.get());
        QJsonArray appIds;
        for (const QString& entry : options.apps)
            appIds.append(entry.left(entry.indexOf(QLatin1Char('='))));
        stream->sendJson(SeamlessStream::ServerHello, 0, QJsonObject{
            {QStringLiteral("protocol"), QStringLiteral("deskport-seamless-stream")},
            {QStringLiteral("version"), SeamlessStream::Version},
            {QStringLiteral("scale"), options.scale},
            {QStringLiteral("encoding"), QStringLiteral("zlib")},
            {QStringLiteral("maxWindows"), options.maxWindows},
            {QStringLiteral("apps"), appIds},
        });
    }

    SidecarHost host(options, emitter);
    if (stream) {
        host.setStream(stream.get());
        SeamlessCompositor* compositor = host.compositor();
        stream->recordReceived = [compositor, &host](const SeamlessStream::RecordReader::Record& record) {
            if (record.type == SeamlessStream::ClientFrameAck) {
                compositor->handleFrameAck();
                return;
            }
            if (record.type != SeamlessStream::ClientInput &&
                record.type != SeamlessStream::ClientHello) {
                return;
            }
            if (record.payload.size() > static_cast<qsizetype>(SeamlessStream::MaxJsonPayload))
                return;
            const QJsonDocument document = QJsonDocument::fromJson(record.payload);
            if (!document.isObject() || record.type == SeamlessStream::ClientHello)
                return;
            if (record.windowId == 0)
                host.handleSessionInput(document.object());
            else
                compositor->handleClientInput(record.windowId, document.object());
        };
        stream->closed = [&host](const QString& reason) {
            host.beginShutdown(reason, 0);
        };
    }
    SignalWatcher signalWatcher([&host](int signalNumber) {
        host.beginShutdown(signalNumber == SIGTERM ? QStringLiteral("sigterm")
                                                   : QStringLiteral("sigint"),
                           128 + signalNumber);
    });
    if (!signalWatcher.install(&error)) {
        emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("signal-setup-failed")},
            {QStringLiteral("message"), error},
        });
        return 1;
    }

    bool selfTestCreated = false;
    bool selfTestCaptured = false;
    bool selfTestDestroyed = false;
    bool selfTestChildExited = false;
    bool selfTestFailed = false;
    const QByteArray expectedHash = knownPatternSha256(QSize(options.width, options.height));
    auto finishSelfTest = [&] {
        if (!options.selfTest || selfTestFailed || !selfTestCreated || !selfTestCaptured ||
            !selfTestDestroyed || !selfTestChildExited) {
            return;
        }
        emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("self-test-pass")},
            {QStringLiteral("sha256"), QString::fromLatin1(expectedHash)},
            {QStringLiteral("networkStreaming"), false},
        });
        host.beginShutdown(QStringLiteral("self-test-complete"), 0);
    };
    auto failSelfTest = [&](const QString& reason) {
        if (!options.selfTest || selfTestFailed)
            return;
        selfTestFailed = true;
        emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("self-test-fail")},
            {QStringLiteral("message"), reason.left(1024)},
        });
        host.beginShutdown(QStringLiteral("self-test-failed"), 1);
    };
    host.windowCreated = [&](quint64) {
        selfTestCreated = true;
        finishSelfTest();
    };
    host.frameCaptured = [&](const CapturedFrame& frame) {
        if (frame.size != QSize(options.width, options.height)) {
            failSelfTest(QStringLiteral("Captured frame size did not match the configured size"));
            return;
        }
        if (frame.sha256 != expectedHash) {
            failSelfTest(QStringLiteral("Captured pixels did not match the known pattern"));
            return;
        }
        selfTestCaptured = true;
        finishSelfTest();
    };
    host.windowDestroyed = [&](quint64) {
        selfTestDestroyed = true;
        finishSelfTest();
    };
    host.childFinished = [&](int code, QProcess::ExitStatus status) {
        if (!options.selfTest)
            return;
        if (status != QProcess::NormalExit || code != 0) {
            failSelfTest(QStringLiteral("The known-pixel client did not exit cleanly"));
            return;
        }
        selfTestChildExited = true;
        finishSelfTest();
    };

    if (!host.start(&error)) {
        emitter.writeEvent(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("code"), QStringLiteral("sidecar-start-failed")},
            {QStringLiteral("message"), error.left(1024)},
        });
        return 1;
    }

    QTimer selfTestTimeout;
    if (options.selfTest) {
        selfTestTimeout.setSingleShot(true);
        selfTestTimeout.setInterval(10000);
        QObject::connect(&selfTestTimeout, &QTimer::timeout, &application,
                         [&] { failSelfTest(QStringLiteral("Known-pixel lifecycle timed out")); });
        selfTestTimeout.start();
        const QStringList clientArgv{
            QCoreApplication::applicationFilePath(),
            QStringLiteral("--test-client"),
            QStringLiteral("--width"),
            QString::number(options.width),
            QStringLiteral("--height"),
            QString::number(options.height),
        };
        if (!host.launchChild(clientArgv, &error))
            failSelfTest(QStringLiteral("Failed to launch known-pixel client: %1").arg(error));
    }

    return application.exec();
}
