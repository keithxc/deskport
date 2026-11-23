// HostManager members for the shared display: policies, resize and restore.
// Moved unchanged from hostmanager.cpp.
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

bool HostManager::saveLinuxDisplayState() {
#ifdef Q_OS_LINUX
    if (!m_LinuxOutputName.isEmpty()) {
        return PeerStore::write(m_Directory + "/virtual-display.json", {
            {"node", double(m_LinuxPipewireNode)}, {"serial", m_LinuxPipewireSerial}, {"output", m_LinuxOutputName},
            {"width", m_DisplayWidth}, {"height", m_DisplayHeight}, {"scale", m_DisplayScale}, {"leaseToken", m_LinuxX11Lease}});
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
    return adaptiveDisplayAvailable() && (!m_LinuxGnome || m_LinuxGnomePolicies) && !m_LinuxX11 && (!m_LinuxHyprland || m_LinuxHyprlandPolicies);
#else
    return adaptiveDisplayAvailable();
#endif
}

// Windows rebuilds the whole display topology through an elevated guardian
// that allows itself 20 seconds; three outputs can take longer than five.
#ifdef Q_OS_WIN
static constexpr int DisplayChangeTimeoutMs = 20000;
#else
static constexpr int DisplayChangeTimeoutMs = 5000;
#endif

QString HostManager::displayResizeRejection(int width, int height, int scale, int sequence, int policy) const {
#ifdef Q_OS_LINUX
    if (m_LinuxX11 && policy != 0) return QStringLiteral("policy-unsupported");
#endif
    if (policy < 0 || policy > 2) return QStringLiteral("policy-unsupported");
    if (!adaptiveDisplayAvailable()) return QStringLiteral("unavailable");
    if (sequence == 0 || width < 640 || width > DeskPortDisplay::MaxWidth ||
        height < 360 || height > DeskPortDisplay::MaxHeight || width % 4 || height % 4 ||
        (scale != 1 && scale != 2)) return QStringLiteral("topology-invalid");
    if (!m_QueuedDisplayRequest.isEmpty() || (m_DisplaySequence && (m_DisplaySequence > 0 || sequence < 0)))
        return QStringLiteral("busy");
    return {};
}

bool HostManager::resizeDisplay(int width, int height, int scale, int sequence, int policy) {
    if (!displayResizeRejection(width, height, scale, sequence, policy).isEmpty()) return false;
    const bool wasParked = m_DisplayParked && sequence > 0;
    const int parkedPolicy = m_ParkedDisplayPolicy;
    unparkDisplay(false);
    if (m_DisplaySequence) {
        // Finish the previous session's restoration before a new client takes over.
        m_QueuedDisplayRequest = {{"width", width}, {"height", height}, {"scale", scale},
                                  {"seq", sequence}, {"policy", policy}};
        return true;
    }
    if (wasParked && policy != parkedPolicy) {
        // Native display helpers forbid changing topology policy mid-session.
        // Settle the parked session first, then apply the new policy via the queue.
        if (!resizeDisplay(sharingWidth(), sharingHeight(), 1, -1)) return false;
        m_QueuedDisplayRequest = {{"width", width}, {"height", height}, {"scale", scale},
                                  {"seq", sequence}, {"policy", policy}};
        return true;
    }
    if(m_CaretMonitor) m_CaretMonitor->stop();
    m_DisplaySequence = sequence;
    m_DisplayWireSequence = m_DisplayWireSequence == std::numeric_limits<int>::max() ? 1 : m_DisplayWireSequence + 1;
    const auto generation = ++m_DisplayGeneration;
    // An unadvertised Hyprland policy contract retains its legacy extension
    // behavior. Older helpers still receive their legacy ID 0. A negotiated
    // request is passed through without substitution.
#ifdef Q_OS_LINUX
    if (m_LinuxHyprlandPolicyRequests && !m_LinuxHyprlandPolicies && policy == 0) policy = 2;
#endif
    QJsonObject request{{"seq", m_DisplayWireSequence}, {"width", width}, {"height", height}, {"scale", scale}, {"session", sequence > 0}};
#ifdef Q_OS_LINUX
    if (!m_LinuxX11)
#endif
        request["displayPolicy"] = policy;
    m_Display.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    // Mutter's independent guardian verifies output removal and may replay a
    // saved layout before admitting a new stream. Do not time out a valid
    // recovery at the ordinary five-second resize deadline.
    const int timeoutMs = m_LinuxGnome ? 20000 : DisplayChangeTimeoutMs;
    QTimer::singleShot(timeoutMs, this, [this, generation] {
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
    const int timeoutMs = m_LinuxGnome ? 20000 : DisplayChangeTimeoutMs;
    QTimer::singleShot(timeoutMs + 5000, pending, [finish] { finish(false); });
    restoreDisplay();
}

quint64 HostManager::localHidEventCount() {
#ifdef Q_OS_MACOS
    static constexpr CGEventType types[] = {
        kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventRightMouseDown,
        kCGEventOtherMouseDown, kCGEventScrollWheel, kCGEventKeyDown, kCGEventFlagsChanged
    };
    quint64 total = 0;
    for (const auto type : types)
        total += CGEventSourceCounterForEventType(kCGEventSourceStateHIDSystemState, type);
    return total;
#else
    return 0;
#endif
}

void HostManager::parkDisplay(const QString& owner, int policy) {
    if (owner.isEmpty() || !virtualDisplayActive() || m_Stopping || m_DisplaySequence != 0) {
        restoreDisplay();
        return;
    }
    if (m_CaretMonitor) m_CaretMonitor->stop();
#ifdef Q_OS_MACOS
    if (m_Display.state() == QProcess::Running)
        m_Display.write("{\"parked\":true}\n");
#endif
    m_DisplayParked = true;
    m_ParkedDisplayOwner = owner;
    m_ParkedDisplayPolicy = policy;
    m_ParkedDisplayTimer.start(m_ParkedDisplayTimeoutMs);
    m_ParkedInputArmed = false;
#ifdef Q_OS_MACOS
    if (!m_Isolated && QGuiApplication::platformName() == QStringLiteral("cocoa"))
        m_ParkedInputMonitor.start(500);
    else m_ParkedInputMonitor.stop();
#else
    m_ParkedInputMonitor.stop();
#endif
    emit changed();
}

void HostManager::claimParkedDisplay(const QString& owner) {
    if (!m_DisplayParked || m_ParkedDisplayOwner != owner) return;
    // Stop local HID polling before the resumed client's input channel starts.
    m_ParkedInputArmed = false;
    m_ParkedInputMonitor.stop();
    m_ParkedDisplayTimer.start(m_ParkedDisplayTimeoutMs);
}

void HostManager::unparkDisplay(bool restore) {
    const bool wasParked = m_DisplayParked;
    m_DisplayParked = false;
    m_ParkedInputArmed = false;
    m_ParkedDisplayOwner.clear();
    m_ParkedDisplayPolicy = -1;
    m_ParkedDisplayTimer.stop();
    m_ParkedInputMonitor.stop();
    if (wasParked && restore) restoreDisplay();
    else if (wasParked) emit changed();
}

void HostManager::restoreDisplay() {
    unparkDisplay(false);
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
