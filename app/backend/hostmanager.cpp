#include "diagnostics.h"
#include <QSysInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_LINUX
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#include "hostmanager.h"
#include "hostcaretmonitor.h"
#include <QScreen>
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
#include <QThread>
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
namespace {
#ifdef Q_OS_LINUX
// KWin EOF recovery: pre-removal preparation (5 s), final recovery (20 s),
// and owned recovery-process reap (1 s). The timer starts at display EOF.
constexpr int DisplayShutdownGraceMs = 26000;
#else
constexpr int DisplayShutdownGraceMs = 9000;
#endif
// Explicit troubleshooting opt-in only. Diagnostics keep fixed event classes and
// drop raw text; DESKPORT_HOST_LOG names a private file that receives the
// helpers' raw output (addresses and paths included), capped at 32 MiB.
void rawHostLog(const QByteArray& output) {
    static const QString path = qEnvironmentVariable("DESKPORT_HOST_LOG");
    static QFile* file = nullptr;
    if (path.isEmpty() || output.isEmpty()) return;
    if (!file) {
        file = new QFile(path);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append) ||
            !file->setPermissions(QFile::ReadOwner | QFile::WriteOwner)) { file->close(); return; }
    }
    if (!file->isOpen() || file->size() > 32 * 1024 * 1024) return;
    file->write(output); file->flush();
}
}

HostManager::HostManager(QObject *parent, const QString &directory, bool interactive) : QObject(parent) {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    m_CaretMonitor=new HostCaretMonitor(this,[this](const QJsonObject& caret){emit caretChanged(caret);});
    connect(&m_Display,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this]{m_CaretMonitor->stop();});
#endif
    m_Isolated = !directory.isEmpty();
#ifdef Q_OS_LINUX
    // Only our Sunshine children own these ports. A crashed GUI must release
    // them even if the child ignores SIGTERM; the display helper separately
    // receives stdin EOF so it can restore the physical layout first.
    for (auto child : {&m_Server, &m_Credentials}) {
        const auto parentPid = ::getpid();
        child->setChildProcessModifier([child, parentPid] {
            if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
                child->failChildProcessModifier("prctl(PR_SET_PDEATHSIG)", errno);
#else
                ::_exit(127);
#endif
            }
            // The parent may die between fork and installing the signal. A
            // subreaper can adopt us, so comparing against PID 1 is insufficient.
            if (::getppid() != parentPid) ::_exit(127);
        });
    }
#endif
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
    if (m_Isolated) {
        const int testParkMs = qEnvironmentVariableIntValue("DESKPORT_TEST_PARK_TIMEOUT_MS");
        if (testParkMs > 0) m_ParkedDisplayTimeoutMs = testParkMs;
    }
    m_ParkedDisplayTimer.setSingleShot(true);
    connect(&m_ParkedDisplayTimer, &QTimer::timeout, this, [this] { unparkDisplay(true); });
    connect(&m_ParkedInputMonitor, &QTimer::timeout, this, [this] {
        if (!m_DisplayParked) { m_ParkedInputMonitor.stop(); return; }
        const auto current = localHidEventCount();
        if (!m_ParkedInputArmed) {
            m_ParkedHidCounter = current;
            m_ParkedInputArmed = true;
            m_ParkedInputMonitor.start(1000);
            return;
        }
        if (current != m_ParkedHidCounter) unparkDisplay(true);
    });
    m_UnattendedMonitor.setInterval(10000);
    connect(&m_UnattendedMonitor, &QTimer::timeout, this, &HostManager::refreshUnattended);
    connect(&m_UnattendedWatcher, &QFileSystemWatcher::directoryChanged, this, &HostManager::refreshUnattended);
    connect(&m_UnattendedWatcher, &QFileSystemWatcher::fileChanged, this, &HostManager::refreshUnattended);
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
        rawHostLog(output);
        // Operational status must work without diagnostic files. Fixed messages only.
        m_DiagnosticStatusBuffer = (m_DiagnosticStatusBuffer + output).right(4096);
        if (m_DiagnosticStatusBuffer.contains("No screen capture permission"))
            setStatus(tr("Screen recording permission is required. Authorize DeskPort in system settings, then restart sharing."));
        else if (m_DiagnosticStatusBuffer.contains("Video failed to find working encoder"))
            setStatus(tr("Screen capture could not start. Check screen recording permission and encoder availability."));
    };
    connect(&m_Server, &QProcess::readyReadStandardOutput, this, [this, consumeHost] { consumeHost(m_Server.readAllStandardOutput()); });
    connect(&m_Server, &QProcess::readyReadStandardError, this, [this, consumeHost] { consumeHost(m_Server.readAllStandardError()); });
    connect(&m_Display, &QProcess::readyReadStandardError, this, [this] {
        const auto output = m_Display.readAllStandardError();
        Diagnostics::instance().ingest("display", output);
        rawHostLog(output);
    });
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
                m_LinuxGnomePolicies = m_LinuxGnome && object["displayPolicy"].toInt() == 1;
                m_LinuxHyprland = object["backend"].toString() == "hyprland";
                m_LinuxHyprlandPolicies = m_LinuxHyprland && object.value("displayPolicy").toBool();
                m_LinuxHyprlandPolicyRequests = m_LinuxHyprland && object.contains("displayPolicy");
                m_LinuxX11 = object["backend"].toString() == "x11";
                if (m_LinuxOutputName.isEmpty() || m_LinuxOutputName.contains(QRegularExpression("[^A-Za-z0-9_-]"))) {
                    beginStop(tr("Invalid virtual display identity")); return;
                }
#endif
                startServer(0);
                continue;
            }
#endif
#ifdef Q_OS_LINUX
            if (object.contains("layoutWarning") && !object.contains("seq")) {
                m_DisplayWarning = object["layoutWarning"].toString().isEmpty() ? QString() :
                    tr("The remote workspace is ready, but some physical screens could not be configured. Check the host display layout.");
                emit changed();
                continue;
            }
#endif
            if (object.contains("seq")) {
                if (object["seq"].toInt() == m_DisplayWireSequence && m_DisplaySequence != 0) {
                    const int sequence = m_DisplaySequence; m_DisplaySequence = 0;
                    m_DisplayWarning = (object.contains("layoutWarning") || (m_LinuxHyprland && !m_LinuxHyprlandPolicies)) ? tr("The remote workspace is ready, but some physical screens could not be configured. Check the host display layout.") : QString();
#ifdef Q_OS_WIN
                    if(object["dpiLimited"].toBool())m_DisplayWarning=tr("The host's minimum desktop size limits scaling. Use full screen or a larger window.");
#endif
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
                            if (m_LinuxX11) m_LinuxX11Lease = object["leaseToken"].toString();
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
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
                    if(sequence>0 && !object.contains("error")) {
                        m_CaretMonitor->start(helperPath(),[this]() -> QStringList {
#ifdef Q_OS_WIN
                            QFile capture(m_Directory+"/windows-capture-output");
                            if(!capture.open(QIODevice::ReadOnly)) return {};
                            const auto name=QString::fromUtf8(capture.read(512)).trimmed();
                            return name.isEmpty() ? QStringList{} : QStringList{name};
#else
                            // Qt and AT-SPI use desktop logical coordinates on
                            // Wayland. Never substitute the primary/physical screen.
                            for(auto* screen:QGuiApplication::screens()) if(screen->name()==m_LinuxOutputName) {
                                const auto r=screen->geometry();
                                return {QString::number(r.x()),QString::number(r.y()),QString::number(r.width()),QString::number(r.height())};
                            }
                            return {};
#endif
                        });
                    }
#endif
                    emit displayResized(sequence, m_DisplayWidth, m_DisplayHeight, object["error"].toString());
                    emit changed();
                }
                continue;
            }
            if (object.contains("error")) {
                Diagnostics::instance().record("display", "DeskPort failure stage=virtual-display code=display-failed");
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
#ifdef Q_OS_LINUX
                if (m_LinuxX11) {
                    // An explicit unsupported X11 selection needs repair or a
                    // user retry, never physical fallback or an endless loop.
                    m_DesiredSharing = false;
                    m_RecoveryTimer.stop();
                    if (!m_Isolated) QSettings().setValue("host/shareOnLaunch", false);
                }
#endif
#ifdef Q_OS_WIN
                // A missing/denied installed display broker needs repair or an
                // explicit retry. Do not repeatedly request a failed lease.
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
                m_LinuxHyprland = object["backend"].toString() == "hyprland";
                m_LinuxHyprlandPolicies = m_LinuxHyprland && object.value("displayPolicy").toBool();
                m_LinuxHyprlandPolicyRequests = m_LinuxHyprland && object.contains("displayPolicy");
                m_LinuxX11 = object["backend"].toString() == "x11";
                if (object.contains("pipewireNode")) m_LinuxGnome = true;
                m_LinuxGnomePolicies = m_LinuxGnome && object["displayPolicy"].toInt() == 1;
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
    // Return to native menu tracking before dispatching any application work.
    // The target is captured when the item is chosen, not when work runs.
    const auto later = [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); };
    for (int i = 0; i < MaxTraySessions; ++i) {
        auto slot = m_Menu->addAction(QString());
        slot->setCheckable(true); slot->setVisible(false);
        connect(slot, &QAction::triggered, this, [this, slot, later] {
            slot->setChecked(slot->property("presenting").toBool()); // A checkmark reflects the viewer only.
            const auto id = slot->data().toString();
            if (!id.isEmpty()) later([this, id] { emit sessionSelected(id); });
        });
        m_SessionSlots.append(slot);
    }
    m_AllSessions = m_Menu->addAction(QString());
    m_AllSessions->setVisible(false);
    connect(m_AllSessions, &QAction::triggered, this, &HostManager::showDevicesRequested, Qt::QueuedConnection);
    m_SessionSeparator = m_Menu->addSeparator();
    m_SessionSeparator->setVisible(false);
    connect(m_Menu->addAction(tr("Open device list")), &QAction::triggered, this, &HostManager::showDevicesRequested, Qt::QueuedConnection);
    m_DisconnectSession = m_Menu->addAction(QString());
    m_DisconnectSession->setVisible(false);
    connect(m_DisconnectSession, &QAction::triggered, this, [this, later] {
        const auto id = m_CurrentSession;
        if (!id.isEmpty()) later([this, id] { emit sessionDisconnectRequested(id); });
    });
    m_Menu->addSeparator();
    auto more = m_Menu->addMenu(tr("More"));
    m_ReconnectSession = more->addAction(QString());
    m_ReconnectSession->setVisible(false);
    connect(m_ReconnectSession, &QAction::triggered, this, [this, later] {
        const auto id = m_CurrentSession;
        if (!id.isEmpty()) later([this, id] { emit sessionReconnectRequested(id); });
    });
    m_StreamTitleBar = more->addAction(tr("Hide title bar while viewing a remote desktop"));
    m_StreamTitleBar->setCheckable(true);
    connect(m_StreamTitleBar, &QAction::triggered, this, [this, later](bool hidden) {
        later([this, hidden] { emit streamTitleBarToggled(hidden); });
    });
    // Restarting from the tray is how a remote viewer picks up a version that a
    // package upgrade already wrote to disk: the running process keeps the old
    // binary until it exits, and a clean exit never comes back on its own.
    connect(more->addAction(tr("Restart DeskPort")), &QAction::triggered, this, &HostManager::requestRestart, Qt::QueuedConnection);
    m_Menu->addSeparator();
    connect(m_Menu->addAction(tr("Quit")), &QAction::triggered, this, &HostManager::requestExit, Qt::QueuedConnection);
    // The device-list order can change while the menu is closed.
    connect(m_Menu, &QMenu::aboutToShow, this, &HostManager::trayMenuAboutToShow);
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
    if (!m_Isolated && interactive) m_Tray.show();
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { m_ShuttingDown = true; m_RecoveryTimer.stop(); updateUnattendedMonitor(); beginStop(tr("Sharing is off")); });
#ifdef Q_OS_MACOS
    if (!m_Isolated && interactive) {
        // A manual launch resumes a prior "Pause and quit"; the paused helper
        // never launches us itself. A normal login still follows the login item.
        if (unattendedEnabled()) unattendedMarker("paused", false);
        m_UnattendedMonitoring = true;
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { refreshUnattended(); });
        refreshUnattended();
    }
#endif
    if (interactive && directory.isEmpty() && !loginStartManaged()) {
        if (setupComplete() && !QSettings().contains("host/startAtLogin")) setLoginStart(true);
        if (loginStart()) setLoginStart(true); // Refresh only startup entries owned by the GUI.
    }
#ifdef Q_OS_WIN
    // A fresh Windows profile shares on first launch, before setup is completed.
    // Keep an explicit stop authoritative, including profiles without the newer key.
    const bool defaultShareOnLaunch = !QSettings().value("host/sharingDisabled", false).toBool();
#else
    const bool defaultShareOnLaunch = false;
#endif
    if (interactive && directory.isEmpty() && available() && ((loginStart() && !QSettings().value("host/sharingDisabled", false).toBool()) || QSettings().value("host/shareOnLaunch", defaultShareOnLaunch).toBool()) &&
            !QCoreApplication::arguments().contains("--no-host-autostart")) {
        QTimer::singleShot(0, this, [this] {
            QSettings settings;
            start(settings.value("host/width", 2560).toInt(), settings.value("host/height", 1440).toInt());
        });
    }
}
#ifdef Q_OS_MACOS
void HostManager::showTrayMenu() {
    if (m_NativeMenuPending || m_NativeMenuTracking || m_ExitRequested) return;
    m_NativeMenuPending = true;
    deskPortAfterStatusItemTracking(this, [this] {
        m_NativeMenuPending = false;
        if (m_ExitRequested) return;
        emit trayMenuAboutToShow();
        m_NativeMenuTracking = true;
        const QPointer<QAction> chosen = deskPortShowStatusMenu(m_Menu);
        // Queued handlers capture the selected session before refreshing rows.
        if (chosen) chosen->trigger();
        m_NativeMenuTracking = false;
        updateSessionMenu(m_PendingSessionRows);
    });
}
#endif
void HostManager::setStreamTitleBarHidden(bool hidden) {
    if (m_StreamTitleBar) m_StreamTitleBar->setChecked(hidden);
}
void HostManager::updateSessionMenu(const QVariantList& sessions) {
    m_PendingSessionRows = sessions;
    if (m_NativeMenuTracking || m_Menu->isVisible()) {
        if (!m_SessionMenuRefreshQueued) {
            m_SessionMenuRefreshQueued = true;
            QTimer::singleShot(100, this, [this] { m_SessionMenuRefreshQueued=false; updateSessionMenu(m_PendingSessionRows); });
        }
        return;
    }
    // Rows arrive in device-list order. Only live connections are listed.
    QList<QVariantMap> live;
    for (const auto& value : sessions) {
        const auto row = value.toMap();
        const auto state = row.value("state").toString();
        if (state == "connected" || state == "starting" || state == "stopping") live.append(row);
    }
    // Actions address the presented desktop, else the last one shown if still live.
    QVariantMap current;
    for (const auto& row : live) if (row.value("visible").toBool()) current = row;
    if (current.isEmpty())
        for (const auto& row : live) if (row.value("selected").toBool() && row.value("state") != "stopping") current = row;
    QString key = current.value("id").toString();
    for (const auto& row : live)
        key += '\n' + row.value("id").toString() + '\t' + row.value("name").toString() + '\t' +
               row.value("state").toString() + '\t' + (row.value("visible").toBool() ? "1" : "0");
    if (key == m_SessionMenuKey && !m_SessionMenuKey.isNull()) return;
    m_SessionMenuKey = key;
    const auto label = [](QString name) { return name.replace('&', QStringLiteral("&&")); };
    for (int i = 0; i < m_SessionSlots.size(); ++i) {
        auto slot = m_SessionSlots.at(i);
        if (i >= live.size()) { slot->setVisible(false); slot->setData(QString()); continue; }
        const auto& row = live.at(i);
        const auto state = row.value("state").toString();
        const bool presenting = row.value("visible").toBool();
        slot->setText(label(row.value("name").toString()) + (state == "starting" ? QStringLiteral(" · ") + tr("Connecting…")
            : state == "stopping" ? QStringLiteral(" · ") + tr("Disconnecting…") : QString()));
        slot->setData(row.value("id").toString());
        slot->setProperty("presenting", presenting);
        slot->setChecked(presenting);
        slot->setEnabled(state != "stopping");
        slot->setVisible(true);
    }
    m_AllSessions->setText(tr("All connections (%1)…").arg(live.size()));
    m_AllSessions->setVisible(live.size() > MaxTraySessions);
    m_SessionSeparator->setVisible(!live.isEmpty());
    m_CurrentSession = current.value("id").toString();
    const auto name = label(current.value("name").toString());
    m_DisconnectSession->setText(tr("Disconnect “%1”").arg(name));
    m_DisconnectSession->setVisible(!m_CurrentSession.isEmpty());
    m_ReconnectSession->setText(tr("Reconnect “%1”").arg(name));
    m_ReconnectSession->setVisible(current.value("state") == "connected");
    int connected = 0;
    QString presented;
    for (const auto& row : live) {
        if (row.value("state") == "connected") ++connected;
        if (row.value("visible").toBool()) presented = row.value("name").toString();
    }
    m_Tray.setToolTip(!connected ? QStringLiteral("DeskPort")
        : presented.isEmpty() ? tr("DeskPort · %1 connected").arg(connected)
        : tr("DeskPort · %1 connected · showing %2").arg(connected).arg(presented));
}

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
    m_ShuttingDown = true;
    m_RecoveryTimer.stop();
    beginStop(tr("Sharing is off"));
    emit exitRequested();
}
void HostManager::requestRestart() {
    if (m_ExitRequested || m_RestartRequested) return;
    m_RestartRequested = true;
    setStatus(tr("Restarting DeskPort"));
    QSettings().setValue("ui/showAfterRestart", true);
    emit operationRequested(m_Status);
    QTimer::singleShot(100, this, &HostManager::requestExit);
}
void HostManager::completeExit() {
    if (!m_ExitRequested || running()) return;
#ifdef Q_OS_MACOS
    // Explicit Quit/Restart has already authorized shutdown and all owners have
    // drained. QGuiApplication's Quit event closes windows first; our resident
    // QML window vetoes close-to-hide, canceling Cocoa termination. Exit the Qt
    // loop directly, preserving aboutToQuit, main()'s relaunch and destructors.
    QCoreApplication::exit(0);
#else
    qApp->quit();
#endif
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
    m_ShuttingDown = true; m_RecoveryTimer.stop(); updateUnattendedMonitor();
    // The event loop may already be gone during application shutdown. Normal UI
    // stops are asynchronous; only destruction waits for our own child processes.
    beginStop(tr("Sharing is off"));
    for (auto process : {&m_Credentials, &m_Server, &m_Display}) {
        process->disconnect(this);
        if (process == &m_Display) process->closeWriteChannel();
        const int grace = process == &m_Display ? DisplayShutdownGraceMs : 2500;
        if (process->state() != QProcess::NotRunning && !process->waitForFinished(grace)) {
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
    if (running() || m_ShuttingDown) return;
    if (m_TrustBusy) {
        QTimer::singleShot(50, this, [this, width, height] { start(width, height); });
        return;
    }
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
    m_LinuxOutputName.clear(); m_LinuxX11Lease.clear(); m_LinuxPipewireNode = 0; m_LinuxGnome = false; m_LinuxGnomePolicies = false; m_LinuxHyprland = false;
    m_LinuxX11 = !qEnvironmentVariableIsEmpty("DESKPORT_X11_OUTPUT") ||
        (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qgetenv("XDG_SESSION_TYPE").toLower() != "wayland" &&
         !qEnvironmentVariableIsEmpty("DISPLAY"));
    QFile::remove(m_Directory + "/virtual-display.json");
    const auto desktops = qgetenv("XDG_CURRENT_DESKTOP").split(':');
    const bool hyprland = qgetenv("XDG_CURRENT_DESKTOP").toLower().split(':').contains("hyprland") ||
        !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE");
    const bool x11Virtual = !qEnvironmentVariableIsEmpty("DESKPORT_X11_OUTPUT");
    if (!x11Virtual && (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") || (!desktops.contains("KDE") && !desktops.contains("GNOME") && !hyprland))) {
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
                displayEnvironment.remove("DESKPORT_DISPLAY_REAL_TOPOLOGY"); // isolated acceptance only
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
    // Leave time for the installed Windows broker and owned device preparation.
#ifdef Q_OS_WIN
    constexpr int displayStartupTimeout = 60000;
#else
    constexpr int displayStartupTimeout = 15000;
#endif
    QTimer::singleShot(displayStartupTimeout, this, [this, generation] {
        if (m_Starting && m_Generation == generation) {
#ifdef Q_OS_WIN
            // A broker/device timeout is not a transient media error. Stop
            // here instead of repeatedly requesting privileged display work.
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
    QString deviceName = this->deviceName().left(64);
    deviceName.replace('\n', ' '); deviceName.replace('\r', ' ');
    if (deviceName.trimmed().isEmpty()) deviceName = "DeskPort";
    // Debug detail only when the operator explicitly asked for a raw host log.
    const int logLevel = qEnvironmentVariableIsEmpty("DESKPORT_HOST_LOG") ? 2 : 1;
    config.write(QString("sunshine_name = %2\nport = %1\naddress_family = ipv4\nupnp = disabled\nsystem_tray = disabled\nmin_log_level = %3\norigin_web_ui_allowed = pc\n").arg(m_BasePort).arg(deviceName).arg(logLevel).toUtf8());
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
    Diagnostics::instance().record("host", "DeskPort runtime kind=capture backend=" +
        QString(m_LinuxX11 ? "x11" : m_LinuxHyprland ? "wlr" : !m_LinuxOutputName.isEmpty() && !m_LinuxGnome ? "kwin" : "portal"));
    config.write(m_LinuxX11 ? "capture = x11\n" : m_LinuxHyprland ? "capture = wlr\n" : !m_LinuxOutputName.isEmpty() && !m_LinuxGnome ? "capture = kwin\n" : "capture = portal\n");
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
    unparkDisplay(false);
    if(m_CaretMonitor) m_CaretMonitor->stop();
    m_DisplaySequence = 0; m_QueuedDisplayRequest = {}; ++m_DisplayGeneration;
    if (m_Stopping) return;
    m_Stopping = true;
    m_DisplayClosing = false;
    const auto generation = ++m_Generation;
    m_Starting = false;
    m_StopStatus = status;
    emit sessionHostStopping(m_DesiredSharing && !m_ShuttingDown);
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
}
void HostManager::finishStop() {
    if (!m_Stopping) return;
    if (m_Credentials.state() != QProcess::NotRunning || m_Server.state() != QProcess::NotRunning) return;
    // Release the display only after the host has stopped using it.
    if (m_Display.state() != QProcess::NotRunning) {
        if (!m_DisplayClosing) {
            m_DisplayClosing = true;
            m_Display.closeWriteChannel();
            // Give layout recovery its full grace period after Sunshine exits,
            // including both pre-removal preparation and final verification.
            const auto generation = m_Generation;
            QTimer::singleShot(DisplayShutdownGraceMs, this, [this, generation] {
                if (!m_Stopping || generation != m_Generation) return;
                if (m_Display.state() != QProcess::NotRunning) m_Display.kill();
                finishStop();
            });
        }
        return;
    }
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

void HostManager::browserControl(const QJsonObject& body, QObject* context,
                                std::function<void(QJsonObject)> completion) {
    managementRequest(QStringLiteral("browser"), body, context, std::move(completion));
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
QString HostManager::deviceName() const { return QSettings().value("host/name", QHostInfo::localHostName()).toString(); }
bool HostManager::setDeviceName(const QString& name) {
    if (running()) { setStatus(tr("Stop sharing before changing the device name.")); return false; }
    if (name.trimmed().isEmpty() || name.size() > 64) { setStatus(tr("Device name must contain 1 to 64 characters.")); return false; }
    for (const QChar character : name) if (character.category() == QChar::Other_Control) {
        setStatus(tr("Device name must not contain control characters.")); return false;
    }
    QSettings settings;
    settings.setValue("host/name", name.trimmed());
    settings.sync();
    if (settings.status() != QSettings::NoError) { setStatus(tr("Cannot save the device name.")); return false; }
    emit changed();
    return true;
}
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

