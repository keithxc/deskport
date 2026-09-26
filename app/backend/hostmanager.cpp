#include "diagnostics.h"
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "hostmanager.h"
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "workspaceresolution.h"
#include "peerstore.h"
#include "serviceconfig.h"
#include <QElapsedTimer>
#include <algorithm>
#include <limits>
#include <QSslError>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QNetworkProxy>
#include <QApplication>
#include <QPalette>
#include <QWindow>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDesktopServices>
#include <QUuid>
#include <QTimer>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include "../../host/macos/recovery-policy.h"
#include <QSettings>
#include <QPointer>
#include <QActionGroup>
#include <QRegularExpression>
#include <QHostInfo>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include "macpermissions.h"
#include "macdock.h"
#include "macunattended.h"
#endif

HostManager::HostManager(QObject *parent, const QString &directory) : QObject(parent) {
    m_Isolated = !directory.isEmpty();
#ifdef Q_OS_WIN
    // Own only our QProcess children. Never look up or terminate hosts by name.
    auto job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        m_WindowsHostJob = job;
        for (auto child : {&m_Server, &m_Credentials}) {
            connect(child, &QProcess::started, this, [this, child] {
                auto process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(child->processId()));
                const bool owned = process && AssignProcessToJobObject(HANDLE(m_WindowsHostJob), process);
                if (process) CloseHandle(process);
                if (!owned) {
                    child->kill();
                    beginStop(tr("Cannot establish private Windows host process ownership"));
                }
            });
        }
    } else if (job) CloseHandle(job);
#endif
    m_RecoveryTimer.setSingleShot(true);
    connect(&m_RecoveryTimer, &QTimer::timeout, this, [this] {
        if (!m_DesiredSharing || m_ShuttingDown || running()) return;
        start(m_RequestedWidth, m_RequestedHeight);
        if (!running()) scheduleRecovery();
    });
    if (!m_Isolated) {
        const int savedPort = QSettings().value("host/port", DeskPortNetwork::DefaultBasePort).toInt();
        if (DeskPortNetwork::isPrivateBase(savedPort)) m_BasePort = savedPort;
    }
    connect(this, &HostManager::displayResized, this, [this](int sequence, int, int, const QString& error) {
        if (sequence >= 0 || m_QueuedDisplayRequest.isEmpty()) return;
        const auto pending = m_QueuedDisplayRequest; m_QueuedDisplayRequest = {};
        // Both native helpers can retain failed local recovery while validating
        // an independent workspace for the next authenticated controller.
        if (!resizeDisplay(pending["width"].toInt(), pending["height"].toInt(),
                pending["scale"].toInt(), pending["seq"].toInt(), pending["policy"].toInt())) {
            emit displayResized(pending["seq"].toInt(), m_DisplayWidth, m_DisplayHeight,
                                error.isEmpty() ? QStringLiteral("Display restoration did not complete") : error);
        }
    });
    // Drain child pipes even while disabled. Raw data remains transient; no raw
    // file handles are inherited, and toggles affect already-running children.
    const auto consumeHost = [this](const QByteArray& output) {
        Diagnostics::instance().ingest("host", output);
        // Operational status must work without diagnostic files. Fixed messages only.
        m_DiagnosticStatusBuffer = (m_DiagnosticStatusBuffer + output).right(4096);
        if (m_DiagnosticStatusBuffer.contains("No screen capture permission"))
            setStatus(tr("Screen recording permission is required. Authorize DeskPort in system settings, then restart sharing."));
        else if (m_DiagnosticStatusBuffer.contains("Video failed to find working encoder"))
            setStatus(tr("Screen capture could not start. Check screen recording permission and encoder availability."));
    };
    connect(&m_Server, &QProcess::readyReadStandardOutput, this, [this, consumeHost] { consumeHost(m_Server.readAllStandardOutput()); });
    connect(&m_Server, &QProcess::readyReadStandardError, this, [this, consumeHost] { consumeHost(m_Server.readAllStandardError()); });
    connect(&m_Display, &QProcess::readyReadStandardError, this, [this] { Diagnostics::instance().ingest("display", m_Display.readAllStandardError()); });
    m_Network.setProxy(QNetworkProxy::NoProxy);
    m_Directory = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/host" : directory;
    m_Status = available() ? tr("Sharing is off") : tr("The bundled DeskPort host is missing. Repair the installation to enable sharing.");
    connect(&m_Display, &QProcess::readyReadStandardOutput, this, [this] {
        m_Buffer += m_Display.readAllStandardOutput();
        while (m_Buffer.contains('\n')) {
            auto index = m_Buffer.indexOf('\n');
            auto object = QJsonDocument::fromJson(m_Buffer.left(index)).object();
            m_Buffer.remove(0, index + 1);
            if (m_Stopping) continue;
            if (object.contains("caret")) { emit caretChanged(object["caret"].toObject()); continue; }
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
            if (object["ready"].toBool() && m_Starting && !m_ServerRequested) {
                m_DisplayWidth = m_DisplayHeight = 0;
#ifdef Q_OS_LINUX
                m_LinuxOutputName = object["outputName"].toString();
                m_LinuxGnome = object["gnome"].toBool();
                if (m_LinuxOutputName.isEmpty() || m_LinuxOutputName.contains(QRegularExpression("[^A-Za-z0-9_-]"))) {
                    beginStop(tr("Invalid virtual display identity")); return;
                }
#endif
                startServer(0);
                continue;
            }
#endif
            if (object.contains("seq")) {
                if (object["seq"].toInt() == m_DisplayWireSequence && m_DisplaySequence != 0) {
                    const int sequence = m_DisplaySequence; m_DisplaySequence = 0;
                    m_DisplayWarning = object.contains("layoutWarning") ? tr("The remote workspace is ready, but some physical screens could not be configured. Check the host display layout.") : QString();
                    if (!object.contains("error")) {
                        m_DisplayWidth = object["width"].toInt(); m_DisplayHeight = object["height"].toInt();
                        m_DisplayScale = object["scale"].toInt(1);
#ifdef Q_OS_MACOS
                        QSaveFile target(m_Directory + "/capture-display");
                        if (!target.open(QIODevice::WriteOnly)) object["error"] = tr("Cannot save capture display identity");
                        else {
                            target.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                            target.write(QByteArray::number(object["displayId"].toInt()) + ' ' +
                                QByteArray::number(m_DisplayWidth) + ' ' + QByteArray::number(m_DisplayHeight) + ' ' +
                                QByteArray::number(m_DisplayScale) + '\n');
                            if (!target.commit()) object["error"] = tr("Cannot save capture display identity");
                        }
#endif
#ifdef Q_OS_LINUX
                        if (object.contains("outputName") && object["active"].toBool(true)) {
                            const auto output = object["outputName"].toString();
                            if (output.isEmpty() || output.size() > 128 || output.contains(QRegularExpression("[^A-Za-z0-9_-]"))) {
                                beginStop(tr("Invalid virtual display identity")); return;
                            }
                            m_LinuxOutputName = output;
                            m_LinuxPipewireNode = quint32(object.value("pipewireNode").toDouble());
                            if (object.contains("pipewireNode")) m_LinuxGnome = true;
                            m_LinuxPipewireSerial = object["pipewireSerial"].toString();
                        }
                        if (object.contains("active") && !object["active"].toBool()) {
                            m_DisplayWidth = m_DisplayHeight = 0;
                            QFile::remove(m_Directory + "/virtual-display.json");
                        } else
#endif
                        if (!saveLinuxDisplayState()) object["error"] = tr("Cannot save virtual display capture state");
                    }
                    emit displayResized(sequence, m_DisplayWidth, m_DisplayHeight, object["error"].toString());
                    emit changed();
                }
                continue;
            }
            if (object.contains("error")) {
                qWarning() << "Virtual display startup failed:" << object["error"].toString();
#ifdef Q_OS_MACOS
                // A private virtual display can stay unavailable indefinitely: a
                // mirror set WindowServer will not detach, or a private API that
                // stops answering. Retrying it alone leaves the host unreachable,
                // so share this Mac's own screen instead until sharing is cycled.
                if (!m_PhysicalFallback && ++m_DisplayFailures >= 3) {
                    m_PhysicalFallback = true;
                    beginStop(tr("A private virtual display is unavailable (%1). Sharing this Mac's own screen instead.")
                        .arg(object["error"].toString()));
                    return;
                }
#endif
#ifdef Q_OS_WIN
                // Display setup requires interactive authorization. Retrying a
                // denied or failed lease would repeatedly raise UAC dialogs.
                m_DesiredSharing = false;
                m_RecoveryTimer.stop();
                if (!m_Isolated) QSettings().setValue("host/shareOnLaunch", false);
#endif
                beginStop(object["error"].toString()); return;
            }
            if (object["displayId"].toInt() > 0 && m_Starting && !m_ServerRequested) {
#ifdef Q_OS_WIN
                m_WindowsVirtualDisplay = object["virtual"].toBool();
                m_DisplayModes = object["displayModes"].toArray();
#endif
                m_DisplayWidth = object["width"].toInt(); m_DisplayHeight = object["height"].toInt();
                m_DisplayScale = object["scale"].toInt(1);
#ifdef Q_OS_LINUX
                m_LinuxOutputName = object["outputName"].toString();
                m_LinuxPipewireNode = quint32(object.value("pipewireNode").toDouble());
                if (object.contains("pipewireNode")) m_LinuxGnome = true;
                m_LinuxPipewireSerial = object["pipewireSerial"].toString();
                if (m_LinuxOutputName.isEmpty() || m_LinuxOutputName.size() > 128 ||
                    m_LinuxOutputName.contains(QRegularExpression("[^A-Za-z0-9_-]"))) {
                    beginStop(tr("Invalid virtual display identity")); return;
                }
                if (!saveLinuxDisplayState()) { beginStop(tr("Cannot save virtual display capture state")); return; }
#endif
                startServer(object["displayId"].toInt());
            }
        }
    });
    connect(&m_Server, &QProcess::started, this, [this] {
        Diagnostics::instance().record("host", "Host started");
        if (m_Stopping) { m_Server.terminate(); return; }
        m_Starting = false;
        m_DisplayFailures = 0;
        if (!m_Isolated) QSettings().setValue("host/port", m_BasePort);
        setStatus(tr("Sharing has started. Connect from an approved device to check picture, sound and control."));
        const auto generation = m_Generation;
        QTimer::singleShot(60000, this, [this, generation] {
            if (generation == m_Generation && m_Server.state() == QProcess::Running) m_RecoveryAttempt = 0;
        });

    });
    const auto watchProcess = [this](QProcess *process, const QString &label) {
        connect(process, &QProcess::errorOccurred, this, [this, process, label](QProcess::ProcessError) {
            if (m_Stopping) { finishStop(); return; }
            beginStop(tr("%1 failed: %2. You can retry after cleanup.").arg(label, process->errorString()));
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, label](int code, QProcess::ExitStatus) {
            if (m_Stopping) { finishStop(); return; }
            beginStop(tr("%1 stopped (%2). See host logs, then retry.").arg(label).arg(code));
        });
    };
    connect(&m_Display, &QProcess::started, this, [] { Diagnostics::instance().record("display", "Display started"); });
    watchProcess(&m_Server, tr("Host"));
    watchProcess(&m_Display, tr("Virtual display"));
    connect(&m_Credentials, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_Stopping) { finishStop(); return; }
        beginStop(tr("Could not initialize host authentication"));
    });
    connect(&m_Credentials, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exitStatus) {
        if (m_Stopping) { finishStop(); return; }
        if (!m_Starting) return;
        if (code != 0 || exitStatus != QProcess::NormalExit) {
            beginStop(tr("Could not initialize host authentication")); return;
        }
        m_DiagnosticStatusBuffer.clear();
#ifdef Q_OS_WIN
        m_Server.setWorkingDirectory(QFileInfo(serverPath()).absolutePath());
#else
        m_Server.setWorkingDirectory(m_Directory);
#endif
        m_Server.setProcessChannelMode(QProcess::SeparateChannels);
        // The child cannot inherit these listeners. Release immediately before
        // starting it; bind failures still clean up only our own processes.
        m_Ports.release();
        m_Server.start(serverPath(), {m_Directory + "/sunshine.conf"});
    });
    m_Menu = new QMenu;
    connect(m_Menu->addAction(tr("Open device list")), &QAction::triggered, this, &HostManager::showDevicesRequested);
    connect(m_Menu->addAction(tr("Reconnect")), &QAction::triggered, this, &HostManager::reconnectRequested);
    connect(m_Menu->addAction(tr("Disconnect")), &QAction::triggered, this, &HostManager::disconnectRequested);
    // Restarting from the tray is how a remote viewer picks up a version that a
    // package upgrade already wrote to disk: the running process keeps the old
    // binary until it exits, and a clean exit never comes back on its own.
    connect(m_Menu->addAction(tr("Restart")), &QAction::triggered, this, &HostManager::requestRestart);
    connect(m_Menu->addAction(tr("Quit")), &QAction::triggered, this, &HostManager::requestExit);
    // The left button shows and hides the window; the menu belongs to the right
    // one. A menu attached to a macOS status item is opened by either button and
    // suppresses the button action entirely, so it is popped up natively there.
#ifndef Q_OS_MACOS
    m_Tray.setContextMenu(m_Menu);
#endif
    connect(&m_Tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // A double click arrives after its own Trigger. Toggling twice would
        // undo itself, so only the single click acts.
        if (reason == QSystemTrayIcon::Trigger) emit toggleWindowRequested();
#ifdef Q_OS_MACOS
        else if (reason == QSystemTrayIcon::Context) showTrayMenu();
#endif
    });
    updateTrayIcon();
    qApp->installEventFilter(this);
    m_Tray.setToolTip("DeskPort");
    if (!m_Isolated) m_Tray.show();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { m_ShuttingDown = true; m_RecoveryTimer.stop(); beginStop(tr("Sharing is off")); });
#ifdef Q_OS_MACOS
    if (!m_Isolated) {
        // A manual launch resumes a prior "Pause and quit"; the paused helper
        // never launches us itself. A normal login still follows the login item.
        if (unattendedEnabled()) unattendedMarker("paused", false);
        refreshUnattended();
        auto monitor = new QTimer(this);
        connect(monitor, &QTimer::timeout, this, [this] {
            if (unattendedEnabled()) refreshUnattended();
        });
        monitor->start(10000);
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { refreshUnattended(); });
    }
#endif
    if (directory.isEmpty() && setupComplete() && !QSettings().contains("host/startAtLogin")) setLoginStart(true);
    if (directory.isEmpty() && loginStart()) setLoginStart(true); // Refresh installed paths and older startup entries.
    if (directory.isEmpty() && available() && ((loginStart() && !QSettings().value("host/sharingDisabled", false).toBool()) || QSettings().value("host/shareOnLaunch", false).toBool()) &&
            !QCoreApplication::arguments().contains("--no-host-autostart")) {
        QTimer::singleShot(0, this, [this] {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        });
    }
}
#ifdef Q_OS_MACOS
void HostManager::showTrayMenu() {
    if (auto chosen = deskPortShowStatusMenu(m_Menu)) chosen->trigger();
}
#endif
void HostManager::updateTrayIcon() {
#ifdef Q_OS_MACOS
    // AppKit renders a template image with the menu bar's current contrast,
    // including wallpaper-dependent appearance and selected menu items.
    QIcon icon(":/res/deskport-tray-black.svg");
    icon.setIsMask(true);
#else
    const bool lightForeground = qApp->palette().color(QPalette::WindowText).lightness() > 127;
    QIcon icon(lightForeground ? ":/res/deskport-tray-white.svg" : ":/res/deskport-tray-black.svg");
#endif
    m_Tray.setIcon(icon);
}
void HostManager::requestExit() {
    if (m_ExitRequested) return;
#ifdef Q_OS_MACOS
    if (!m_Isolated && unattendedEnabled() && !m_RestartRequested) {
        QMessageBox question(QMessageBox::Question, tr("Unattended operation"),
            tr("Pause automatic recovery and quit? Recovery resumes the next time DeskPort opens."),
            QMessageBox::Cancel);
        auto pause = question.addButton(tr("Pause and quit"), QMessageBox::AcceptRole);
        question.setDefaultButton(QMessageBox::Cancel);
        question.exec();
        if (question.clickedButton() != pause || !unattendedMarker("paused", true)) return;
    }
#endif
    m_ExitRequested = true;
    emit exitRequested();
}
void HostManager::requestRestart() {
    if (m_ExitRequested) return;
    m_RestartRequested = true;
    setStatus(tr("Restarting DeskPort"));
    requestExit();
}
void HostManager::scheduleRecovery() {
    if (!m_DesiredSharing || m_ShuttingDown || m_RecoveryTimer.isActive()) return;
    const int delay = qMin(60, 5 * (1 << qMin(m_RecoveryAttempt++, 4)));
    m_RecoveryTimer.start(delay * 1000);
    setStatus(m_StopStatus + tr(" Retrying automatically in %1 seconds.").arg(delay));
}
bool HostManager::eventFilter(QObject* watched, QEvent* event) {
    if (watched == qApp && event->type() == QEvent::Quit && m_Resident && !m_ExitRequested && !qApp->isSavingSession()) {
        emit hideRequested(); return true;
    }
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) updateTrayIcon();
    return QObject::eventFilter(watched, event);
}
HostManager::~HostManager() {
    m_ShuttingDown = true; m_RecoveryTimer.stop();
    // The event loop may already be gone during application shutdown. Normal UI
    // stops are asynchronous; only destruction waits for our own child processes.
    beginStop(tr("Sharing is off"));
    for (auto process : {&m_Credentials, &m_Server, &m_Display}) {
        process->disconnect(this);
        if (process == &m_Display) process->closeWriteChannel();
        if (process->state() != QProcess::NotRunning && !process->waitForFinished(2500)) {
            process->kill(); process->waitForFinished(1000);
        }
    }
#ifdef Q_OS_WIN
    if (m_WindowsHostJob) CloseHandle(HANDLE(m_WindowsHostJob));
#endif
    delete m_Menu;
}
QString HostManager::helperPath() const {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/host/deskport-display.exe";
#elif defined(Q_OS_LINUX)
    return QCoreApplication::applicationDirPath() + "/../libexec/deskport-display";
#else
    return QCoreApplication::applicationDirPath() + "/../Helpers/deskport-display";
#endif
}
QString HostManager::serverPath() const {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/host/deskport-host.exe";
#elif defined(Q_OS_LINUX)
    return QCoreApplication::applicationDirPath() + "/../libexec/deskport-host";
#else
    return QCoreApplication::applicationDirPath() + "/../Helpers/Sunshine.app/Contents/MacOS/Sunshine";
#endif
}
bool HostManager::available() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return QFile::exists(helperPath()) && QFile::exists(serverPath());
#elif defined(Q_OS_LINUX)
    return QFile::exists(serverPath());
#else
    return false;
#endif
}
bool HostManager::running() const {
    return m_Starting || m_Stopping || m_Credentials.state() != QProcess::NotRunning ||
        m_Server.state() != QProcess::NotRunning || m_Display.state() != QProcess::NotRunning;
}
bool HostManager::canPair() const { return !m_Starting && !m_Stopping && m_Server.state() == QProcess::Running; }
void HostManager::setStatus(const QString &value) {
#ifdef Q_OS_WIN
    qInfo() << "Windows host status:" << value;
#endif
    m_Status = value; emit changed();
}
void HostManager::start(int width, int height) {
    if (running()) return;
    if (!available()) {
        setStatus(tr("The bundled DeskPort host is missing. Repair the installation to enable sharing."));
        return;
    }
#ifdef Q_OS_WIN
    if (!m_WindowsHostJob) { setStatus(tr("Cannot create private Windows host process ownership")); return; }
#endif
    if (width < 640 || width > 3840 || height < 360 || height > 2160 || width % 2 || height % 2) {
        setStatus(tr("Unsupported display size")); return;
    }
    m_DesiredSharing = true;
    m_RequestedWidth = width; m_RequestedHeight = height;
    m_RecoveryTimer.stop();
    if (!QDir().mkpath(m_Directory + "/credentials")) {
        setStatus(tr("Cannot create host state directory")); return;
    }
    m_HostLock.reset(new QLockFile(m_Directory + "/instance.lock"));
    // Hosting is long-lived. Age alone must never let another instance take over.
    m_HostLock->setStaleLockTime(0);
    if (!m_HostLock->tryLock(0)) {
        m_HostLock.reset();
        setStatus(tr("Another DeskPort instance is already using this host state. Open that instance to manage sharing.")); return;
    }
    const bool liveIsolated = m_Isolated && QCoreApplication::arguments().contains("--host-session-test");
    const int testBase = liveIsolated ? qEnvironmentVariableIntValue("DESKPORT_TEST_BASE_PORT") : 0;
    if (testBase) {
        if (testBase < DeskPortNetwork::DefaultBasePort ||
            testBase >= DeskPortNetwork::DefaultBasePort + DeskPortNetwork::PortStep * DeskPortNetwork::PortChoices ||
            (testBase - DeskPortNetwork::DefaultBasePort) % DeskPortNetwork::PortStep) {
            beginStop(tr("Invalid isolated test port group")); return;
        }
        m_BasePort = testBase;
    } else if (liveIsolated && m_BasePort == DeskPortNetwork::DefaultBasePort)
        m_BasePort += DeskPortNetwork::PortStep * (1 + (qHash(m_Directory) % (DeskPortNetwork::PortChoices - 1)));
    const int selectedPort = m_Ports.reserve(m_BasePort,
        m_Isolated && !liveIsolated ? QHostAddress::LocalHost : QHostAddress::AnyIPv4);
    if (!selectedPort) {
        beginStop(tr("No free DeskPort port group is available. Existing services were left unchanged.")); return;
    }
    m_BasePort = selectedPort;
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile credentials(m_Directory + "/control-secret");
    if (credentials.open(QIODevice::ReadOnly)) m_Password = QString::fromUtf8(credentials.readAll()).trimmed();
    credentials.close();
    if (m_Password.isEmpty()) {
        m_Password = QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile save(credentials.fileName());
        if (!save.open(QIODevice::WriteOnly)) { beginStop(tr("Cannot save host credentials")); return; }
        save.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        save.write(m_Password.toUtf8());
        if (!save.commit()) { beginStop(tr("Cannot save host credentials")); return; }
    }
    if (!m_Isolated) {
        QSettings settings;
        settings.setValue("host/width", width); settings.setValue("host/height", height);
        settings.setValue("host/shareOnLaunch", true);
        settings.setValue("host/sharingDisabled", false);
    }
    m_Buffer.clear(); m_Starting = true; m_ServerRequested = false;
    const auto generation = ++m_Generation;
    // stderr is consumed by the shared diagnostics sink; stdout is the control protocol.
#ifdef Q_OS_LINUX
    m_LinuxOutputName.clear(); m_LinuxPipewireNode = 0; m_LinuxGnome = false;
    QFile::remove(m_Directory + "/virtual-display.json");
    const auto desktops = qgetenv("XDG_CURRENT_DESKTOP").split(':');
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") || (!desktops.contains("KDE") && !desktops.contains("GNOME"))) {
        setStatus(tr("Starting desktop sharing…"));
        startServer(0);
    } else
#endif
    {
#ifdef Q_OS_MACOS
        if (m_PhysicalFallback) {
            m_DisplayWidth = m_DisplayHeight = 0; m_DisplayScale = 1; m_DisplayModes = {};
            setStatus(tr("Sharing this Mac's own screen…"));
            startServer(int(CGMainDisplayID()));
        } else
#endif
        {
            auto displayEnvironment = QProcessEnvironment::systemEnvironment();
            displayEnvironment.insert("DESKPORT_DISPLAY_ON_DEMAND", "1");
            if (m_Isolated) {
                displayEnvironment.insert("DESKPORT_DISPLAY_ISOLATED", "1");
                displayEnvironment.insert("DESKPORT_DISPLAY_STATE_DIR", m_Directory);
                const auto digest = QCryptographicHash::hash(m_Directory.toUtf8(), QCryptographicHash::Sha256).toHex().left(8);
                displayEnvironment.insert("DESKPORT_DISPLAY_SERIAL", QString::number(digest.toUInt(nullptr, 16) | 0x80000000u));
            } else {
                displayEnvironment.remove("DESKPORT_DISPLAY_ISOLATED");
                displayEnvironment.remove("DESKPORT_DISPLAY_STATE_DIR");
                displayEnvironment.remove("DESKPORT_DISPLAY_SERIAL");
            }
#ifdef Q_OS_WIN
            displayEnvironment.insert("DESKPORT_DISPLAY_STATE_DIR", m_Directory);
#endif
            m_Display.setProcessEnvironment(displayEnvironment);
            m_Display.start(helperPath(), {QString::number(width), QString::number(height)});
#ifdef Q_OS_WIN
            setStatus(tr("Preparing Windows display sharing…"));
#else
            setStatus(tr("Preparing on-demand desktop sharing…"));
#endif
        }
    }
    // Windows may show normal UAC consent for the owned display lease.
#ifdef Q_OS_WIN
    constexpr int displayStartupTimeout = 60000;
#else
    constexpr int displayStartupTimeout = 15000;
#endif
    QTimer::singleShot(displayStartupTimeout, this, [this, generation] {
        if (m_Starting && m_Generation == generation) {
#ifdef Q_OS_WIN
            // A timeout may be unanswered UAC, not a transient media error.
            // Retrying would repeatedly summon an administrative prompt.
            m_DesiredSharing = false;
            m_RecoveryTimer.stop();
            if (!m_Isolated) QSettings().setValue("host/shareOnLaunch", false);
#endif
            beginStop(tr("Host startup timed out; see logs"));
        }
    });
}
void HostManager::startServer(int displayId) {
    m_ServerRequested = true;
    QSaveFile config(m_Directory + "/sunshine.conf");
    if (!config.open(QIODevice::WriteOnly)) { beginStop(tr("Cannot write host configuration")); return; }
    config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    config.write(QString("file_apps = %1/apps.json\nfile_state = %1/state.json\npkey = %1/credentials/key.pem\ncert = %1/credentials/cert.pem\ncredentials_file = %1/control.json\nlog_path = %2\n").arg(m_Directory, QProcess::nullDevice()).toUtf8());
    config.write("stream_audio = enabled\n");
    QString deviceName = QHostInfo::localHostName().left(64);
    deviceName.replace('\n', ' '); deviceName.replace('\r', ' ');
    if (deviceName.trimmed().isEmpty()) deviceName = "DeskPort";
    config.write(QString("sunshine_name = %2\nport = %1\naddress_family = ipv4\nupnp = disabled\nsystem_tray = disabled\nmin_log_level = 2\norigin_web_ui_allowed = pc\n").arg(m_BasePort).arg(deviceName).toUtf8());
#ifdef Q_OS_MACOS
    config.write(QString("output_name = %1\n").arg(displayId).toUtf8());
    auto hostEnvironment = QProcessEnvironment::systemEnvironment();
    hostEnvironment.insert("DESKPORT_CAPTURE_DISPLAY", QString::number(displayId));
    if (!displayId) {
        QFile::remove(m_Directory + "/capture-display");
        hostEnvironment.insert("DESKPORT_CAPTURE_DISPLAY_FILE", m_Directory + "/capture-display");
    } else hostEnvironment.remove("DESKPORT_CAPTURE_DISPLAY_FILE");
    hostEnvironment.insert("DESKPORT_SMART_STREAMING", "1");
    hostEnvironment.insert("DESKPORT_HOST_OS", QSysInfo::prettyProductName());
    m_Server.setProcessEnvironment(hostEnvironment);
    m_Credentials.setProcessEnvironment(hostEnvironment);
#else
    Q_UNUSED(displayId);
    auto hostEnvironment = QProcessEnvironment::systemEnvironment();
    hostEnvironment.insert("DESKPORT_HOST_OS", QSysInfo::prettyProductName());
#ifdef Q_OS_WIN
    hostEnvironment.insert("DESKPORT_HOST_STATE_DIR", m_Directory);
    hostEnvironment.insert("DESKPORT_SMART_STREAMING", "1");
    config.write("dd_configuration_option = disabled\n");
    // The helper owns mode restoration; Sunshine must not race it.
    QFile displayState(m_Directory + "/windows-display.json");
    if (displayState.open(QIODevice::ReadOnly)) {
        const auto output = QJsonDocument::fromJson(displayState.readAll()).object()["output"].toString();
        if (!output.isEmpty() && !output.contains('\n') && !output.contains('\r'))
            config.write(QString("output_name = %1\n").arg(output).toUtf8());
    }
#endif
#ifdef Q_OS_LINUX
    if (!m_LinuxOutputName.isEmpty()) {
        hostEnvironment.insert("DESKPORT_VIRTUAL_DISPLAY", m_Directory + "/virtual-display.json");
        hostEnvironment.insert("DESKPORT_ON_DEMAND_DISPLAY", "1");
    }
    else hostEnvironment.remove("DESKPORT_VIRTUAL_DISPLAY");
#endif
    m_Server.setProcessEnvironment(hostEnvironment);
    m_Credentials.setProcessEnvironment(hostEnvironment);
#endif
#ifdef Q_OS_LINUX
    config.write(QString("output_name = %1\n").arg(m_LinuxOutputName).toUtf8());
    config.write(!m_LinuxOutputName.isEmpty() && !m_LinuxGnome ? "capture = kwin\n" : "capture = portal\n");
#endif
    if (!config.commit()) { beginStop(tr("Cannot save host configuration")); return; }
#ifdef Q_OS_WIN
    // Upstream resolves immutable assets relative to cwd, while every mutable
    // state/config path above remains absolute and private to DeskPort.
    m_Credentials.setWorkingDirectory(QFileInfo(serverPath()).absolutePath());
#else
    m_Credentials.setWorkingDirectory(m_Directory);
#endif
    m_Credentials.setStandardOutputFile(QProcess::nullDevice());
    m_Credentials.setStandardErrorFile(QProcess::nullDevice());
    m_Credentials.start(serverPath(), {m_Directory + "/sunshine.conf", "--creds", "deskport", m_Password});
    const auto generation = m_Generation;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_Generation && m_Credentials.state() != QProcess::NotRunning)
            beginStop(tr("Host authentication timed out; see logs"));
    });
}
void HostManager::stop() {
    m_DesiredSharing = false; m_RecoveryTimer.stop(); m_RecoveryAttempt = 0;
    m_DisplayFailures = 0; m_PhysicalFallback = false;
    if (!m_Isolated) {
        QSettings().setValue("host/shareOnLaunch", false);
        QSettings().setValue("host/sharingDisabled", true);
    }
    beginStop(available() ? tr("Sharing is off") : tr("The bundled DeskPort host is missing. Repair the installation to enable sharing."));
}
void HostManager::beginStop(const QString &status) {
    m_DisplaySequence = 0; m_QueuedDisplayRequest = {}; ++m_DisplayGeneration;
    if (m_Stopping) return;
    m_Stopping = true;
    const auto generation = ++m_Generation;
    m_Starting = false;
    m_StopStatus = status;
    setStatus(tr("Stopping sharing…"));
    for (auto process : {&m_Credentials, &m_Server}) {
        if (process->state() != QProcess::NotRunning) process->terminate();
    }
    finishStop();
    QTimer::singleShot(2500, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        for (auto process : {&m_Credentials, &m_Server}) {
            if (process->state() != QProcess::NotRunning) process->kill();
        }
        finishStop();
    });
    QTimer::singleShot(3500, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        if (m_Display.state() != QProcess::NotRunning) m_Display.kill();
        finishStop();
    });
}
void HostManager::finishStop() {
    if (!m_Stopping) return;
    if (m_Credentials.state() != QProcess::NotRunning || m_Server.state() != QProcess::NotRunning) return;
    // Release the display only after the host has stopped using it.
    if (m_Display.state() != QProcess::NotRunning) { m_Display.closeWriteChannel(); return; }
    // QProcess may emit errorOccurred and finished together. Keep cleanup active
    // through both signals so a terminated sibling cannot replace the root error.
    const auto generation = m_Generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (!m_Stopping || generation != m_Generation) return;
        if (m_Credentials.state() != QProcess::NotRunning ||
            m_Server.state() != QProcess::NotRunning || m_Display.state() != QProcess::NotRunning) return;
#ifdef Q_OS_LINUX
        QFile::remove(m_Directory + "/virtual-display.json");
#endif
        m_Ports.release();
        m_HostLock.reset();
        m_Stopping = false;
        setStatus(m_StopStatus);
        scheduleRecovery();
    });
}
void HostManager::sessionControl(const QJsonObject& body, QObject* context,
                                 std::function<void(QJsonObject)> completion) {
    managementRequest(QStringLiteral("sessions"), body, context, std::move(completion));
}

void HostManager::managementRequest(const QString& path, const QJsonObject& body, QObject* context,
                                    std::function<void(QJsonObject)> completion) {
    const auto certificates = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    if (!running() || certificates.isEmpty()) {
        completion({{"status", false}, {"code", "unavailable"}}); return;
    }
    const auto generation = m_Generation;
    const auto expected = certificates.first();
    QNetworkRequest request(QUrl(QString("https://127.0.0.1:%1/api/deskport/%2").arg(m_BasePort + 1).arg(path)));
    request.setTransferTimeout(10000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Basic " + ("deskport:" + m_Password).toUtf8().toBase64());
    auto reply = body.isEmpty() ? m_Network.get(request) :
        m_Network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, qOverload<const QList<QSslError>&>(&QNetworkReply::sslErrors), reply,
            [reply, expected](const QList<QSslError>&) {
        if (reply->sslConfiguration().peerCertificate() == expected) reply->ignoreSslErrors();
    });
    const QPointer<QObject> alive(context);
    connect(reply, &QNetworkReply::finished, this, [this, reply, alive, generation, expected, completion] {
        const auto object = QJsonDocument::fromJson(reply->readAll()).object();
        const bool valid = generation == m_Generation && running() &&
            reply->error() == QNetworkReply::NoError &&
            reply->sslConfiguration().peerCertificate() == expected && object["version"].toInt() == 1 &&
            object["status"].isBool();
        reply->deleteLater();
        if (alive) completion(valid ? object : QJsonObject{{"status", false}, {"code", "unavailable"}});
    });
}

void HostManager::pair(const QString &pin, const QString &name) {
    if (!canPair()) return;
    const auto generation = m_Generation;
    if (pin.size() != 4 || !std::all_of(pin.begin(), pin.end(), [](QChar c) { return c >= '0' && c <= '9'; })) {
        setStatus(tr("Enter the four-digit PIN shown on the connecting device")); return;
    }
    const auto certificates = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    if (certificates.isEmpty()) { setStatus(tr("Host certificate is not ready")); return; }
    const auto expectedCertificate = certificates.first();
    const auto pinCertificate = [expectedCertificate](QNetworkReply *reply) {
        connect(reply, qOverload<const QList<QSslError>&>(&QNetworkReply::sslErrors), reply,
                [reply, expectedCertificate](const QList<QSslError>&) {
            if (reply->sslConfiguration().peerCertificate() == expectedCertificate) reply->ignoreSslErrors();
        });
    };
    QNetworkRequest request(QUrl(QString("https://127.0.0.1:%1/api/pin").arg(m_BasePort + 1)));
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Basic " + ("deskport:" + m_Password).toUtf8().toBase64());
    // Current Sunshine requires an explicit pending-request ID, not just a PIN.
    auto pending = m_Network.get(request);
    pinCertificate(pending);
    connect(pending, &QNetworkReply::finished, this, [this, pending, request, pinCertificate, pin, name, generation] {
        if (generation != m_Generation || !canPair()) { pending->deleteLater(); return; }
        const auto pairings = QJsonDocument::fromJson(pending->readAll()).object()["pairings"].toArray();
        const bool ok = pending->error() == QNetworkReply::NoError;
        pending->deleteLater();
        if (!ok || pairings.size() != 1) {
            setStatus(tr("Keep exactly one connecting device's PIN dialog open, then retry pairing.")); return;
        }
        const auto id = pairings.first().toObject()["id"].toString();
        const auto clientName = name.trimmed().isEmpty() ? QStringLiteral("DeskPort client") : name.trimmed().left(32);
        auto reply = m_Network.post(request, QJsonDocument(QJsonObject{{"pairing_id", id}, {"pin", pin}, {"name", clientName}}).toJson());
        pinCertificate(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
            if (generation != m_Generation || !canPair()) { reply->deleteLater(); return; }
            const auto object = QJsonDocument::fromJson(reply->readAll()).object();
            setStatus(reply->error() == QNetworkReply::NoError && object["status"].toBool()
                      ? tr("Pairing accepted") : tr("Pairing failed. Keep the PIN dialog open on the connecting device and retry."));
            reply->deleteLater();
        });
    });
}
int HostManager::sharingWidth() const { return QSettings().value("host/width", 2560).toInt(); }
int HostManager::sharingHeight() const { return QSettings().value("host/height", 1440).toInt(); }
QString HostManager::deviceName() const { return QHostInfo::localHostName(); }
QUrl HostManager::applicationUrl() const {
#ifdef Q_OS_MACOS
    return QUrl::fromLocalFile(QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../.."));
#else
    return QUrl::fromLocalFile(QCoreApplication::applicationFilePath());
#endif
}
bool HostManager::setupComplete() const { return QSettings().value("setup/completed", false).toBool(); }
void HostManager::completeSetup() {
    QSettings().setValue("setup/completed", true);
    if (!m_Isolated && !QSettings().contains("host/startAtLogin")) setLoginStart(true);
    emit permissionsChanged();
}
QString HostManager::readiness() const {
    bool pending = false;
    for (const auto& value : permissions()) {
        const auto permission = value.toMap();
        if (permission.value("key") == "microphone") continue;
        const auto state = permission.value("state").toString();
        if (state == "denied" || state == "needsSetup" || state == "restricted") return "attention";
        if (state != "allowed") pending = true;
    }
    return pending ? "unverified" : "allowed";
}
void HostManager::refreshPermissions() { emit permissionsChanged(); }
QVariantList HostManager::permissions() const {
    QVariantList result;
    const auto add = [&result](const QString& key, const QString& title, const QString& purpose, const QString& state) {
        result.append(QVariantMap{{"key",key}, {"title",title}, {"purpose",purpose}, {"state",state}});
    };
#ifdef Q_OS_MACOS
    add("screen", tr("Screen & system audio"), tr("Let a connected device see this desktop and hear its sound."),
        CGPreflightScreenCaptureAccess() ? "allowed" : "denied");
    add("input", tr("Keyboard & mouse"), tr("Let a device you approve control this computer."),
        AXIsProcessTrusted() ? "allowed" : "denied");
    add("microphone", tr("Audio input"), tr("Needed only when your sharing audio path uses microphone access."), deskPortMicrophoneStatus());
#elif defined(Q_OS_WIN)
    add("screen", tr("Desktop capture"), tr("Shares the selected Windows display in this signed-in session."), "onShare");
    add("input", tr("Keyboard & mouse"), tr("Controls ordinary applications in this session. Windows may restrict elevated applications and secure desktops."), "onShare");
#else
    add("screen", tr("Desktop capture"), tr("KDE uses the current desktop. Other desktops may ask you to choose a screen when sharing starts."), "onShare");
    add("input", tr("Keyboard & mouse"), tr("Remote control requires access to the system input device."),
        QFileInfo("/dev/uinput").isWritable() ? "allowed" : "needsSetup");
#endif
    return result;
}
void HostManager::revealApplication() {
#ifdef Q_OS_MACOS
    QProcess::startDetached("/usr/bin/open", {"-R", applicationUrl().toLocalFile()});
#endif
}
void HostManager::permission(const QString &kind) {
#ifdef Q_OS_MACOS
    if (kind == "screen") CGRequestScreenCaptureAccess();
    else if (kind == "input") CGRequestPostEventAccess();
    QString pane;
    if (kind == "screen") pane = "ScreenCapture";
    else if (kind == "input") pane = "Accessibility";
    else if (kind == "microphone") pane = "Microphone";
    if (!pane.isEmpty()) QDesktopServices::openUrl(QUrl(
        "x-apple.systempreferences:com.apple.preference.security?Privacy_" + pane));
#else
    Q_UNUSED(kind);
#endif
    refreshPermissions();
}
void HostManager::openLogs() { Diagnostics::instance().feedback(); }

bool HostManager::loginStartManaged() const {
#ifdef Q_OS_LINUX
    return DeskPortService::storeManaged(DeskPortService::autostartPath()) ||
           DeskPortService::storeManaged(DeskPortService::unitPath());
#else
    return false;
#endif
}
bool HostManager::loginStart() const {
#ifdef Q_OS_LINUX
    // 系统配置接管时以磁盘为准: 自启由它开关, 我们自己那份 QSettings 不作数。
    if (loginStartManaged())
        return QFile::exists(DeskPortService::autostartPath()) || QFile::exists(DeskPortService::unitPath());
#endif
    return QSettings().value("host/startAtLogin", false).toBool();
}
QString HostManager::unattendedDirectory() const {
    return m_Isolated ? m_Directory + "/unattended" : QDir::homePath() + QString::fromLatin1(DeskPortRecovery::Directory);
}
bool HostManager::unattendedMarker(const QString& name, bool present) {
    const auto directory = unattendedDirectory();
    const auto path = directory + "/" + name;
    bool ok = true;
    if (present) {
        ok = QDir().mkpath(directory);
        QSaveFile file(path);
        ok = ok && file.open(QIODevice::WriteOnly);
        if (ok) {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            ok = file.write("1\n") == 2 && file.commit();
        }
    } else ok = !QFile::exists(path) || QFile::remove(path);
    if (!ok) m_UnattendedError = tr("Cannot save unattended preferences.");
    emit changed();
    return ok;
}
bool HostManager::unattendedEnabled() const {
#ifdef Q_OS_MACOS
    // Staged builds and App Translocation must not resume the installed app's
    // paused recovery or read its opt-in as their own.
    if (!m_Isolated && QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..") != "/Applications/DeskPort.app") return false;
    return QFileInfo(unattendedDirectory() + "/enabled").isFile();
#else
    return false;
#endif
}
bool HostManager::unattendedNeedsApproval() const {
    return unattendedEnabled() && m_UnattendedServiceStatus == 2;
}
QString HostManager::unattendedStatus() const {
    if (!m_UnattendedError.isEmpty()) return m_UnattendedError;
    if (!unattendedEnabled()) return tr("Off");
    if (m_UnattendedServiceStatus == 2) return tr("Waiting for approval in System Settings");
    if (m_UnattendedServiceStatus != 1) return tr("Recovery service needs setup. Turn it off and on to try again.");
    const auto path = unattendedDirectory() + "/heartbeat";
    QFile heartbeat(path);
    if (!heartbeat.open(QIODevice::ReadOnly)) {
        if (QFileInfo(unattendedDirectory() + "/enabled").lastModified().secsTo(QDateTime::currentDateTime()) <= 90)
            return tr("Enabled; waiting for the recovery service to check in");
        return tr("Recovery has not checked in. Review background permissions.");
    }
    if (QFileInfo(path).lastModified().secsTo(QDateTime::currentDateTime()) > 90)
        return tr("Recovery has not checked in. Review background permissions.");
    const auto result = heartbeat.read(64).trimmed();
    if (result == "running") return tr("Enabled; recovery service is checking this Mac");
    if (result == "waiting-session") return tr("Waiting for a logged-in desktop session");
    return tr("Recovery needs attention. Check login startup and background permissions.");
}
void HostManager::refreshUnattended() {
#ifdef Q_OS_MACOS
    if (!m_Isolated) m_UnattendedServiceStatus = unattendedEnabled() ? deskPortUnattendedServiceStatus() : 0;
#endif
    emit changed();
}
void HostManager::openUnattendedSettings() {
#ifdef Q_OS_MACOS
    if (!m_Isolated) deskPortOpenBackgroundItems();
#endif
}
void HostManager::setUnattended(bool enabled) {
#ifdef Q_OS_MACOS
    if (m_Isolated) return;
    m_UnattendedError.clear();
    const auto bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
    if (bundle != "/Applications/DeskPort.app" ||
        (enabled && !QFile::exists(bundle + "/Contents/Helpers/deskport-recovery"))) {
        m_UnattendedError = tr("Install DeskPort in Applications before enabling unattended operation.");
        emit changed(); return;
    }
    if (enabled) {
        setLoginStart(true);
        if (!loginStart() || !QFile::exists(QDir::homePath() + "/Library/LaunchAgents/io.github.keithxc.DeskPort.plist")) {
            m_UnattendedError = tr("Cannot enable login startup."); emit changed(); return;
        }
        // Remove stale health evidence before requesting a fresh registration.
        if (!unattendedMarker("paused", false) || !unattendedMarker("heartbeat", false) ||
            !unattendedMarker("enabled", true)) return;
        if (!deskPortSetUnattendedService(true, m_UnattendedError)) unattendedMarker("enabled", false);
    } else {
        // Stop this user's recovery first, even when unregister needs attention.
        if (!unattendedMarker("enabled", false)) return;
        unattendedMarker("paused", false);
        deskPortSetUnattendedService(false, m_UnattendedError);
    }
    refreshUnattended();
    if (unattendedNeedsApproval()) openUnattendedSettings();
#else
    Q_UNUSED(enabled);
#endif
}
void HostManager::setLoginStart(bool enabled) {
    if (!enabled && unattendedEnabled()) {
        setStatus(tr("Turn off unattended operation before disabling login startup.")); emit changed(); return;
    }
#ifdef Q_OS_MACOS
    const QString bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../..");
    if (bundle != "/Applications/DeskPort.app") {
        setStatus(tr("Install DeskPort in Applications before enabling login startup")); emit changed(); return;
    }
    const QString path = QDir::homePath() + "/Library/LaunchAgents/io.github.keithxc.DeskPort.plist";
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        file.write(DeskPortService::launchAgent().toUtf8());
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QSettings().setValue("host/startAtLogin", enabled);
    emit changed();
#elif defined(Q_OS_LINUX)
    const QString path = DeskPortService::autostartPath();
    if (loginStartManaged()) {
        // Nix 优先: 它已经装好了自启, 这里一个字都不写, 免得两边来回覆盖。
        setStatus(tr("Login startup is managed by your system configuration")); emit changed(); return;
    }
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString executable = DeskPortService::persistentExecutable(
            QCoreApplication::applicationDirPath() + "/deskport", QString::fromLocal8Bit(qgetenv("APPIMAGE")));
        const QString unitPath = DeskPortService::unitPath();
        QDir().mkpath(QFileInfo(unitPath).absolutePath());
        QSaveFile unit(unitPath);
        if (!unit.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        unit.write(DeskPortService::systemdUnit(executable).toUtf8());
        if (!unit.commit()) { setStatus(tr("Cannot save login startup")); return; }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Cannot install login startup")); return; }
        file.write(DeskPortService::desktopEntry().toUtf8());
        if (!file.commit()) { setStatus(tr("Cannot save login startup")); return; }
    } else if (QFile::exists(path) && !QFile::remove(path)) {
        setStatus(tr("Cannot remove login startup")); return;
    }
    QProcess::startDetached("systemctl", {"--user", "daemon-reload"});
    QSettings().setValue("host/startAtLogin", enabled); emit changed();
#elif defined(Q_OS_WIN)
    QSettings startup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) startup.setValue("io.github.keithxc.DeskPort", "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" --background");
    else startup.remove("io.github.keithxc.DeskPort");
    startup.sync();
    if (startup.status() != QSettings::NoError) { setStatus(tr("Cannot update Windows login startup")); return; }
    QSettings().setValue("host/startAtLogin", enabled); emit changed();
#else
    Q_UNUSED(enabled);
#endif
}

bool HostManager::prepareIdentity(const QByteArray& certificate, const QByteArray& key) {
    if (running() || !QDir().mkpath(m_Directory + "/credentials")) return false;
    QLockFile lock(m_Directory + "/instance.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return false;
    QFile::setPermissions(m_Directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    if (QFile::exists(m_Directory + "/credentials/cert.pem") != QFile::exists(m_Directory + "/credentials/key.pem")) return false;
    for (const auto& entry : {qMakePair(QString("cert.pem"), certificate), qMakePair(QString("key.pem"), key)}) {
        const QString path = m_Directory + "/credentials/" + entry.first;
        if (QFile::exists(path)) continue;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(entry.second) != entry.second.size() || !file.commit()) return false;
    }
    bool ok;
    auto state = PeerStore::read(m_Directory + "/state.json", &ok);
    if (!ok) return false;
    auto root = state["root"].toObject();
    if (root["uniqueid"].toString().isEmpty()) {
        root["uniqueid"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper();
        root["named_devices"] = QJsonArray();
        state["root"] = root;
        if (!PeerStore::write(m_Directory + "/state.json", state)) return false;
    }
    return !identity()["hostCert"].toString().isEmpty();
}
QJsonObject HostManager::identity() const {
    const auto certs = QSslCertificate::fromPath(m_Directory + "/credentials/cert.pem");
    return {{"hostId", PeerStore::read(m_Directory + "/state.json")["root"].toObject()["uniqueid"]},
            {"hostPort", m_BasePort}, {"hostCert", certs.isEmpty() ? QString() : QString::fromUtf8(certs.first().toPem())}};
}
void HostManager::updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove) {
    if (m_TrustBusy) { emit trustUpdated(false); return; }
    m_TrustBusy = true;
    if (!remove && running()) {
        // Binding grants permission only. Never stop an existing stream to add
        // trust, including when an older/mismatched helper lacks this endpoint.
        managementRequest(QStringLiteral("trust"), {{"uuid", id}, {"name", name},
            {"cert", QString::fromUtf8(certificate.toPem())}}, this, [this](QJsonObject result) {
            m_TrustBusy = false;
            emit trustUpdated(result["status"].toBool());
        });
        return;
    }
    const bool restart = running();
    stop();
    auto timer = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
    connect(timer, &QTimer::timeout, this, [=] {
        if (running() && elapsed->elapsed() < 7000) return;
        timer->stop(); timer->deleteLater();
        bool ok = false;
        if (!running()) {
            QLockFile lock(m_Directory + "/instance.lock"); lock.setStaleLockTime(0);
            if (lock.tryLock(0)) ok = PeerStore::trust(m_Directory + "/state.json", id, name, certificate, remove);
        }
        m_TrustBusy = false;
        if (restart) {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        }
        emit trustUpdated(ok);
    });
    timer->start(50);
}

bool HostManager::saveLinuxDisplayState() {
#ifdef Q_OS_LINUX
    if (!m_LinuxOutputName.isEmpty()) {
        return PeerStore::write(m_Directory + "/virtual-display.json", {
            {"node", double(m_LinuxPipewireNode)}, {"serial", m_LinuxPipewireSerial}, {"output", m_LinuxOutputName},
            {"width", m_DisplayWidth}, {"height", m_DisplayHeight}, {"scale", m_DisplayScale}});
    }
#endif
    return true;
}

bool HostManager::physicalDisplaySharing() const {
#ifdef Q_OS_WIN
    QSettings owned(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\DeskPort"), QSettings::NativeFormat);
    const auto instance=owned.value("VirtualDisplayDevice").toString();
    return !m_WindowsVirtualDisplay && !instance.startsWith("ROOT\\DESKPORTVIRTUALDISPLAY\\", Qt::CaseInsensitive);
#else
    return false;
#endif
}
bool HostManager::adaptiveDisplayAvailable() const {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    return running() && !changing() && m_Display.state() == QProcess::Running;
#else
    return false;
#endif
}
bool HostManager::displayPoliciesAvailable() const {
#ifdef Q_OS_WIN
    return adaptiveDisplayAvailable() && m_WindowsVirtualDisplay;
#elif defined(Q_OS_LINUX)
    return adaptiveDisplayAvailable() && !m_LinuxGnome;
#else
    return adaptiveDisplayAvailable();
#endif
}
bool HostManager::resizeDisplay(int width, int height, int scale, int sequence, int policy) {
    if (policy < 0 || policy > 2 || !adaptiveDisplayAvailable() || !m_QueuedDisplayRequest.isEmpty() || sequence == 0 || width < 640 || width > DeskPortDisplay::MaxWidth ||
        height < 360 || height > DeskPortDisplay::MaxHeight || width % 4 || height % 4 || (scale != 1 && scale != 2)) return false;
    if (m_DisplaySequence) {
        if (m_DisplaySequence > 0 || sequence < 0) return false;
        // Finish the previous session's restoration before a new client takes over.
        m_QueuedDisplayRequest = {{"width", width}, {"height", height}, {"scale", scale},
                                  {"seq", sequence}, {"policy", policy}};
        return true;
    }
    m_DisplaySequence = sequence;
    m_DisplayWireSequence = m_DisplayWireSequence == std::numeric_limits<int>::max() ? 1 : m_DisplayWireSequence + 1;
    const auto generation = ++m_DisplayGeneration;
    m_Display.write(QJsonDocument(QJsonObject{{"seq", m_DisplayWireSequence}, {"width", width}, {"height", height}, {"scale", scale}, {"session", sequence > 0}, {"displayPolicy", policy}}).toJson(QJsonDocument::Compact) + '\n');
    QTimer::singleShot(5000, this, [this, generation] {
        if (m_DisplaySequence && generation == m_DisplayGeneration) {
            const auto sequence = m_DisplaySequence; m_DisplaySequence = 0;
            emit displayResized(sequence, m_DisplayWidth, m_DisplayHeight, tr("Virtual display resize timed out"));
        }
    });
    return true;
}
void HostManager::settleSessionDisplay(QObject* context, std::function<void(bool)> completion) {
    auto pending = new QObject(context);
    auto finished = std::make_shared<bool>(false);
    auto finish = [pending, finished, completion](bool ok) {
        if (*finished) return;
        *finished = true; completion(ok); pending->deleteLater();
    };
    connect(this, &HostManager::displayResized, pending, [finish](int sequence, int, int, const QString& error) {
        if (sequence < 0) finish(error.isEmpty());
    });
    QTimer::singleShot(10000, pending, [finish] { finish(false); });
    restoreDisplay();
}
void HostManager::restoreDisplay() {
    m_QueuedDisplayRequest = {}; // A disconnected queued controller must never take over later.
    // Video renegotiation retains the controller. Restore promptly on actual
    // disconnect, while allowing an already pending helper request to settle.
    const auto generation = m_DisplayGeneration;
    QTimer::singleShot(250, this, [this, generation] {
        if (generation != m_DisplayGeneration) return;
        if (m_DisplaySequence) { restoreDisplay(); return; }
        resizeDisplay(sharingWidth(), sharingHeight(), 1, -1);
    });
}
