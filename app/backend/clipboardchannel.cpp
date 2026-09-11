#include "clipboardchannel.h"
#include "clipboardprotocol.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QElapsedTimer>

ClipboardChannel::ClipboardChannel(QString address, quint16 port, QSslCertificate peer, QByteArray cert, QByteArray key)
    : m_Address(address), m_Port(port), m_Peer(peer), m_Cert(cert), m_Key(key) { start(); }
ClipboardChannel::~ClipboardChannel() { requestInterruption(); wait(); }
bool ClipboardChannel::ready() { QMutexLocker lock(&m_Mutex); return m_Ready; }
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
    auto receive = [&]() -> QJsonObject {
        QElapsedTimer timeout; timeout.start();
        while (!isInterruptionRequested() && timeout.elapsed() < 5000 && socket.state() == QAbstractSocket::ConnectedState) {
            buffer += socket.readAll();
            if (buffer.size() > DeskPortClipboard::MaxFrame) return {};
            const int end = buffer.indexOf('\n');
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
    socket.connectToHostEncrypted(m_Address, m_Port);
    bool ok = socket.waitForEncrypted(3000) && socket.peerCertificate() == m_Peer;
    if (ok) {
        const auto hello = receive();
        ok = hello["type"] == "hello" && hello["meta"].toObject()["clipboard"].toInt() == 1;
    }
    if (ok) {
        send({{"type", "clipboard-start"}});
        const auto reply = receive();
        ok = reply["type"] == "clipboard-ready";
        QMutexLocker lock(&m_Mutex); m_Ready = ok;
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
        const auto reply = receive();
        ok = reply["type"] == "clipboard-result" && reply["seq"] == request["seq"];
        if (ok) { QMutexLocker lock(&m_Mutex); m_Reply = reply; }
    }
    socket.abort();
    QMutexLocker lock(&m_Mutex);
    m_Ready = false;
    if (!isInterruptionRequested()) m_Error = QStringLiteral("Clipboard sharing unavailable. Enable it on both paired devices and reconnect.");
}
