#include "seamlesshostmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif

namespace {
constexpr int MaxControlLine = 32768;
constexpr int MinimumWidth = 64;
constexpr int MinimumHeight = 64;
constexpr int MaximumWidth = 7680;
constexpr int MaximumHeight = 4320;
constexpr int MaximumWindows = 64;
constexpr int StartupTimeoutMs = 10000;
constexpr int MaxMetadataText = 1024;
constexpr int MaxAppIdText = 255;
// Reserve room under the local control socket's 1 MiB response limit even for
// 64 windows whose validated metadata consists entirely of JSON-escaped text.
constexpr int MaxSnapshotDiagnosticCharacters = 512;

bool integerField(const QJsonObject& object, const QString& name, int minimum, int maximum,
                  int* result) {
    const auto value = object.value(name);
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < minimum || number > maximum) return false;
    if (result) *result = int(number);
    return true;
}

bool textField(const QJsonObject& object, const QString& name, int maximum, bool allowEmpty = true) {
    const auto value = object.value(name);
    if (!value.isString()) return false;
    const auto text = value.toString();
    return (allowEmpty || !text.isEmpty()) && !text.contains(QChar::Null) &&
           text.toUtf8().size() <= maximum;
}

bool sequenceField(const QJsonObject& object, quint64* sequence) {
    const auto value = object.value(QStringLiteral("sequence"));
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    // JSON numbers must remain exactly representable across consumers.
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < 1 || number > 9007199254740991.0) return false;
    *sequence = static_cast<quint64>(number);
    return true;
}

bool damageField(const QJsonObject& object, const QString& name, int width, int height) {
    const auto value = object.value(name);
    if (!value.isArray() || value.toArray().size() > 128) return false;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) return false;
        const auto rectangle = entry.toObject();
        int x, y, w, h;
        if (!integerField(rectangle, QStringLiteral("x"), 0, width, &x) ||
            !integerField(rectangle, QStringLiteral("y"), 0, height, &y) ||
            !integerField(rectangle, QStringLiteral("width"), 1, width, &w) ||
            !integerField(rectangle, QStringLiteral("height"), 1, height, &h) ||
            w > width - x || h > height - y) return false;
    }
    return true;
}

}

SeamlessHostManager::SeamlessHostManager(QObject* parent) : QObject(parent) {
    m_Status = available() ? tr("Seamless host is off") :
        tr("The experimental Seamless host helper is missing");
    m_Process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_LINUX
    m_Process.setChildProcessModifier([this] {
        if (::prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            m_Process.failChildProcessModifier("prctl(PR_SET_PDEATHSIG)", errno);
#else
            ::_exit(127); // Qt 6.4: no failure channel; the child does not start.
#endif
        }
        if (::getppid() == 1)
            ::raise(SIGTERM);
    });
#endif
    connect(&m_Process, &QProcess::readyReadStandardOutput, this, &SeamlessHostManager::consumeOutput);
    connect(&m_Process, &QProcess::readyReadStandardError, this, [this] {
        const auto message = QString::fromUtf8(m_Process.readAllStandardError()).trimmed();
        if (!message.isEmpty()) qWarning().noquote() << "Seamless host:" << message.left(4096);
    });
    connect(&m_Process, &QProcess::started, this, [this] {
        // terminate() may have been requested while QProcess was still starting.
        if (m_Stopping) m_Process.terminate();
    });
    connect(&m_Process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_Active) return;
        if (error == QProcess::FailedToStart && m_Stopping) {
            finishStop(); // Cancellation during Starting still needs a terminal notification.
            return;
        }
        if (m_Stopping) return;
        if (m_TerminalStatus.isEmpty())
            m_TerminalStatus = tr("Seamless host failed: %1").arg(m_Process.errorString());
        if (error == QProcess::FailedToStart)
            finishStop(); // QProcess does not emit finished() when exec fails.
        else if (error != QProcess::Crashed)
            stop();
        // A crash is followed by finished(); emit stopped exactly once there.
    });
    connect(&m_Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
        if (!m_Active || m_Finishing) return;
        const auto run = m_RunId;
        consumeOutput();
        if (!m_Active || run != m_RunId) return;
        if (m_TerminalStatus.isEmpty() &&
            (!m_Stopping || (!m_StopRequested && (code != 0 || exitStatus != QProcess::NormalExit)))) {
            m_TerminalStatus = !m_Output.isEmpty() ?
                tr("Seamless host returned a truncated control message") :
                tr("Seamless host stopped (%1, %2)").arg(code).arg(
                    exitStatus == QProcess::NormalExit ? tr("normal exit") : tr("crashed"));
        }
        finishStop();
    });
    m_StartupTimer.setSingleShot(true);
    m_StartupTimer.setInterval(StartupTimeoutMs);
    connect(&m_StartupTimer, &QTimer::timeout, this, [this] {
        if (m_Active && !m_Ready && !m_Stopping)
            failAndStop(tr("Seamless host timed out waiting for ready"));
    });
    m_KillTimer.setSingleShot(true);
    m_KillTimer.setInterval(5000);
    connect(&m_KillTimer, &QTimer::timeout, this, [this] {
        // This QProcess is the exact child started by this manager. Never kill
        // by executable name or touch another DeskPort/compositor session.
        if (m_Process.state() != QProcess::NotRunning) {
            const auto run = m_RunId;
            if (m_TerminalStatus.isEmpty()) {
                m_TerminalStatus = tr("Seamless host did not stop; forcing termination");
                setStatus(m_TerminalStatus);
            }
            if (!m_Active || run != m_RunId) return;
            m_Process.kill();
        }
    });
}

SeamlessHostManager::~SeamlessHostManager() {
    if (m_Process.state() == QProcess::NotRunning) return;
    m_Process.disconnect(this);
    m_Process.terminate();
    // The sidecar gives its owned application group three seconds to exit and
    // then verifies the SIGKILL escalation. Do not kill the supervisor before
    // that group-cleanup window has completed.
    if (!m_Process.waitForFinished(5500)) {
        m_Process.kill();
        m_Process.waitForFinished(1000);
    }
}

QString SeamlessHostManager::binaryPath() {
    const auto override = qEnvironmentVariable("DESKPORT_SEAMLESS_HOST_BINARY");
    if (!override.isEmpty() && QDir::isAbsolutePath(override)) return QDir::cleanPath(override);
#ifdef Q_OS_LINUX
    return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                           QStringLiteral("/../libexec/deskport-seamless-host"));
#else
    return {};
#endif
}

bool SeamlessHostManager::available() const {
    const QFileInfo helper(binaryPath());
    return helper.isFile() && helper.isExecutable();
}

bool SeamlessHostManager::running() const {
    return m_Process.state() != QProcess::NotRunning;
}

QJsonArray SeamlessHostManager::windowStates() const {
    QJsonArray result;
    auto ids = m_Windows.keys();
    std::sort(ids.begin(), ids.end());
    for (const int id : ids) {
        const auto& window = m_Windows.constFind(id).value();
        result.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), window.title},
            {QStringLiteral("appId"), window.appId},
            {QStringLiteral("initialWidth"), window.initialSize.width()},
            {QStringLiteral("initialHeight"), window.initialSize.height()},
            {QStringLiteral("lastSequence"), static_cast<qint64>(window.lastSequence)},
            {QStringLiteral("pendingSequence"), static_cast<qint64>(window.pendingSequence)},
            {QStringLiteral("lastFrame"), window.lastFrame},
            {QStringLiteral("captureError"), window.captureError},
            {QStringLiteral("captureFileError"), window.captureFileMessage},
        });
    }
    return result;
}

QJsonObject SeamlessHostManager::localStatus() const {
    return QJsonObject{{"running", running()}, {"ready", ready()}, {"available", available()},
                       {"changing", running() && !ready()},
                       {"readiness", ready() ? "experimental" : "unavailable"},
                       {"displayWarning", QString()}, {"hostPort", 0},
                       {"status", status()}, {"windows", windowCount()},
                       {"windowStates", windowStates()}, {"waylandSocket", socketName()}};
}

bool SeamlessHostManager::start(const QStringList& application, const QSize& size) {
    if (m_Active || running() || m_Stopping) return false;
    if (!available()) {
        setStatus(tr("The experimental Seamless host helper is missing"));
        return false;
    }
    if (size.width() < MinimumWidth || size.height() < MinimumHeight ||
        size.width() > MaximumWidth || size.height() > MaximumHeight) {
        setStatus(tr("Unsupported Seamless workspace size"));
        return false;
    }
    for (const auto& argument : application) {
        if (argument.contains(QChar::Null)) {
            setStatus(tr("Seamless application arguments contain an invalid character"));
            return false;
        }
    }

    m_Output.clear();
    m_TerminalStatus.clear();
    m_LastWindowId = 0;
    m_Active = true;
    const auto run = ++m_RunId;
    m_Ready = false;
    m_Stopping = false;
    m_StopRequested = false;
    m_DiscardingLine = false;
    m_Windows.clear();
    m_SocketName.clear();
    QStringList arguments{QStringLiteral("--width"), QString::number(size.width()),
                          QStringLiteral("--height"), QString::number(size.height())};
    if (!application.isEmpty()) {
        arguments.append(QStringLiteral("--"));
        arguments.append(application);
    }
    m_Process.setProgram(binaryPath());
    m_Process.setArguments(arguments);
    setStatus(tr("Starting experimental Seamless host"));
    if (!m_Active || m_Stopping || run != m_RunId) return false;
    m_StartupTimer.start();
    m_Process.start();
    return true;
}

void SeamlessHostManager::stop() {
    beginStop(true);
}

void SeamlessHostManager::beginStop(bool requested) {
    if (!m_Active || m_Stopping) return;
    m_StartupTimer.stop();
    m_StopRequested = requested;
    if (requested && m_Process.state() == QProcess::NotRunning) {
        finishStop();
        return;
    }
    m_Stopping = true;
    m_Ready = false;
    const auto run = m_RunId;
    setStatus(m_TerminalStatus.isEmpty() ? tr("Stopping Seamless host") : m_TerminalStatus);
    if (!m_Active || run != m_RunId) return;
    if (m_Process.state() == QProcess::NotRunning) return;
    if (requested) m_Process.terminate();
    m_KillTimer.start();
}

void SeamlessHostManager::consumeOutput() {
    const auto run = m_RunId;
    // Bound our parser's allocation before copying data out of QProcess. The
    // sidecar's 32 KiB record limit includes its terminating newline.
    while (m_Process.bytesAvailable() > 0) {
        if (!m_Active) {
            m_Process.read(MaxControlLine);
            continue;
        }
        m_Output += m_Process.read(MaxControlLine - m_Output.size());
        int newline;
        while ((newline = m_Output.indexOf('\n')) >= 0) {
            const auto line = m_Output.left(newline);
            m_Output.remove(0, newline + 1);
            if (m_DiscardingLine) {
                m_DiscardingLine = false;
                continue;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (m_Stopping) {
                // Shutdown can still report a real cleanup failure. Only a
                // validated error may change diagnostics; no late lifecycle
                // event can restore readiness or enter a subsequent run.
                const auto event = document.object();
                if (m_TerminalStatus.isEmpty() && error.error == QJsonParseError::NoError &&
                    document.isObject() && integerField(event, QStringLiteral("version"), 1, 1, nullptr) &&
                    event.value(QStringLiteral("type")).toString() == QStringLiteral("error") &&
                    textField(event, QStringLiteral("code"), 255, false) &&
                    textField(event, QStringLiteral("message"), MaxMetadataText)) {
                    m_TerminalStatus = tr("Seamless host error: %1")
                        .arg(event.value(QStringLiteral("message")).toString());
                    setStatus(m_TerminalStatus);
                }
                if (!m_Active || run != m_RunId) return;
                continue;
            }
            if (error.error != QJsonParseError::NoError || !document.isObject() ||
                !acceptEvent(document.object())) {
                failAndStop(tr("Seamless host returned an invalid control or window lifecycle event"));
                return;
            }
            if (run != m_RunId) return;
            // A callback can stop the manager while handling a valid event.
            // Late ready/destroy events must not revive it or overwrite a failure.
            if (!m_Active) return;
        }
        if (m_Output.size() >= MaxControlLine) {
            m_Output.clear();
            m_DiscardingLine = true;
            if (!m_Stopping) {
                failAndStop(tr("Seamless host returned an oversized control message"));
                return;
            }
        }
    }
}

bool SeamlessHostManager::acceptEvent(const QJsonObject& event) {
    const auto run = m_RunId;
    if (!integerField(event, QStringLiteral("version"), 1, 1, nullptr) ||
        !event.value(QStringLiteral("type")).isString()) return false;
    const auto type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready")) {
        const auto socket = event.value(QStringLiteral("socket")).toString();
        static const QRegularExpression socketSyntax(
            QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.-]{0,63}\\z"));
        if (m_Ready || !m_Windows.isEmpty() || !socketSyntax.match(socket).hasMatch() ||
            !integerField(event, QStringLiteral("width"), MinimumWidth, MaximumWidth, nullptr) ||
            !integerField(event, QStringLiteral("height"), MinimumHeight, MaximumHeight, nullptr))
            return false;
        m_StartupTimer.stop();
        m_SocketName = socket;
        m_Ready = true;
        setStatus(tr("Experimental Seamless host is ready"));
    } else if (type == QStringLiteral("window-create")) {
        int id = 0;
        if (!m_Ready ||
            !integerField(event, QStringLiteral("id"), 1, 2147483647, &id) ||
            id <= m_LastWindowId || m_Windows.size() >= MaximumWindows ||
            !integerField(event, QStringLiteral("width"), 16, MaximumWidth, nullptr) ||
            !integerField(event, QStringLiteral("height"), 16, MaximumHeight, nullptr) ||
            !textField(event, QStringLiteral("title"), MaxMetadataText) ||
            !textField(event, QStringLiteral("appId"), MaxAppIdText)) return false;
        WindowState window;
        window.title = event.value(QStringLiteral("title")).toString();
        window.appId = event.value(QStringLiteral("appId")).toString();
        window.initialSize = QSize(event.value(QStringLiteral("width")).toInt(),
                                   event.value(QStringLiteral("height")).toInt());
        m_LastWindowId = id;
        m_Windows.insert(id, window);
        emit changed();
    } else if (type == QStringLiteral("window-destroy")) {
        int id = 0;
        if (!m_Ready || !integerField(event, QStringLiteral("id"), 1, 2147483647, &id) ||
            !m_Windows.remove(id)) return false;
        emit changed();
    } else if (type == QStringLiteral("window-title") || type == QStringLiteral("window-app-id")) {
        int id = 0;
        if (!m_Ready || !integerField(event, QStringLiteral("id"), 1, 2147483647, &id) ||
            !m_Windows.contains(id) ||
            !(type == QStringLiteral("window-title") ?
                  textField(event, QStringLiteral("title"), MaxMetadataText) :
                  textField(event, QStringLiteral("appId"), MaxAppIdText))) return false;
        auto& window = m_Windows[id];
        if (type == QStringLiteral("window-title"))
            window.title = event.value(QStringLiteral("title")).toString();
        else
            window.appId = event.value(QStringLiteral("appId")).toString();
    } else if (type == QStringLiteral("window-damage") || type == QStringLiteral("frame-metadata") ||
               type == QStringLiteral("frame-capture-failed") || type == QStringLiteral("capture-file-error")) {
        int id = 0;
        quint64 sequence = 0;
        if (!m_Ready || !integerField(event, QStringLiteral("id"), 1, 2147483647, &id) ||
            !m_Windows.contains(id) || !sequenceField(event, &sequence)) return false;
        auto& window = m_Windows[id];
        if (type == QStringLiteral("window-damage")) {
            if (window.pendingSequence || sequence <= window.lastSequence ||
                !textField(event, QStringLiteral("bufferType"), 32, false) ||
                !damageField(event, QStringLiteral("rectangles"), MaximumWidth, MaximumHeight) ||
                !event.value(QStringLiteral("rectanglesTruncated")).isBool()) return false;
            window.pendingSequence = sequence;
            window.captureFileError = false;
            window.captureFileMessage.clear();
        } else if (type == QStringLiteral("capture-file-error")) {
            if (sequence != window.pendingSequence || window.captureFileError ||
                !textField(event, QStringLiteral("message"), MaxMetadataText)) return false;
            window.captureFileError = true;
            window.captureFileMessage = event.value(QStringLiteral("message")).toString()
                .left(MaxSnapshotDiagnosticCharacters);
        } else {
            if (sequence <= window.lastSequence ||
                (window.pendingSequence && sequence != window.pendingSequence)) return false;
            if (type == QStringLiteral("frame-metadata")) {
                int width = 0, height = 0;
                static const QRegularExpression hashSyntax(QStringLiteral("\\A[0-9a-f]{64}\\z"));
                if (sequence != window.pendingSequence ||
                    !integerField(event, QStringLiteral("width"), 1, MaximumWidth, &width) ||
                    !integerField(event, QStringLiteral("height"), 1, MaximumHeight, &height) ||
                    event.value(QStringLiteral("pixelFormat")).toString() != QStringLiteral("rgba8888-premultiplied") ||
                    !hashSyntax.match(event.value(QStringLiteral("sha256")).toString()).hasMatch() ||
                    !damageField(event, QStringLiteral("damage"), width, height) ||
                    !event.value(QStringLiteral("damageTruncated")).isBool() ||
                    (event.contains(QStringLiteral("captureFile")) &&
                     !textField(event, QStringLiteral("captureFile"), 255, false))) return false;
            } else if (!textField(event, QStringLiteral("error"), MaxMetadataText, false)) {
                return false;
            }
            // Persist only validated diagnostic fields, never arbitrary JSON,
            // capture paths, buffers or pixels. A failed capture retains the last
            // successful frame with its own sequence, distinct from lastSequence.
            if (type == QStringLiteral("frame-metadata")) {
                window.lastFrame = QJsonObject{
                    {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
                    {QStringLiteral("width"), event.value(QStringLiteral("width"))},
                    {QStringLiteral("height"), event.value(QStringLiteral("height"))},
                    {QStringLiteral("sha256"), event.value(QStringLiteral("sha256"))},
                };
                window.captureError.clear();
            } else {
                window.captureError = event.value(QStringLiteral("error")).toString()
                    .left(MaxSnapshotDiagnosticCharacters);
            }
            window.lastSequence = sequence;
            window.pendingSequence = 0;
        }
    } else if (type == QStringLiteral("child-start")) {
        // An explicitly selected application starts before the ready event.
        if (!integerField(event, QStringLiteral("pid"), 1, 2147483647, nullptr) ||
            !textField(event, QStringLiteral("program"), MaxMetadataText, false)) return false;
    } else if (type == QStringLiteral("child-exit")) {
        const auto status = event.value(QStringLiteral("exitStatus")).toString();
        if (!integerField(event, QStringLiteral("exitCode"), -2147483647 - 1, 2147483647, nullptr) ||
            (status != QStringLiteral("normal") && status != QStringLiteral("crashed"))) return false;
    } else if (type == QStringLiteral("window-rejected") || type == QStringLiteral("popup-rejected")) {
        if (!m_Ready || !textField(event, QStringLiteral("reason"), MaxMetadataText, false)) return false;
    } else if (type == QStringLiteral("error")) {
        if (!textField(event, QStringLiteral("code"), 255, false) ||
            !textField(event, QStringLiteral("message"), MaxMetadataText)) return false;
        failAndStop(tr("Seamless host error: %1").arg(event.value(QStringLiteral("message")).toString()));
        return true;
    } else if (type == QStringLiteral("stopping")) {
        if (!textField(event, QStringLiteral("reason"), MaxMetadataText, false)) return false;
        beginStop(false);
        return true;
    } else {
        return false;
    }
    if (m_Active && !m_Stopping && run == m_RunId) emit eventReceived(event);
    return true;
}

void SeamlessHostManager::failAndStop(const QString& status) {
    if (!m_Active || m_Stopping) return;
    if (m_TerminalStatus.isEmpty()) m_TerminalStatus = status;
    stop();
}

void SeamlessHostManager::setStatus(const QString& status) {
    if (m_Status == status) return;
    m_Status = status;
    emit changed();
}

void SeamlessHostManager::finishStop() {
    if (!m_Active || m_Finishing) return;
    m_Finishing = true;
    m_StartupTimer.stop();
    m_KillTimer.stop();
    // Keep the run active while notifying status observers. They may stop or
    // try to restart synchronously; a new run is admitted only from stopped().
    m_Stopping = true;
    m_Ready = false;
    m_Windows.clear();
    m_SocketName.clear();
    m_Output.clear();
    const auto status = m_TerminalStatus.isEmpty() ? tr("Seamless host is off") : m_TerminalStatus;
    if (m_Status == status)
        emit changed(); // Cleanup changes readiness/window state even when diagnostics persist.
    else
        setStatus(status);
    m_Active = false;
    m_Stopping = false;
    m_Finishing = false;
    emit stopped();
}
