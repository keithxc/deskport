// Session members for DeskPort's session continuity: network recovery,
// continuation sessions, sleep, clipboard, viewer switching and traffic.
// Moved unchanged from session.cpp.
#include "../backend/inputtestprofile.h"
#include "video/remotedecoder.h"
#include "../backend/sessionfailure.h"
#include "backend/diagnostics.h"
#include "connectionwait.h"
#include "resumewatchdog.h"
#include "resizetrace.h"
#include <QElapsedTimer>
#include <QHostInfo>
#include "session.h"
#include <QPointer>
#include "backend/hostalias.h"
#include "../../moonlight-common-c/traffic.h"
#include "backend/clipboardtraffic.h"
#ifdef Q_OS_MACOS
#include "backend/macdock.h"
#endif
#include "backend/sleepmonitor.h"
#include "backend/peerstore.h"
#include "backend/streambudget.h"
#include "backend/sessionwindowstate.h"
#include <QDataStream>
#include "backend/identitymanager.h"
#include <QStandardPaths>
#include "settings/streamingpreferences.h"
#include "streaming/streamutils.h"
#include "backend/richpresencemanager.h"

#include <Limelight.h>
#include <SDL.h>
#include "utils.h"

#ifdef HAVE_FFMPEG
#include "video/ffmpeg.h"
#endif

#ifdef HAVE_SLVIDEO
#include "video/slvid.h"
#endif

#include <openssl/rand.h>

#include <QtEndian>
#include <QCoreApplication>
#include <QThreadPool>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QGuiApplication>
#include <QCursor>
#include <QWindow>
#include <QScreen>

bool Session::adaptiveRestartPending() const {
    // Sleep is only resumable for an already authenticated lifecycle session.
    // Never launch a legacy host, request sharing, or offer takeover on wake.
    if (m_SystemSleep) return (!m_TransitionWindow || !m_TransitionWindow->cancelled()) &&
        m_AdaptiveDisplay && m_AdaptiveDisplay->retryable() &&
        m_AdaptiveDisplay->admissionRequired() && !m_AdaptiveDisplay->resumeToken().isEmpty();
    return !m_RecoveryCancelled && (m_NetworkRetry || m_ManualReconnect || m_AdaptiveNextSize.isValid());
}
bool Session::scheduleNetworkRecovery() {
    if (m_RecoveryCancelled || !m_AdaptiveDisplay || !m_AdaptiveDisplay->retryable() ||
        (!m_RecoveryDeadline && !m_AdaptiveDisplay->admissionRequired())) return false;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_StreamStartedAt && now - m_StreamStartedAt > 10000) {
        m_RecoveryDeadline = 0; m_RecoveryAttempt = 0;
    }
    if (!m_RecoveryDeadline) m_RecoveryDeadline = now + 30000;
    if (now >= m_RecoveryDeadline || m_RecoveryAttempt >= 5) return false;
    if (!m_AdaptiveDisplay->resumeToken().isEmpty()) m_ResumeToken = m_AdaptiveDisplay->resumeToken();
    m_NetworkRetry = true; m_UnexpectedTermination = true;
    return true;
}

Session* Session::adaptiveContinuation() {
    if (!adaptiveRestartPending()) return nullptr;
    auto next = continuationSession();
    // A tuning change that arrived while the stream was stopping still applies.
    next->m_PendingDesktopAdjustment = m_PendingDesktopAdjustment.load();
    return next;
}

Session* Session::continuationSession() {
    if (m_NetworkRetry || m_SystemSleep) {
        auto next = new Session(m_Computer, m_App, m_Preferences);
        next->m_SystemResume = m_SystemSleep.load();
        next->m_RecoveryDeadline = next->m_SystemResume ? 0 : m_RecoveryDeadline.load();
        next->m_RecoveryAttempt = next->m_SystemResume ? 0 : m_RecoveryAttempt + 1;
        next->m_ResumeToken = next->m_SystemResume ? m_AdaptiveDisplay->resumeToken() : m_ResumeToken;
        next->m_ManualResume = true;
        next->m_AdaptiveGeometry = m_AdaptiveGeometry;
        next->m_IsFullScreen = m_IsFullScreen;
        next->m_ViewerRequested = m_ViewerRequested.load();
        next->m_LifecycleQuiet = m_LifecycleQuiet.load();
        next->m_TrafficReceivedBase = m_TrafficReceivedBase;
        next->m_TrafficSentBase = m_TrafficSentBase;
        // Keep the retained window responsive through the retry delay.
        if (m_TransitionTimer) m_TransitionTimer->stop();
        next->m_TransitionWindow = std::move(m_TransitionWindow);
        if (next->m_TransitionWindow) next->pumpTransitionWindowBetweenSessions();
        // A media-only interruption may retain its authenticated controller.
        if (!next->m_SystemResume && m_AdaptiveDisplay && !m_AdaptiveDisplay->failed()) next->m_AdaptiveDisplay = std::move(m_AdaptiveDisplay);
        else m_AdaptiveDisplay.reset();
        SDL_FlushEvents(SDL_USEREVENT, SDL_LASTEVENT);
        return next;
    }
    if (m_ManualReconnect) {
        // Read the saved device profile only after the old transport is stopped.
        auto next = new Session(m_Computer, m_App);
        next->m_ViewerRequested = m_ViewerRequested.load();
        next->m_LifecycleQuiet = m_LifecycleQuiet.load();
        next->m_TrafficReceivedBase = m_TrafficReceivedBase;
        next->m_TrafficSentBase = m_TrafficSentBase;
        next->m_ManualResume = true;
        if (next->m_Preferences->adaptiveResolution || (m_AdaptiveDisplay && m_AdaptiveDisplay->admissionRequired())) next->m_AdaptiveDisplay = std::move(m_AdaptiveDisplay);
        else m_AdaptiveDisplay.reset();
        SDL_FlushEvents(SDL_USEREVENT, SDL_LASTEVENT);
        return next;
    }
    auto next = new Session(m_Computer, m_App, m_Preferences);
    next->m_ViewerRequested = m_ViewerRequested.load();
    next->m_LifecycleQuiet = m_LifecycleQuiet.load();
    if (m_TransitionTimer) m_TransitionTimer->stop();
    // The old transport has stopped; discard its queued decoder/rumble callbacks.
    SDL_FlushEvents(SDL_USEREVENT, SDL_LASTEVENT);
    next->m_TransitionWindow = std::move(m_TransitionWindow);
    next->m_AdaptiveDisplay = std::move(m_AdaptiveDisplay);
    next->m_AdaptiveNextSize = m_AdaptiveNextSize;
    next->m_AdaptiveGeometry = m_AdaptiveGeometry;
    next->m_WindowOutputs = m_WindowOutputs;
    next->m_AdaptiveScale = m_AdaptiveScale;
    next->m_AdaptiveMaximized = m_AdaptiveMaximized;
    next->m_IsFullScreen = m_IsFullScreen;
    next->m_TrafficReceivedBase = m_TrafficReceivedBase;
    next->m_TrafficSentBase = m_TrafficSentBase;
    next->m_AdaptiveResume = true;
    deskportResizeStage("continuation");
    return next;
}

void Session::setDataSaver(bool on) {
    if (m_Preferences->dataSaver == on) return;
    m_Preferences->dataSaver = on;
    const auto settings = DeskPortStream::sessionMediaSettings(
        on, m_Preferences->remoteAudio, m_Preferences->smartStreaming, m_HostSessionSettings);
    const bool liveRateSupported = m_LiveRateNegotiated && m_AdaptiveDisplay && m_AdaptiveDisplay->liveRateSupported();
    if (DeskPortStream::dataSaverNeedsReconnect(m_NegotiatedMediaSettings, settings, liveRateSupported)) {
        // The current host cannot pause/resume audio or change smart cadence
        // through video-rate. Older hosts also require a new bitrate negotiation.
        requestReconnect();
        return;
    }
    const int cap = std::min(m_StreamConfig.bitrate, DeskPortStream::dataSaverBitrate(m_StreamConfig.width, m_StreamConfig.height));
    m_RateCapKbps = on && cap < m_StreamConfig.bitrate ? cap : 0;
    // Lifting the ceiling sends the negotiated bitrate, which removes it.
    m_AdaptiveDisplay->setRateCeiling(m_RateCapKbps ? m_RateCapKbps : m_StreamConfig.bitrate, on);
}

void Session::requestReconnect() {
    // SDL owns session state on Linux; marshal tray requests to that thread.
    SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortReconnect;
    SDL_PushEvent(&event);
}

void Session::releaseForClientLock() {
    m_LifecycleQuiet = true;
    m_ViewerRequested = false;
    // Windows/Cocoa dispatch native notifications on the SDL/UI thread. Release
    // before returning to the OS, which may freeze us before another event turn.
#if !defined(Q_OS_LINUX)
    if (m_InputHandler) m_InputHandler->suspendInput();
    m_Clipboard.reset();
    if (m_Window) hideViewerWindow();
#endif
    SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortHideWindow;
    SDL_PushEvent(&event);
}
void Session::endForSystemSleep() {
    // A continuation parked before exec already waits on the normalized state.
    if (!m_ExecRequested || m_RecoveryCancelled.exchange(true)) return;
    m_SystemSleep = true;
    releaseForClientLock();
    m_UnexpectedTermination = true;
    SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = DeskPortTransportEnded;
    SDL_PushEvent(&event);
}

void Session::initializeClipboard() {
    if (!m_Preferences->sharedClipboard || !m_Preferences->remoteInput) return;
    const auto peers = PeerStore::read(InputTestProfile::data() + "/binding/peers.json")["peers"].toObject();
    for (const auto& value : peers) {
        const auto peer = value.toObject();
        if (peer["hostId"].toString().compare(m_Computer->uuid, Qt::CaseInsensitive) ||
            !peer["ready"].toBool() || !peer["granted"].toBool()) continue;
        const QSslCertificate cert(peer["clientCert"].toString().toUtf8());
        const int port = peer["bindingPort"].toInt();
        if (cert.isNull() || port < 1 || port > 65535) break;
        auto identity = IdentityManager::get();
        m_Clipboard.reset(new ClipboardSync(std::unique_ptr<ClipboardChannel>(new ClipboardChannel(
            m_Computer->activeAddress.address(), quint16(port), cert, identity->getCertificate(), identity->getPrivateKey(), true))));
        return;
    }
    m_OverlayManager.updateOverlayText(Overlay::OverlayStatusUpdate, "Clipboard sharing requires a DeskPort device binding.");
    m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, true);
}

void Session::requestNextViewer() {
    // Only an isolated session worker has a shell to switch; the legacy
    // in-process viewer has no other desktop and ignores the chord.
    if (m_QtWindow && m_QtWindow->metaObject()->indexOfMethod("showNextSession()") >= 0)
        QMetaObject::invokeMethod(m_QtWindow, "showNextSession", Qt::QueuedConnection);
}

void Session::requestShortcutToggle() {
    if (ShortcutState::enabled() && m_QtWindow)
        QMetaObject::invokeMethod(m_QtWindow, "requestShortcutToggle", Qt::QueuedConnection);
}

QVariantMap Session::traffic() const {
    return {{"received", double(DpTrafficReceived() + DeskPortTraffic::clipboardReceived().load(std::memory_order_relaxed) - m_TrafficReceivedBase)},
            {"sent", double(DpTrafficSent() + DeskPortTraffic::clipboardSent().load(std::memory_order_relaxed) - m_TrafficSentBase)}};
}
