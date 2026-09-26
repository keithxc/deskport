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
void AdaptiveDisplay::cancel() { QMutexLocker lock(&m_Mutex); m_Failed = true; m_Retryable = false; requestInterruption(); m_Wake.wakeAll(); }
void AdaptiveDisplay::release() { QMutexLocker lock(&m_Mutex); m_Release = true; requestInterruption(); }
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
    if (!m_Complete || !m_Result) { m_Failed = true; requestInterruption(); return false; }
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
    if (connected) SmallTcp::accepted(socket, m_Address, m_Port);
    bool policySupported = false;
    if (connected) {
        const auto hello = receive();
        connected = hello["type"].toString() == "hello" && hello["meta"].toObject()["adaptiveDisplay"].toInt() == 1;
        { QMutexLocker lock(&m_Mutex); m_Lifecycle = hello["meta"].toObject()["sessionLifecycle"].toInt() == DP_SESSION_LIFECYCLE_VERSION; }
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
                lock.unlock();
                if (confirmed) {
                    send({{"type", DP_MESSAGE_SESSION_TAKEOVER}, {"challenge", state["challenge"]}});
                    const auto result = receive(25000);
                    state = result;
                    admitted = result["type"].toString() == DP_MESSAGE_SESSION_RESULT && result["admitted"].toBool();
                }
            }
            { QMutexLocker lock(&m_Mutex);
              if (admitted && m_Lifecycle) m_ResumeToken = state["resumeToken"].toString();
              if (!admitted && !state.isEmpty()) m_Retryable = false;
              if (!admitted) m_TopologyError = state["code"].toString();
            }
            connected = connected && admitted;
        }
        connected = connected && DPDisplayPolicyValid(m_Policy) && (policySupported || m_Policy == DP_DISPLAY_PRIMARY_MIRROR);
        if (!connected) qWarning() << "The host does not support the selected virtual screen policy";
    }
    int sequence = 0;
    QElapsedTimer heartbeat; heartbeat.start();
    while (connected && !isInterruptionRequested()) {
        QSize size; int scale; bool pending, fullScreen;
        { QMutexLocker lock(&m_Mutex); pending = m_Pending; size = m_Size; scale = m_Scale; fullScreen = m_FullScreen; }
        if (pending) {
            size=selectedSize(size);scale=selectedScale(scale);
            QJsonObject request{{"type", DP_MESSAGE_DISPLAY_RESIZE}, {"seq", ++sequence}, {"width", size.width()}, {"height", size.height()}, {"scale", scale}};
            if (policySupported) request["displayPolicy"] = m_Policy;
            if (windowSupported) { request["clientWindow"] = DP_CLIENT_WINDOW_VERSION; request["clientFullScreen"] = fullScreen; }
            send(request);
            const auto reply = receive();
            connected = reply["type"].toString() == DP_MESSAGE_DISPLAY_RESULT && reply["seq"].toInt() == sequence &&
                reply["width"].toInt() == size.width() && reply["height"].toInt() == size.height() && !reply.contains("error");
            if (!connected && !reply.isEmpty()) { QMutexLocker lock(&m_Mutex); m_Retryable = false; }
            if (!connected) qWarning() << "Adaptive display unavailable:" << reply["error"].toString();
            { QMutexLocker lock(&m_Mutex); m_Warning = reply["warning"].toString().left(512); m_Result = connected; if (connected) m_NegotiatedSize = size; m_Complete = true; m_Pending = false; m_Wake.wakeAll(); }
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
