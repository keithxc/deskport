#include <QJsonArray>
#include <cmath>
#include <limits>
#include "smalltcp.h"
#include "../../shared/deskport-core/include/deskport/protocol.h"
#include "adaptivedisplay.h"
#include "sessiongraph.h"
#include <QUuid>
#include "workspaceresolution.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QDebug>

AdaptiveDisplay::AdaptiveDisplay(QString address, quint16 port, QSslCertificate peer,
                                 QByteArray certificate, QByteArray key, int policy, QString resumeToken)
    : m_Address(address), m_Port(port), m_Peer(peer), m_Certificate(certificate), m_Key(key), m_Policy(policy), m_ResumeToken(resumeToken) {
    m_GraphIdentity = SessionGraph::identity(QSslCertificate(certificate));
    m_GraphToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_GraphReserved = SessionGraph::reserve(m_GraphIdentity, {m_GraphToken, address, port, peer, certificate, key});
    if (!m_GraphReserved) { m_TopologyError = "cycle"; m_Failed = true; m_Retryable = false; }
    else start();
}
AdaptiveDisplay::~AdaptiveDisplay() {
    requestInterruption();
    { QMutexLocker lock(&m_Mutex); m_Wake.wakeAll(); }
    wait();
    if (m_GraphReserved) SessionGraph::release(m_GraphIdentity, m_GraphToken);
}
QString AdaptiveDisplay::topologyError() { QMutexLocker lock(&m_Mutex); return m_TopologyError; }
bool AdaptiveDisplay::retryable() { QMutexLocker lock(&m_Mutex); return m_Retryable && !m_TakenOver; }
QString AdaptiveDisplay::warning() { QMutexLocker lock(&m_Mutex); return m_Warning; }
QString AdaptiveDisplay::resumeToken() { QMutexLocker lock(&m_Mutex); return m_ResumeToken; }
void AdaptiveDisplay::cancel(bool keepRecovery) { QMutexLocker lock(&m_Mutex); m_TopologyError = "cancelled"; m_Failed = true; m_Retryable = m_Retryable && keepRecovery && !m_ResumeToken.isEmpty(); requestInterruption(); m_Wake.wakeAll(); }
void AdaptiveDisplay::release() { QMutexLocker lock(&m_Mutex); m_Release = true; requestInterruption(); m_Wake.wakeAll(); }
QSize AdaptiveDisplay::selectedSize(QSize requested) {
    QMutexLocker lock(&m_Mutex);
    if(m_Modes.isEmpty() || m_Modes.contains(requested))return requested;
    QSize best; double score=std::numeric_limits<double>::infinity();
    for(const auto mode:m_Modes) {
        const double x=std::log(double(mode.width())/requested.width());
        const double y=std::log(double(mode.height())/requested.height());
        const double candidate=x*x+y*y+4*(x-y)*(x-y);
        if(candidate<score){score=candidate;best=mode;}
    }
    return best;
}
int AdaptiveDisplay::selectedScale(int requested) { QMutexLocker lock(&m_Mutex); return m_Modes.isEmpty()?requested:1; }
QSize AdaptiveDisplay::negotiatedSize() { QMutexLocker lock(&m_Mutex); return m_NegotiatedSize; }
bool AdaptiveDisplay::admissionRequired() { QMutexLocker lock(&m_Mutex); return m_AdmissionRequired; }
bool AdaptiveDisplay::liveRateSupported() { QMutexLocker lock(&m_Mutex); return m_RateSupported; }
void AdaptiveDisplay::beginRateStream(int initialKbps, bool saver) {
    QMutexLocker lock(&m_Mutex);
    m_RateReady = false;
    ++m_RateGeneration;
    m_RateCeiling = initialKbps > 0 ? qBound(DP_VIDEO_RATE_MIN_KBPS, initialKbps, DP_VIDEO_RATE_MAX_KBPS) : 0;
    m_RateSaver = saver;
}
void AdaptiveDisplay::markRateStreamReady() {
    QMutexLocker lock(&m_Mutex);
    m_RateReady = true;
    m_Wake.wakeAll();
}
void AdaptiveDisplay::setRateCeiling(int kbps, bool saver) {
    QMutexLocker lock(&m_Mutex);
    m_RateCeiling = qBound(DP_VIDEO_RATE_MIN_KBPS, kbps, DP_VIDEO_RATE_MAX_KBPS);
    m_RateSaver = saver;
    m_Wake.wakeAll();
}
bool AdaptiveDisplay::takeRateFallback() {
    QMutexLocker lock(&m_Mutex);
    const bool fallback = m_RateFallback;
    m_RateFallback = false;
    return fallback;
}
void AdaptiveDisplay::setVideoPaused(bool paused) { QMutexLocker lock(&m_Mutex); m_VideoPaused = paused; }
void AdaptiveDisplay::setFullScreen(bool fullScreen) { QMutexLocker lock(&m_Mutex); m_FullScreen = fullScreen; }
bool AdaptiveDisplay::takeLeaveFullscreen() { QMutexLocker lock(&m_Mutex); bool value = m_LeaveFullscreen; m_LeaveFullscreen = false; return value; }
bool AdaptiveDisplay::failed() { QMutexLocker lock(&m_Mutex); return m_Failed; }
bool AdaptiveDisplay::wasTakenOver(int timeoutMs) {
    QMutexLocker lock(&m_Mutex);
    QElapsedTimer timer; timer.start();
    while (m_AdmissionRequired && !m_Failed && !m_TakenOver && timer.elapsed() < timeoutMs)
        m_Wake.wait(&m_Mutex, qMax(1, timeoutMs - int(timer.elapsed())));
    return m_TakenOver;
}
QSize AdaptiveDisplay::boundedSize(QSize pixels) {
    if (pixels.width() <= 0 || pixels.height() <= 0) return {};
    const double factor = qMin(1.0, qMin(double(DeskPortDisplay::MaxWidth) / pixels.width(), double(DeskPortDisplay::MaxHeight) / pixels.height()));
    // Four-pixel alignment also gives integral HiDPI logical modes.
    return QSize(qBound(640, int(pixels.width() * factor) & ~3, DeskPortDisplay::MaxWidth),
                 qBound(360, int(pixels.height() * factor) & ~3, DeskPortDisplay::MaxHeight));
}
bool AdaptiveDisplay::resize(const QSize& pixels, int scale, const std::function<void()>& progress,
                             const std::function<bool()>& confirmTakeover) {
    QMutexLocker lock(&m_Mutex);
    if (m_Failed || m_Pending || pixels != boundedSize(pixels) || (scale != 1 && scale != 2)) return false;
    m_Size = pixels; m_Scale = scale; m_Pending = true; m_Complete = false;
    m_Wake.wakeAll();
    QElapsedTimer timer; timer.start();
    while (!m_Complete && !m_Failed && timer.elapsed() < 25000) {
        if (m_ConfirmationNeeded && !m_ConfirmationReady) {
            lock.unlock();
            const bool confirmed = confirmTakeover && confirmTakeover();
            lock.relock(); m_Confirmed = confirmed; m_ConfirmationReady = true;
            m_Wake.wakeAll(); timer.restart();
        }
        m_Wake.wait(&m_Mutex, progress ? 20 : 100);
        if (progress) { lock.unlock(); progress(); lock.relock(); }
    }
    if (!m_Complete || !m_Result) { if (m_TopologyError.isEmpty()) m_TopologyError = "timeout"; m_Failed = true; requestInterruption(); return false; }
    return true;
}
void AdaptiveDisplay::run() {
    QSslSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.setReadBufferSize(32769);
    socket.setLocalCertificate(QSslCertificate(m_Certificate));
    socket.setPrivateKey(QSslKey(m_Key, QSsl::Rsa));
    socket.setProtocol(QSsl::TlsV1_2OrLater);
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    QObject::connect(&socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), &socket,
        [&socket, this](const QList<QSslError>& errors) {
            if (socket.peerCertificate() != m_Peer) { QMutexLocker lock(&m_Mutex); m_Retryable = false; return; }
            for (const auto& error : errors)
                if (error.error() != QSslError::SelfSignedCertificate && error.error() != QSslError::HostNameMismatch) return;
            socket.ignoreSslErrors(errors);
        }, Qt::DirectConnection);
    QByteArray buffer;
    auto send = [&socket](QJsonObject object) {
        socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
    };
    bool windowSupported = false;
    auto receive = [&](int timeout = 6000) -> QJsonObject {
        QElapsedTimer timer; timer.start();
        while (!isInterruptionRequested() && timer.elapsed() < timeout) {
            buffer += socket.readAll();
            if (buffer.size() > 32768) return {};
            const auto newline = buffer.indexOf('\n');
            if (newline >= 0) {
                const auto object = QJsonDocument::fromJson(buffer.left(newline)).object();
                buffer.remove(0, newline + 1);
                if (object["type"].toString() == DP_MESSAGE_SESSION_ENDED) {
                    QMutexLocker lock(&m_Mutex);
                    m_Retryable = false;
                    if (m_AdmissionRequired) { m_TakenOver = m_TakenOver || object["reason"].toString() == "taken-over"; m_Wake.wakeAll(); }
                }
                if (windowSupported && object["type"].toString() == DP_MESSAGE_CLIENT_WINDOW &&
                    object["action"].toString() == "leave-fullscreen") {
                    QMutexLocker lock(&m_Mutex); m_LeaveFullscreen = true;
                    continue;
                }
                return object;
            }
            if (socket.state() != QAbstractSocket::ConnectedState) return {};
            socket.waitForReadyRead(100);
        }
        return {};
    };
    bool connected = SmallTcp::connectBlocking(socket, m_Address, m_Port, 4000) && socket.peerCertificate() == m_Peer;
    if (!connected) { QMutexLocker lock(&m_Mutex); if (m_TopologyError.isEmpty()) m_TopologyError = "unavailable"; }
    if (connected) SmallTcp::accepted(socket, m_Address, m_Port);
    bool policySupported = false, videoPauseSupported = false;
    bool videoStateUnknown = !m_ResumeToken.isEmpty();
    if (connected) {
        const auto hello = receive();
        connected = hello["type"].toString() == "hello" && hello["meta"].toObject()["adaptiveDisplay"].toInt() == 1;
        { QMutexLocker lock(&m_Mutex); m_Lifecycle = hello["meta"].toObject()["sessionLifecycle"].toInt() == DP_SESSION_LIFECYCLE_VERSION; }
        videoPauseSupported = hello["meta"].toObject()["videoPause"].toInt() == DP_VIDEO_PAUSE_VERSION;
        { QMutexLocker lock(&m_Mutex); m_RateSupported = hello["meta"].toObject()["videoRate"].toInt() == DP_VIDEO_RATE_VERSION; }
        windowSupported = hello["meta"].toObject()["clientWindow"].toInt() == DP_CLIENT_WINDOW_VERSION;
        policySupported = hello["meta"].toObject()["displayPolicy"].toInt() == DP_DISPLAY_POLICY_VERSION;
        const auto advertised=hello["meta"].toObject()["displayModes"].toArray();
        QVector<QSize> modes;
        if(advertised.size()<=96)for(const auto value:advertised) {
            const auto mode=value.toObject();const QSize size(mode["width"].toInt(),mode["height"].toInt());
            if(size!=boundedSize(size)||!size.isValid()){modes.clear();break;}
            if(!modes.contains(size))modes.append(size);
        }
        { QMutexLocker lock(&m_Mutex); m_Modes=modes; }
        const bool admission = hello["meta"].toObject()["sessionTakeover"].toInt() == DP_SESSION_TAKEOVER_VERSION;
        const bool topology = hello["meta"].toObject()["sessionTopology"].toInt() == DP_SESSION_TOPOLOGY_VERSION;
        if (!admission || !topology) {
            QMutexLocker lock(&m_Mutex); m_TopologyError = "topology-unsupported"; m_Retryable = false;
            connected = false;
        }
        if (connected && admission) {
            QJsonObject query{{"type", DP_MESSAGE_SESSION_STATUS}, {"sessionTakeover", DP_SESSION_TAKEOVER_VERSION},
                              {"sessionTopology", DP_SESSION_TOPOLOGY_VERSION}};
            if (videoPauseSupported) query["videoPause"] = DP_VIDEO_PAUSE_VERSION;
            if ([&] { QMutexLocker lock(&m_Mutex); return m_RateSupported; }()) query["videoRate"] = DP_VIDEO_RATE_VERSION;
            if (m_Lifecycle) { query["sessionLifecycle"] = DP_SESSION_LIFECYCLE_VERSION; query["resumeToken"] = m_ResumeToken; }
            send(query);
            auto state = receive();
            bool admitted = state["type"].toString() == DP_MESSAGE_SESSION_STATE && state["admitted"].toBool();
            if (!admitted && state["type"].toString() == DP_MESSAGE_SESSION_STATE && state["busy"].toBool() &&
                !state["challenge"].toString().isEmpty()) {
                QElapsedTimer confirmation; confirmation.start();
                QMutexLocker lock(&m_Mutex); m_ConfirmationNeeded = true; m_Wake.wakeAll();
                while (!m_ConfirmationReady && !isInterruptionRequested() && confirmation.elapsed() < DP_SESSION_CONFIRMATION_TTL_MS)
                    m_Wake.wait(&m_Mutex, 100);
                const bool confirmed = m_ConfirmationReady && m_Confirmed && confirmation.elapsed() < DP_SESSION_CONFIRMATION_TTL_MS;
                const bool declined = m_ConfirmationReady;
                lock.unlock();
                if (confirmed) {
                    send({{"type", DP_MESSAGE_SESSION_TAKEOVER}, {"challenge", state["challenge"]}});
                    const auto result = receive(25000);
                    state = result;
                    admitted = result["type"].toString() == DP_MESSAGE_SESSION_RESULT && result["admitted"].toBool();
                } else {
                    // A declined takeover is cancellation, not authorization failure.
                    state["code"] = declined ? "cancelled" : "timeout";
                }
            }
            { QMutexLocker lock(&m_Mutex);
              if (admitted && m_Lifecycle) m_ResumeToken = state["resumeToken"].toString();
              // A supervisor may still be rebuilding capture during a bounded
              // retry. Only an authenticated unavailable response is transient;
              // denials, takeover prompts and malformed replies stay terminal.
              if (!admitted && !state.isEmpty() &&
                  !(state["type"].toString() == DP_MESSAGE_SESSION_RESULT &&
                    state["code"].toString() == "unavailable" && !m_ResumeToken.isEmpty()))
                  m_Retryable = false;
              if (!admitted && m_TopologyError != "cancelled") m_TopologyError = state.value("code").toString(state.value("busy").toBool() ? "busy" : "unavailable");
            }
            connected = connected && admitted;
        }
        if (connected && (!DPDisplayPolicyValid(m_Policy) || (!policySupported && m_Policy != DP_DISPLAY_PRIMARY_MIRROR))) {
            QMutexLocker lock(&m_Mutex); m_TopologyError = "policy-unsupported";
            connected = false;
        }
        if (!connected) qWarning() << "The host does not support the selected virtual screen policy";
    }
    int sequence = 0, videoSequence = 0, rateSequence = 0, rateSent = 0;
    bool rateSaverSent = false;
    quint64 rateGenerationSent = 0;
    bool videoPaused = false;
    QElapsedTimer heartbeat; heartbeat.start();
    QElapsedTimer resizeRetry;
    constexpr int ResizeRetryMs = 3000, ResizeRetryIntervalMs = 300;
    while (connected && !isInterruptionRequested()) {
        QSize size; int scale; bool pending, fullScreen;
        { QMutexLocker lock(&m_Mutex); pending = m_Pending; size = m_Size; scale = m_Scale; fullScreen = m_FullScreen; }
        if (pending) {
            size=selectedSize(size);scale=selectedScale(scale);
            QJsonObject request{{"type", DP_MESSAGE_DISPLAY_RESIZE}, {"seq", ++sequence}, {"width", size.width()}, {"height", size.height()}, {"scale", scale}};
            if (policySupported) request["displayPolicy"] = m_Policy;
            if (windowSupported) { request["clientWindow"] = DP_CLIENT_WINDOW_VERSION; request["clientFullScreen"] = fullScreen; }
            send(request);
            auto reply = receive();
            connected = reply.value("type").toString() == DP_MESSAGE_DISPLAY_RESULT && reply.value("seq").toInt() == sequence &&
                reply.value("width").toInt() == size.width() && reply.value("height").toInt() == size.height() && !reply.contains("error");
            // Older hosts combine invalid-size and busy refusals in this exact
            // reply. Our size and policy have already been validated. Retry that
            // refusal only; helper failures, policy errors and malformed replies
            // remain terminal. Keep the authenticated control lease throughout.
            const bool refused = reply.value("type").toString() == DP_MESSAGE_DISPLAY_RESULT &&
                reply.value("seq").toInt() == sequence &&
                reply.value("error").toString() == "Display size is invalid or the display is busy";
            if (refused && !resizeRetry.isValid()) resizeRetry.start();
            if (!connected && refused && resizeRetry.elapsed() < ResizeRetryMs && !isInterruptionRequested()) {
                qWarning() << "Adaptive display is busy; retrying the request";
                QElapsedTimer interval; interval.start();
                QMutexLocker lock(&m_Mutex);
                // Cancellation/release wakes this worker immediately. Other work
                // may wake it too, but must not turn the retry into a tight loop.
                while (!isInterruptionRequested() && interval.elapsed() < ResizeRetryIntervalMs &&
                       resizeRetry.elapsed() < ResizeRetryMs)
                    m_Wake.wait(&m_Mutex, qMax(1, qMin(ResizeRetryIntervalMs - int(interval.elapsed()),
                                                      ResizeRetryMs - int(resizeRetry.elapsed()))));
                if (!isInterruptionRequested() && resizeRetry.elapsed() < ResizeRetryMs) {
                    connected = socket.state() == QAbstractSocket::ConnectedState;
                    if (connected) {
                        heartbeat.restart();
                        continue;
                    }
                    reply = {}; // The peer disconnected during the retry interval.
                }
            }
            resizeRetry.invalidate();
            if (!connected) {
                QMutexLocker lock(&m_Mutex);
                if (m_TopologyError != "cancelled")
                    m_TopologyError = reply.isEmpty() ? "unavailable" : "display-failed";
                if (!reply.isEmpty()) m_Retryable = false;
            }
            if (!connected) qWarning() << "Adaptive display unavailable:" << reply.value("error").toString();
            { QMutexLocker lock(&m_Mutex); m_Warning = reply.value("warning").toString().left(512); m_Result = connected; if (connected) m_NegotiatedSize = size; m_Complete = true; m_Pending = false; m_Wake.wakeAll(); }
            heartbeat.restart();
        } else if (videoPauseSupported && [&] { QMutexLocker lock(&m_Mutex); return videoStateUnknown || m_VideoPaused != videoPaused; }()) {
            bool desired;
            { QMutexLocker lock(&m_Mutex); desired = m_VideoPaused; }
            send({{"type", DP_MESSAGE_VIDEO_STATE}, {"seq", ++videoSequence}, {"paused", desired}});
            const auto reply = receive(12000);
            connected = reply["type"].toString() == DP_MESSAGE_VIDEO_RESULT &&
                reply["seq"].toInt() == videoSequence && reply["paused"].isBool() &&
                reply["paused"].toBool() == desired && !reply.contains("error");
            if (connected) { videoPaused = desired; videoStateUnknown = false; }
            // A lost acknowledgment leaves remote state uncertain. End control
            // rather than silently treating a paused server as a running one.
            if (!connected) { QMutexLocker lock(&m_Mutex); m_Retryable = false; }
            heartbeat.restart();
        } else if ([&] { QMutexLocker lock(&m_Mutex); return m_RateSupported && m_RateReady && m_RateCeiling > 0 &&
                (m_RateCeiling != rateSent || m_RateSaver != rateSaverSent || m_RateGeneration != rateGenerationSent); }()) {
            int desired; bool saver; quint64 generation;
            { QMutexLocker lock(&m_Mutex);
              // A new stream can begin between the pending check and this lock.
              if (!m_RateSupported || !m_RateReady || m_RateCeiling <= 0) continue;
              desired = m_RateCeiling; saver = m_RateSaver; generation = m_RateGeneration;
            }
            send({{"type", DP_MESSAGE_VIDEO_RATE}, {"seq", ++rateSequence}, {"ceilingKbps", desired}, {"saver", saver}});
            const auto reply = receive(12000);
            // Hosts predating the saver field omit its echo; that is still applied.
            connected = reply["type"].toString() == DP_MESSAGE_VIDEO_RATE_RESULT &&
                reply["seq"].toInt() == rateSequence && reply["ceilingKbps"].toInt() == desired &&
                reply["live"].isBool() && !reply.contains("error") &&
                (!reply.contains("saver") || reply["saver"] == saver);
            if (connected) {
                rateSent = desired; rateSaverSent = saver; rateGenerationSent = generation;
                if (!reply["live"].toBool()) {
                    QMutexLocker lock(&m_Mutex);
                    // Keep the discovered fixed-backend result across the
                    // retained controller's reconnect; negotiate a real cap.
                    m_RateSupported = false;
                    m_RateFallback = true;
                }
            }
            // Same rule as video-state: an unacknowledged change is not applied.
            if (!connected) { QMutexLocker lock(&m_Mutex); m_Retryable = false; }
            heartbeat.restart();
        } else if (heartbeat.elapsed() >= 5000) {
            send({{"type", DP_MESSAGE_DISPLAY_PING}});
            connected = receive()["type"].toString() == DP_MESSAGE_DISPLAY_PONG;
            heartbeat.restart();
        } else {
            // Read unsolicited termination promptly, including buffered TLS data
            // after EOF. Video teardown can precede the control-channel reason.
            if (socket.bytesAvailable() || socket.waitForReadyRead(100)) {
                const auto unsolicited = receive(150);
                connected = unsolicited.isEmpty() && socket.state() == QAbstractSocket::ConnectedState;
            } else connected = socket.state() == QAbstractSocket::ConnectedState;
        }
    }
    { QMutexLocker lock(&m_Mutex);
      if (m_Release && m_Lifecycle && socket.state() == QAbstractSocket::ConnectedState) {
          send({{"type", DP_MESSAGE_SESSION_RELEASE}});
          socket.waitForBytesWritten(250);
      }
    }
    socket.abort();
    QMutexLocker lock(&m_Mutex); m_Failed = true; m_Wake.wakeAll();
}
