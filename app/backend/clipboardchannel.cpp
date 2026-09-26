#include "smalltcp.h"
#include "clipboardchannel.h"
#include "clipboardtraffic.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>

ClipboardChannel::ClipboardChannel(QString address, quint16 port, QSslCertificate peer, QByteArray cert, QByteArray key, bool nativeSharing)
    : m_Address(address), m_Port(port), m_Peer(peer), m_Cert(cert), m_Key(key) { m_NativeRequested = nativeSharing; start(); }
ClipboardChannel::~ClipboardChannel() { requestInterruption(); wait(); }
bool ClipboardChannel::nativeSharing() { QMutexLocker lock(&m_Mutex); return m_NativeActive; }
QString ClipboardChannel::notice() { QMutexLocker lock(&m_Mutex); return m_Notice; }
bool ClipboardChannel::ready() { QMutexLocker lock(&m_Mutex); return m_Ready; }
int ClipboardChannel::maxText() { QMutexLocker lock(&m_Mutex); return m_MaxText; }
QString ClipboardChannel::error() { QMutexLocker lock(&m_Mutex); return m_Error; }
bool ClipboardChannel::submit(const QJsonObject& request) {
    QMutexLocker lock(&m_Mutex);
    if (!m_Ready || m_Busy) return false;
    m_Request = request; m_Busy = true; return true;
}
bool ClipboardChannel::take(QJsonObject& reply) {
    QMutexLocker lock(&m_Mutex);
    if (m_Reply.isEmpty()) return false;
    reply = m_Reply; m_Reply = {}; m_Busy = false; return true;
}
void ClipboardChannel::run() {
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
        QElapsedTimer timeout; timeout.start();
        while (!isInterruptionRequested() && timeout.elapsed() < timeoutMs && socket.state() == QAbstractSocket::ConnectedState) {
            const auto chunk = socket.readAll();
            DeskPortTraffic::clipboardReceived().fetch_add(chunk.size(), std::memory_order_relaxed);
            buffer += chunk;
            if (buffer.size() > DeskPortClipboard::MaxFrame) return {};
            const int end = buffer.indexOf('\n', searchFrom);
            searchFrom = buffer.size();
            if (end >= 0) {
                const auto reply = QJsonDocument::fromJson(buffer.left(end)).object();
                buffer.remove(0, end + 1); return reply;
            }
            socket.waitForReadyRead(50);
        }
        return {};
    };
    auto send = [&](const QJsonObject& object) {
        socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
    };
    bool ok = SmallTcp::connectBlocking(socket, m_Address, m_Port, 3000) && socket.peerCertificate() == m_Peer;
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
        if (ok) { helper.start(); ok = helper.waitForStarted(5000); }
        { QMutexLocker lock(&m_Mutex); m_Ready = ok; m_NativeActive = ok; }
        QEventLoop loop;
        auto failed = [&] { ok = false; loop.quit(); };
        QObject::connect(&helper, &QProcess::readyReadStandardOutput, &loop, [&] {
            if (!helper.drain([&](const QJsonObject& message) {
                if (message["type"] == "clipboard-v2-status") {
                    QMutexLocker lock(&m_Mutex); m_Notice = message["message"].toString();
                } else send(message);
            }) || socket.bytesToWrite() > DeskPortClipboard::MaxFrame) failed();
        });
        QObject::connect(&helper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &loop, failed);
        QObject::connect(&helper, &QProcess::errorOccurred, &loop, failed);
        QObject::connect(&socket, &QSslSocket::disconnected, &loop, failed);
        auto drain = [&] {
            const auto chunk = socket.readAll();
            DeskPortTraffic::clipboardReceived().fetch_add(chunk.size(), std::memory_order_relaxed);
            buffer += chunk;
            if (buffer.size() > DeskPortClipboard::MaxFrame) { failed(); return; }
            int end;
            while ((end = buffer.indexOf('\n')) >= 0) {
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(buffer.left(end), &error); buffer.remove(0, end + 1);
                const auto message = doc.object(); const auto type = message["type"].toString();
                if (error.error != QJsonParseError::NoError || !doc.isObject()) { failed(); return; }
                if (type == "clipboard-v2-pong") continue;
                if (type != "clipboard-v2-offer" && type != "clipboard-v2-read" && type != "clipboard-v2-data") { failed(); return; }
                if (!helper.put(message)) { failed(); return; }
            }
        };
        QObject::connect(&socket, &QSslSocket::readyRead, &loop, drain);
        QTimer heartbeat, interrupted;
        QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&] { send({{"type", "clipboard-v2-ping"}}); });
        QObject::connect(&interrupted, &QTimer::timeout, &loop, [&] { if (isInterruptionRequested()) loop.quit(); });
        heartbeat.start(5000); interrupted.start(100);
        drain(); // A ready reply can share a TLS read with the first offer.
        if (ok && !isInterruptionRequested()) loop.exec();
        helper.closeWriteChannel();
        socket.abort();
        QMutexLocker lock(&m_Mutex); m_Ready = false;
        if (!isInterruptionRequested()) m_Error = QCoreApplication::translate("ClipboardChannel", "Clipboard sharing stopped. Reconnect to try again.");
        return;
    }
    if (ok) {
        send({{"type", "clipboard-start"}, {"maxText", DeskPortClipboard::MaxText}});
        const auto reply = receive();
        ok = reply["type"] == "clipboard-ready";
        QMutexLocker lock(&m_Mutex); m_MaxText = DeskPortClipboard::negotiatedLimit(reply["maxText"]); m_Ready = ok;
    }
    while (ok && !isInterruptionRequested()) {
        QJsonObject request;
        { QMutexLocker lock(&m_Mutex); request = m_Request; m_Request = {}; }
        if (request.isEmpty()) {
            socket.waitForReadyRead(25);
            ok = socket.state() == QAbstractSocket::ConnectedState;
            continue;
        }
        send(request);
        const auto reply = receive(120000);
        ok = reply["type"] == "clipboard-result" && reply["seq"] == request["seq"];
        if (ok) { QMutexLocker lock(&m_Mutex); m_Reply = reply; }
    }
    socket.abort();
    QMutexLocker lock(&m_Mutex);
    m_Ready = false;
    // Transport, negotiation and peer admission can all fail here. Do not
    // diagnose a disabled preference when the failure did not establish one.
    if (!isInterruptionRequested()) m_Error = QCoreApplication::translate("ClipboardChannel", "Clipboard sharing is unavailable. Reconnect to try again; desktop control is unaffected.");
}
