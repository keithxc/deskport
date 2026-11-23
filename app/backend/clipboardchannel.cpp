#include "smalltcp.h"
#include "clipboardchannel.h"
#include "clipboardtraffic.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QScopeGuard>

ClipboardChannel::ClipboardChannel(QString address, quint16 port, QSslCertificate peer, QByteArray cert, QByteArray key, bool nativeSharing)
    : m_Address(address), m_Port(port), m_Peer(peer), m_Cert(cert), m_Key(key) { m_NativeRequested = nativeSharing; start(); }
ClipboardChannel::~ClipboardChannel() {
    m_Stopping.store(true);
    {
        QMutexLocker lock(&m_Mutex);
        if (m_Loop) QMetaObject::invokeMethod(m_Loop, &QEventLoop::quit, Qt::QueuedConnection);
    }
    wait();
}
bool ClipboardChannel::nativeSharing() { QMutexLocker lock(&m_Mutex); return m_NativeActive; }
QString ClipboardChannel::notice() { QMutexLocker lock(&m_Mutex); return m_Notice; }
bool ClipboardChannel::ready() { QMutexLocker lock(&m_Mutex); return m_Ready; }
int ClipboardChannel::maxText() { QMutexLocker lock(&m_Mutex); return m_MaxText; }
QString ClipboardChannel::error() { QMutexLocker lock(&m_Mutex); return m_Error; }
bool ClipboardChannel::submit(const QJsonObject& request) {
    QMutexLocker lock(&m_Mutex);
    if (!m_RequestTarget || !m_Ready || m_Busy || m_Stopping.load()) return false;
    m_Request = request; m_Busy = true;
    QMetaObject::invokeMethod(m_RequestTarget, m_RequestWake, Qt::QueuedConnection);
    return true;
}
bool ClipboardChannel::take(QJsonObject& reply) {
    QMutexLocker lock(&m_Mutex);
    if (m_Reply.isEmpty()) return false;
    reply = m_Reply; m_Reply = {}; m_Busy = false; return true;
}
void ClipboardChannel::run() {
    QEventLoop loop;
    // Retire the queued-call target under the same lock used by the caller.
    // The latch covers a stop before publication or before any exec().
    auto retireLoop = qScopeGuard([this] {
        QMutexLocker lock(&m_Mutex);
        m_Loop = nullptr; m_Ready = false;
    });
    { QMutexLocker lock(&m_Mutex); m_Loop = &loop; }
    QSslSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    QObject::connect(&socket, &QSslSocket::bytesWritten, &socket, [](qint64 n) {
        if (n > 0) DeskPortTraffic::clipboardSent().fetch_add(n, std::memory_order_relaxed);
    }, Qt::DirectConnection);
    socket.setReadBufferSize(DeskPortClipboard::MaxFrame + 1);
    socket.setLocalCertificate(QSslCertificate(m_Cert));
    socket.setPrivateKey(QSslKey(m_Key, QSsl::Rsa));
    socket.setProtocol(QSsl::TlsV1_2OrLater);
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    QObject::connect(&socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), &socket,
        [&](const QList<QSslError>& errors) {
            if (socket.peerCertificate() != m_Peer) return;
            for (const auto& e : errors)
                if (e.error() != QSslError::SelfSignedCertificate && e.error() != QSslError::HostNameMismatch) return;
            socket.ignoreSslErrors(errors);
        }, Qt::DirectConnection);
    QByteArray buffer;
    auto receive = [&](int timeoutMs = 5000) -> QJsonObject {
        int searchFrom = 0;
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.setTimerType(Qt::PreciseTimer);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        // Use a scoped context so callbacks cannot outlive this wait.
        bool disconnected = false;
        QObject context;
        QObject::connect(&socket, &QSslSocket::readyRead, &context, [&] { loop.quit(); });
        QObject::connect(&socket, &QSslSocket::disconnected, &context, [&] { disconnected = true; loop.quit(); });
        QObject::connect(&socket, &QSslSocket::errorOccurred, &context, [&] { disconnected = true; loop.quit(); });
        deadline.start(timeoutMs);
        while (!m_Stopping.load() && !disconnected && socket.state() == QAbstractSocket::ConnectedState) {
            const auto chunk = socket.readAll();
            DeskPortTraffic::clipboardReceived().fetch_add(chunk.size(), std::memory_order_relaxed);
            buffer += chunk;
            if (buffer.size() > DeskPortClipboard::MaxFrame) return {};
            const int end = buffer.indexOf('\n', searchFrom);
            searchFrom = buffer.size();
            if (end >= 0) {
                QJsonParseError error;
                const auto reply = QJsonDocument::fromJson(buffer.left(end), &error);
                buffer.remove(0, end + 1);
                return error.error == QJsonParseError::NoError && reply.isObject() ? reply.object() : QJsonObject();
            }
            if (!deadline.isActive()) break;
            loop.exec();
        }
        return {};
    };
    auto send = [&](const QJsonObject& object) {
        socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
    };
    bool ok = false;
    if (!m_Stopping.load()) {
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.setTimerType(Qt::PreciseTimer);
        bool failed = false;
        QObject context;
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&socket, &QSslSocket::encrypted, &context, [&] { loop.quit(); });
        QObject::connect(&socket, &QSslSocket::disconnected, &context, [&] { failed = true; loop.quit(); });
        QObject::connect(&socket, &QSslSocket::errorOccurred, &context, [&] { failed = true; loop.quit(); });
        // Preserve the shared small-TCP fallback and the bounded TLS startup.
        // Async startup lets queued quit interrupt DNS and handshake waits too.
        SmallTcp::connectAsync(&socket, m_Address, m_Port);
        deadline.start(SmallTcp::supported() ? 6000 : 3000);
        if (!socket.isEncrypted() && !failed && !m_Stopping.load()) loop.exec();
        ok = !m_Stopping.load() && !failed && socket.isEncrypted() && socket.peerCertificate() == m_Peer;
    }
    if (ok) SmallTcp::accepted(socket, m_Address, m_Port);
    bool native = false;
    if (ok) {
        const auto hello = receive();
        native = m_NativeRequested && hello["meta"].toObject()["clipboardV2"].toInt() == 1;
        ok = hello["type"] == "hello" && hello["meta"].toObject()["clipboard"].toInt() == 1;
    }
    if (ok && native) {
        send({{"type", "clipboard-v2-start"}});
        ok = receive()["type"] == "clipboard-v2-ready";
        ClipboardProcess helper;
        if (ok && !m_Stopping.load()) {
            QTimer deadline;
            deadline.setSingleShot(true);
            deadline.setTimerType(Qt::PreciseTimer);
            bool failed = false;
            QObject context;
            QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            QObject::connect(&helper, &QProcess::started, &context, [&] { loop.quit(); });
            QObject::connect(&helper, &QProcess::errorOccurred, &context, [&] { failed = true; loop.quit(); });
            QObject::connect(&helper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &context, [&] { failed = true; loop.quit(); });
            helper.start();
            deadline.start(5000);
            if (helper.state() == QProcess::Starting && !failed && !m_Stopping.load()) loop.exec();
            ok = !m_Stopping.load() && !failed && helper.state() == QProcess::Running;
        } else ok = false;
        { QMutexLocker lock(&m_Mutex); m_Ready = ok; m_NativeActive = ok; }
        QObject context;
        auto failed = [&] { ok = false; loop.quit(); };
        auto drainHelper = [&] {
            if (!ok || m_Stopping.load()) return;
            if (!helper.drain([&](const QJsonObject& message) {
                if (message["type"] == "clipboard-v2-status") {
                    QMutexLocker lock(&m_Mutex); m_Notice = message["message"].toString();
                } else send(message);
            }) || socket.bytesToWrite() > DeskPortClipboard::MaxFrame) failed();
        };
        QObject::connect(&helper, &QProcess::readyReadStandardOutput, &context, drainHelper);
        QObject::connect(&helper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &context, failed);
        QObject::connect(&helper, &QProcess::errorOccurred, &context, failed);
        QObject::connect(&socket, &QSslSocket::disconnected, &context, failed);
        QObject::connect(&socket, &QSslSocket::errorOccurred, &context, failed);
        qsizetype searchFrom = 0;
        auto drain = [&] {
            if (!ok || m_Stopping.load()) return;
            const auto chunk = socket.readAll();
            DeskPortTraffic::clipboardReceived().fetch_add(chunk.size(), std::memory_order_relaxed);
            buffer += chunk;
            if (buffer.size() > DeskPortClipboard::MaxFrame) { failed(); return; }
            qsizetype end;
            while ((end = buffer.indexOf('\n', searchFrom)) >= 0) {
                searchFrom = 0;
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(buffer.left(end), &error); buffer.remove(0, end + 1);
                const auto message = doc.object(); const auto type = message["type"].toString();
                if (error.error != QJsonParseError::NoError || !doc.isObject()) { failed(); return; }
                if (type == "clipboard-v2-pong") continue;
                if (type != "clipboard-v2-offer" && type != "clipboard-v2-read" && type != "clipboard-v2-data") { failed(); return; }
                if (!helper.put(message)) { failed(); return; }
            }
            searchFrom = buffer.size();
        };
        QObject::connect(&socket, &QSslSocket::readyRead, &context, drain);
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &context, [&] { send({{"type", "clipboard-v2-ping"}}); });
        heartbeat.start(5000);
        drain(); // A ready reply can share a TLS read with the first offer.
        drainHelper(); // The helper can write before its started signal is handled.
        if (socket.state() != QAbstractSocket::ConnectedState || helper.state() != QProcess::Running) ok = false;
        if (ok && !m_Stopping.load()) loop.exec();
        helper.closeWriteChannel();
        socket.abort();
        QMutexLocker lock(&m_Mutex); m_Ready = false;
        if (!m_Stopping.load()) m_Error = QCoreApplication::translate("ClipboardChannel", "Clipboard sharing stopped. Reconnect to try again.");
        return;
    }
    if (ok) {
        send({{"type", "clipboard-start"}, {"maxText", DeskPortClipboard::MaxText}});
        const auto reply = receive();
        ok = reply["type"] == "clipboard-ready";
        QMutexLocker lock(&m_Mutex); m_MaxText = DeskPortClipboard::negotiatedLimit(reply["maxText"]);
    }
    if (ok && !m_Stopping.load()) {
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.setTimerType(Qt::PreciseTimer);
        QJsonObject pending;
        QObject context;
        auto failed = [&] { ok = false; loop.quit(); };
        QObject::connect(&deadline, &QTimer::timeout, &context, failed);
        QObject::connect(&socket, &QSslSocket::disconnected, &context, failed);
        QObject::connect(&socket, &QSslSocket::errorOccurred, &context, failed);
        qsizetype searchFrom = 0;
        auto drain = [&] {
            if (!ok || m_Stopping.load()) return;
            const auto chunk = socket.readAll();
            DeskPortTraffic::clipboardReceived().fetch_add(chunk.size(), std::memory_order_relaxed);
            buffer += chunk;
            if (buffer.size() > DeskPortClipboard::MaxFrame) { failed(); return; }
            if (pending.isEmpty()) return;
            const auto end = buffer.indexOf('\n', searchFrom);
            if (end < 0) { searchFrom = buffer.size(); return; }
            searchFrom = 0;
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(buffer.left(end), &error);
            buffer.remove(0, end + 1);
            const auto reply = doc.object();
            if (error.error != QJsonParseError::NoError || !doc.isObject() ||
                reply["type"] != "clipboard-result" || reply["seq"] != pending["seq"]) { failed(); return; }
            deadline.stop(); pending = {};
            QMutexLocker lock(&m_Mutex); m_Reply = reply;
        };
        QObject::connect(&socket, &QSslSocket::readyRead, &context, drain);
        auto requestReady = [&] {
            if (!ok || m_Stopping.load()) return;
            { QMutexLocker lock(&m_Mutex); pending = m_Request; m_Request = {}; }
            if (pending.isEmpty()) return;
            deadline.start(120000); // One absolute deadline, including partial frames.
            send(pending);
            drain(); // A complete reply may already be buffered with negotiation.
        };
        auto retireRequest = qScopeGuard([this] {
            QMutexLocker lock(&m_Mutex);
            m_RequestTarget = nullptr; m_RequestWake = {}; m_Ready = false;
        });
        // Install the queued wake before publishing ready; submit cannot miss it.
        {
            QMutexLocker lock(&m_Mutex);
            m_RequestTarget = &context; m_RequestWake = requestReady;
            m_Ready = !m_Stopping.load();
        }
        drain();
        if (ok && !m_Stopping.load() && socket.state() == QAbstractSocket::ConnectedState) loop.exec();
    }
    socket.abort();
    QMutexLocker lock(&m_Mutex);
    m_Ready = false;
    // Transport, negotiation and peer admission can all fail here. Do not
    // diagnose a disabled preference when the failure did not establish one.
    if (!m_Stopping.load()) m_Error = QCoreApplication::translate("ClipboardChannel", "Clipboard sharing is unavailable. Reconnect to try again; desktop control is unaffected.");
}
