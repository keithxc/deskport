#include "adaptivedisplay.h"
#include "workspaceresolution.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QDebug>
#include <cmath>

AdaptiveDisplay::AdaptiveDisplay(QString address, quint16 port, QSslCertificate peer,
                                 QByteArray certificate, QByteArray key)
    : m_Address(address), m_Port(port), m_Peer(peer), m_Certificate(certificate), m_Key(key) { start(); }
AdaptiveDisplay::~AdaptiveDisplay() { requestInterruption(); wait(); }
QSize AdaptiveDisplay::boundedSize(QSize pixels) {
    if (pixels.width() <= 0 || pixels.height() <= 0) return {};
    const double factor = qMin(1.0, qMin(double(DeskPortDisplay::MaxWidth) / pixels.width(), double(DeskPortDisplay::MaxHeight) / pixels.height()));
    // Four-pixel alignment also gives integral HiDPI logical modes.
    return QSize(qBound(640, int(pixels.width() * factor) & ~3, DeskPortDisplay::MaxWidth),
                 qBound(360, int(pixels.height() * factor) & ~3, DeskPortDisplay::MaxHeight));
}
bool AdaptiveDisplay::resize(const QSize& pixels, int scale, const std::function<void()>& progress) {
    QMutexLocker lock(&m_Mutex);
    if (m_Failed || m_Pending || pixels != boundedSize(pixels) || (scale != 1 && scale != 2)) return false;
    m_Size = pixels; m_Scale = scale; m_Pending = true; m_Complete = false;
    QElapsedTimer timer; timer.start();
    while (!m_Complete && !m_Failed && timer.elapsed() < 10000) {
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
            if (socket.peerCertificate() != m_Peer) return;
            for (const auto& error : errors)
                if (error.error() != QSslError::SelfSignedCertificate && error.error() != QSslError::HostNameMismatch) return;
            socket.ignoreSslErrors(errors);
        }, Qt::DirectConnection);
    QByteArray buffer;
    auto send = [&socket](QJsonObject object) {
        socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
    };
    auto receive = [&]() -> QJsonObject {
        QElapsedTimer timer; timer.start();
        while (!isInterruptionRequested() && timer.elapsed() < 6000 && socket.state() == QAbstractSocket::ConnectedState) {
            buffer += socket.readAll();
            if (buffer.size() > 32768) return {};
            const auto newline = buffer.indexOf('\n');
            if (newline >= 0) {
                const auto object = QJsonDocument::fromJson(buffer.left(newline)).object();
                buffer.remove(0, newline + 1); return object;
            }
            socket.waitForReadyRead(100);
        }
        return {};
    };
    socket.connectToHostEncrypted(m_Address, m_Port);
    bool connected = socket.waitForEncrypted(4000) && socket.peerCertificate() == m_Peer;
    if (connected) {
        const auto hello = receive();
        connected = hello["type"].toString() == "hello" && hello["meta"].toObject()["adaptiveDisplay"].toInt() == 1;
    }
    int sequence = 0;
    QElapsedTimer heartbeat; heartbeat.start();
    while (connected && !isInterruptionRequested()) {
        QSize size; int scale; bool pending;
        { QMutexLocker lock(&m_Mutex); pending = m_Pending; size = m_Size; scale = m_Scale; }
        if (pending) {
            send({{"type", "display-resize"}, {"seq", ++sequence}, {"width", size.width()}, {"height", size.height()}, {"scale", scale}});
            const auto reply = receive();
            connected = reply["type"].toString() == "display-result" && reply["seq"].toInt() == sequence &&
                reply["width"].toInt() == size.width() && reply["height"].toInt() == size.height() && !reply.contains("error");
            if (!connected) qWarning() << "Adaptive display unavailable:" << reply["error"].toString();
            { QMutexLocker lock(&m_Mutex); m_Result = connected; m_Complete = true; m_Pending = false; m_Wake.wakeAll(); }
            heartbeat.restart();
        } else if (heartbeat.elapsed() > 5000) {
            send({{"type", "display-ping"}});
            connected = receive()["type"].toString() == "display-pong";
            heartbeat.restart();
        } else {
            msleep(50);
            connected = socket.state() == QAbstractSocket::ConnectedState;
        }
    }
    socket.abort();
    QMutexLocker lock(&m_Mutex); m_Failed = true; m_Wake.wakeAll();
}
