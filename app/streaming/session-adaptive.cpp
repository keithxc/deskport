// Session members for DeskPort's adaptive display: workspace sizing, window
// memory, transition windows and fullscreen exit. Moved unchanged from session.cpp.
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
#ifdef Q_OS_LINUX
#include "backend/sleepmonitor.h"
#endif
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

namespace {
QSize windowPixels(SDL_Window* window) {
    if (CanvasBridge::owns(window)) return CanvasBridge::pixels();
    int width, height;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(window, &width, &height);
#else
    SDL_GL_GetDrawableSize(window, &width, &height);
#endif
    return QSize(width, height);
}
}

qreal Session::systemScaleForWindow(SDL_Window* window) const {
    if (CanvasBridge::owns(window)) return CanvasBridge::scale();
    // XWayland commonly exposes physical pixels for both SDL sizes, hiding the
    // compositor's fractional scaling. Match Qt's logical output geometry to the
    // SDL pixel mode; QScreen DPR can itself be rounded to 2 on a 1.5x compositor.
    const int index = SDL_GetWindowDisplayIndex(window);
    const char* name = SDL_GetDisplayName(index);
    SDL_Rect bounds {}; SDL_GetDisplayBounds(index, &bounds);
    qreal systemScale = m_ClientDefaultScale;
    for (const auto& screen : m_ClientScreens) {
        if ((name && screen.name == QString::fromUtf8(name)) || screen.origin == QPoint(bounds.x, bounds.y)) {
            systemScale = screen.scale;
            if (m_ClientWayland) {
                SDL_DisplayMode mode {}; SDL_Rect safeArea {};
                if (StreamUtils::getNativeDesktopMode(index, &mode, &safeArea))
                    systemScale = DeskPortDisplay::scaleForOutput(QSize(mode.w, mode.h), screen.logicalSize, systemScale);
            }
            if (name && screen.name == QString::fromUtf8(name)) break;
        }
    }
    return systemScale;
}

DeskPortDisplay::Workspace Session::workspaceForWindow(SDL_Window* window, bool initialFullscreen) const {
    QSize pixels = windowPixels(window);
    const int index = SDL_GetWindowDisplayIndex(window);
    int logicalWidth, logicalHeight; SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
    qreal scale = logicalWidth > 0 ? qreal(pixels.width()) / logicalWidth : 1.0;
    const qreal systemScale = systemScaleForWindow(window);
    scale = qMax(scale, systemScale);
    if (initialFullscreen) {
        SDL_DisplayMode mode; SDL_Rect safeArea;
        if (StreamUtils::getNativeDesktopMode(index, &mode, &safeArea)) {
            pixels = QSize(mode.w, mode.h);
            scale = systemScale; // Native output pixels, not an oversampled window buffer.
        }
    }
    auto client = DeskPortDisplay::forClient(pixels, scale);
    // Data saver keeps the logical desktop at 1x; toggling it restarts the
    // stream through the normal adaptive size check.
    if (m_Preferences->dataSaver) client = DeskPortDisplay::forDataSaver(client);
    const auto workspace = DeskPortDisplay::adjusted(client, m_Preferences->desktopAdjustment);
    if (m_Preferences->adaptiveResolution) m_DesktopTuningLimited = workspace.limited;
    if (initialFullscreen || window != m_Window)
        qInfo() << "Client display pixels:" << pixels << "system scale:" << scale << "workspace backing:" << workspace.pixels << "host scale:" << workspace.scale;
    return workspace;
}

void Session::setDesktopAdjustment(double value) {
    if (qFuzzyCompare(value, m_Preferences->desktopAdjustment)) return;
    // Save only this key; do not overwrite unrelated edits with a session snapshot.
    if (StreamingPreferences::saveDesktopAdjustment(m_Computer->uuid, value)) applyDesktopAdjustment(value);
}

bool Session::applyPendingDesktopAdjustment() {
    const double value = m_PendingDesktopAdjustment.exchange(0);
    if (value <= 0 || !StreamingPreferences::validDesktopAdjustment(value)) return false;
    m_Preferences->desktopAdjustment = value;
    if (m_Preferences->adaptiveResolution) {
        // The adaptive check compares the new desktop with the current stream
        // and keeps the window for a change, exactly like a window resize.
        m_ResizeSettler.reset();
        return false;
    }
    const auto fixed = DeskPortDisplay::adjusted({AdaptiveDisplay::boundedSize(QSize(m_Preferences->width, m_Preferences->height)), 1}, value);
    m_DesktopTuningLimited = fixed.limited;
    if (fixed.pixels == QSize(m_StreamConfig.width, m_StreamConfig.height)) return false;
    rememberAdaptiveWindow();
    m_ManualReconnect = true;
    return true;
}

void Session::pumpTransitionWindowBetweenSessions() {
    if (!m_TransitionTimer) {
        m_TransitionTimer = new QTimer(this);
        connect(m_TransitionTimer, &QTimer::timeout, this, [this] { if (m_TransitionWindow) pumpTransitionWindow(); });
    }
    m_TransitionTimer->start(20);
}

void Session::reportViewerFocus(bool focused) {
    if (m_ReportedViewerFocus == int(focused)) return;
    m_ReportedViewerFocus = focused;
    emit viewerFocusChanged(focused);
}

void Session::pumpTransitionWindow() {
    // On macOS, SDL_PumpEvents() runs the Cocoa run loop, which also dispatches
    // Qt timers and queued calls. A continuation can start its exec() there and
    // take or release this window, so keep it and this session alive meanwhile.
    const auto window = m_TransitionWindow;
    if (!window) return;
    const QPointer<Session> self(this);
    window->pump();
    if (!self) return;
    if (window->takePresentationShown() && m_ViewerRequested) emit presentationShown();
    // The connecting window is the viewer too: the tray hides it when in front.
    if (window->window()) reportViewerFocus(CanvasBridge::flags(window->window()) & SDL_WINDOW_INPUT_FOCUS);
    if (window->takeShortcutRequested() && m_ViewerRequested) requestShortcutToggle();
}

void Session::initializeAdaptiveDisplay(SDL_Window* window) {
    if (!m_AdaptiveDisplay) {
        const auto peers = PeerStore::read(InputTestProfile::data() + "/binding/peers.json")["peers"].toObject();
        for (const auto& value : peers) {
            const auto peer = value.toObject();
            if (peer["hostId"].toString().compare(m_Computer->uuid, Qt::CaseInsensitive) ||
                !peer["ready"].toBool() || !peer["granted"].toBool()) continue;
            const QSslCertificate certificate(peer["clientCert"].toString().toUtf8());
            const int port = peer["bindingPort"].toInt();
            if (certificate.isNull() || port < 1 || port > 65535) break;
            auto identity = IdentityManager::get();
            m_AdaptiveDisplay = std::make_shared<AdaptiveDisplay>(m_Computer->activeAddress.address(), quint16(port),
                certificate, identity->getCertificate(), identity->getPrivateKey(), m_Preferences->displayPolicy, m_ResumeToken);
            break;
        }
    }
    if (!m_AdaptiveDisplay) {
        m_SessionAdmissionFailed = true; m_SessionTopologyError = "topology-unsupported"; return;
    }
    // A controller retained across a network retry may still hold the host's
    // video paused for a hidden viewer. A new stream must receive its first
    // frame, or it ends with no video traffic and retries forever; the
    // viewer's paused state is applied again after the first key frame.
    m_AdaptiveDisplay->setVideoPaused(false);
    // The retained native window has received any intervening drag events.
    // Read its newest size once before committing the next host request.
    if (m_AdaptiveResume && m_TransitionWindow) {
        // The committed size already waited out the quiet interval before the
        // old stream stopped; only a change or a pressed button since restarts it.
        ResizeSettler settling;
        if (m_AdaptiveNextSize.isValid()) settling.settled(m_AdaptiveNextSize, m_AdaptiveScale, SDL_GetTicks());
        auto retained = m_TransitionWindow->window();
        while (!m_TransitionWindow->cancelled() && desktopWindowVisible(retained)) {
            pumpTransitionWindow();
            const auto latest = workspaceForWindow(retained);
            if (settling.update(latest.pixels, latest.scale, SDL_GetTicks(),
                                (SDL_GetGlobalMouseState(nullptr, nullptr) | SDL_GetMouseState(nullptr, nullptr)) != 0)) break;
            if (!m_ThreadedExec) QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            SDL_Delay(20);
        }
        if (m_TransitionWindow->cancelled()) return;
        if (desktopWindowVisible(retained)) {
            const auto latest = workspaceForWindow(retained);
            if (latest.pixels.isValid()) {
                m_AdaptiveNextSize = latest.pixels; m_AdaptiveScale = latest.scale;
                int x, y, w, h; SDL_GetWindowPosition(retained, &x, &y); SDL_GetWindowSize(retained, &w, &h);
                m_IsFullScreen = (CanvasBridge::flags(retained) & SDL_WINDOW_FULLSCREEN) != 0;
                m_AdaptiveMaximized = (CanvasBridge::flags(retained) & SDL_WINDOW_MAXIMIZED) != 0;
                if (!CanvasBridge::enabled() && !m_IsFullScreen && !m_AdaptiveMaximized) m_AdaptiveGeometry = QRect(x, y, w, h);
            }
        }
    }
    const auto workspace = workspaceForWindow(window, m_IsFullScreen && !m_AdaptiveResume);
    if (!m_AdaptiveResume) m_AdaptiveScale = m_Preferences->adaptiveResolution ? workspace.scale : 1;
    // Restore window geometry, not a stream size negotiated by an older policy.
    const auto fixed = DeskPortDisplay::adjusted({AdaptiveDisplay::boundedSize(QSize(m_Preferences->width,m_Preferences->height)), 1}, m_Preferences->desktopAdjustment);
    if (!m_Preferences->adaptiveResolution) m_DesktopTuningLimited = fixed.limited;
    const QSize target = !m_Preferences->adaptiveResolution ? fixed.pixels : m_AdaptiveResume ? m_AdaptiveNextSize : workspace.pixels;
    m_AdaptiveNextSize = {};
    deskportResizeStage("mode-request", target.width(), target.height());
    m_AdaptiveDisplay->setFullScreen(m_IsFullScreen);
    if (m_AdaptiveDisplay->resize(target, m_AdaptiveScale, [this] {
            if (m_RecoveryCancelled || (m_RecoveryDeadline && QDateTime::currentMSecsSinceEpoch() >= m_RecoveryDeadline))
                m_AdaptiveDisplay->cancel();
            if (m_TransitionWindow) pumpTransitionWindow();
            if (!m_ThreadedExec) QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }, [this, window] {
            if (m_RecoveryDeadline || m_RecoveryCancelled) return false;
            if (m_TransitionWindow && m_TransitionWindow->rendering()) {
                // Ask inside the connecting window, in the app's own style. The
                // host keeps the takeover offer for 30 seconds.
                const auto viewer = m_TransitionWindow->window();
                int points, pointsHeight; SDL_GetWindowSize(viewer, &points, &pointsHeight);
                const qreal pixelRatio = points > 0 ? qreal(windowPixels(viewer).width()) / points : 1.0;
                m_TransitionWindow->ask(tr("Host already in use"),
                    tr("Another device is connected to this host. Disconnect it and connect here?"),
                    tr("Cancel"), tr("Disconnect and connect"), qMax(pixelRatio, systemScaleForWindow(viewer)));
                recallDesktopWindow(m_TransitionWindow->window());
                const Uint32 deadline = SDL_GetTicks() + 30000;
                int answer = -1;
                while ((answer = m_TransitionWindow->takeAnswer()) < 0 && !m_TransitionWindow->cancelled() &&
                       !m_RecoveryCancelled && !SDL_TICKS_PASSED(SDL_GetTicks(), deadline)) {
                    pumpTransitionWindow();
                    if (!m_ThreadedExec) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    SDL_Delay(10);
                }
                if (answer < 0) m_TransitionWindow->dismiss();
                return answer == 1;
            }
            const auto title = tr("Host already in use").toUtf8();
            const auto text = tr("Another device is connected to this host. Disconnect it and connect here?").toUtf8();
            const auto cancel = tr("Cancel").toUtf8();
            const auto takeover = tr("Disconnect and connect").toUtf8();
            const SDL_MessageBoxButtonData buttons[] = {
                {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, cancel.constData()},
                {0, 1, takeover.constData()}
            };
            SDL_MessageBoxData dialog{SDL_MESSAGEBOX_WARNING, window, title.constData(), text.constData(), 2, buttons, nullptr};
            int selected = 0;
            return SDL_ShowMessageBox(&dialog, &selected) == 0 && selected == 1;
        })) {
        const auto selected=m_AdaptiveDisplay->negotiatedSize();
        m_AdaptiveScale=m_AdaptiveDisplay->selectedScale(m_AdaptiveScale);
        m_StreamConfig.width = selected.width(); m_StreamConfig.height = selected.height();
        deskportResizeStage("mode-ready", target.width(), target.height());
        if (!m_AdaptiveDisplay->warning().isEmpty()) emit displayLaunchWarning(m_AdaptiveDisplay->warning());
        qInfo() << "Adaptive display negotiated:" << selected << "requested" << target << "scale" << m_AdaptiveScale;
    } else {
        // Older/unavailable hosts retain normal fixed-resolution streaming.
        // Disable adaptation for this session to avoid reconnect loops.
        deskportResizeStage("mode-failed");
        m_SessionAdmissionFailed = m_RecoveryDeadline || m_AdaptiveDisplay->admissionRequired();
        m_SessionTopologyError = m_AdaptiveDisplay->topologyError();
        if (m_RecoveryDeadline) scheduleNetworkRecovery();
        m_AdaptiveDisplay.reset();
        qWarning() << "Using fixed-resolution streaming; adaptive display negotiation was unavailable";
    }
}

void Session::hideViewerWindow() {
    if (CanvasBridge::owns(m_Window)) CanvasBridge::requested(false);
    const auto flags = CanvasBridge::flags(m_Window);
    if (!(flags & SDL_WINDOW_HIDDEN)) m_HiddenWindowFlags = flags;
    SDL_HideWindow(m_Window);
}

bool Session::checkAdaptiveResize() {
    if (!m_ViewerRequested || (CanvasBridge::flags(m_Window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) ||
        (m_RecallGraceUntil && !SDL_TICKS_PASSED(SDL_GetTicks(), m_RecallGraceUntil))) {
        // Hidden time does not establish a stable visible size. Compositors can
        // report transient frame geometry while a retained window is remapped,
        // so a recalled window gets time to regain its previous state.
        m_ResizeSettler.reset();
        return false;
    }
    m_RecallGraceUntil = 0;
    if (!m_Preferences->adaptiveResolution || !m_AdaptiveDisplay || m_UnexpectedTermination) return false;
    const auto workspace = workspaceForWindow(m_Window);
    const auto size = workspace.pixels;
    const int scale = workspace.scale;
    if (!size.isValid()) return false;
    const bool settled = m_ResizeSettler.update(size, scale, SDL_GetTicks(),
        (SDL_GetGlobalMouseState(nullptr, nullptr) | SDL_GetMouseState(nullptr, nullptr)) != 0);
    if (size != m_AdaptiveObservedSize || scale != m_AdaptiveObservedScale) {
        m_AdaptiveObservedSize = size; m_AdaptiveObservedScale = scale;
        // Capture a current picture for the transition while the stream still runs.
        StreamSnapshot::request();
        deskportResizeStage("observed", size.width(), size.height()); return false;
    }
    if (m_AdaptiveDisplay->selectedSize(size) == QSize(m_StreamConfig.width, m_StreamConfig.height) && m_AdaptiveDisplay->selectedScale(scale) == m_AdaptiveScale) {
        if (settled) rememberAdaptiveWindow();
        return false;
    }
    if (!settled ||
        SDL_HasEvent(SDL_QUIT) || SDL_HasEvents(SDL_KEYDOWN, SDL_KEYUP)) return false;
    // Finish the old stream before changing the capture mode. Preserve desktop
    // apps and the authenticated display lease across the new resume request.
    int x, y, width, height;
    SDL_GetWindowPosition(m_Window, &x, &y); SDL_GetWindowSize(m_Window, &width, &height);
    const auto flags = CanvasBridge::flags(m_Window);
    m_IsFullScreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    m_AdaptiveMaximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!CanvasBridge::enabled() && !m_IsFullScreen && !m_AdaptiveMaximized) m_AdaptiveGeometry = QRect(x, y, width, height);
    m_AdaptiveNextSize = size; m_AdaptiveScale = scale;
    deskportResizeStage("stop-begin", size.width(), size.height());
    qInfo() << "Adaptive display restarting stream for" << size;
    return true;
}

void Session::restoreAdaptiveWindow()
{
    if (CanvasBridge::enabled() || !m_Preferences->adaptiveResolution || m_AdaptiveResume) return;
    QByteArray outputs;
    QDataStream stream(&outputs, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_9);
    stream << QString::fromUtf8(SDL_GetCurrentVideoDriver());
    for (const auto& screen : m_ClientScreens)
        stream << screen.name << screen.origin << screen.logicalSize << double(screen.scale);
    for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i) {
        SDL_Rect bounds {}; SDL_DisplayMode mode {}; SDL_Rect safeArea {};
        SDL_GetDisplayUsableBounds(i, &bounds);
        StreamUtils::getNativeDesktopMode(i, &mode, &safeArea);
        stream << QString::fromUtf8(SDL_GetDisplayName(i)) << QRect(bounds.x, bounds.y, bounds.w, bounds.h)
               << QSize(mode.w, mode.h);
    }
    m_WindowOutputs = QCryptographicHash::hash(outputs, QCryptographicHash::Sha256);
    QSettings settings;
    const auto state = DeskPortDisplay::readSessionWindow(settings, m_Computer->uuid, m_WindowOutputs,
                                                        int(m_Preferences->windowMode));
    if (!state.valid()) return;
    m_AdaptiveGeometry = state.geometry;
    m_AdaptiveMaximized = state.maximized;
    m_IsFullScreen = state.fullscreen;
    m_RestoredWindow = true;
    qInfo() << "Restoring last client workspace:" << state.streamSize << "window:" << state.geometry.size();
}

void Session::rememberAdaptiveWindow()
{
    if (CanvasBridge::enabled() || !m_AdaptiveDisplay || m_UnexpectedTermination || !m_Window || m_WindowOutputs.isEmpty()) return;
    const auto flags = CanvasBridge::flags(m_Window);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return;
    int x, y, w, h;
    SDL_GetWindowPosition(m_Window, &x, &y);
    SDL_GetWindowSize(m_Window, &w, &h);
    const DeskPortDisplay::SessionWindowState state {QRect(x, y, w, h),
        QSize(m_StreamConfig.width, m_StreamConfig.height), m_AdaptiveScale,
        bool(flags & SDL_WINDOW_MAXIMIZED), bool(flags & SDL_WINDOW_FULLSCREEN)};
    const auto record = QString("%1,%2,%3,%4,%5,%6,%7,%8")
        .arg(x).arg(y).arg(w).arg(h).arg(state.streamSize.width()).arg(state.streamSize.height())
        .arg(state.maximized).arg(state.fullscreen);
    if (record == m_LastWindowRecord || !state.valid()) return;
    QSettings settings;
    DeskPortDisplay::writeSessionWindow(settings, m_Computer->uuid, m_WindowOutputs,
                                       int(m_Preferences->windowMode), state);
    m_LastWindowRecord = record;
}

bool Session::leaveFullscreen() {
    if (!m_Window || !(CanvasBridge::flags(m_Window) & SDL_WINDOW_FULLSCREEN)) return false;
    toggleFullscreen();
    if (m_InputHandler) { m_InputHandler->raiseAllKeys(); m_InputHandler->setCaptureActive(false); }
    return true;
}
