#pragma once
#include "../../shared/deskport-core/include/deskport/session_graph.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QNetworkProxy>
#include <QPointer>
#include <QSet>
#include <QSslSocket>
#include <QSslKey>
#include <QSslError>
#include <QTimer>
#include <functional>

// Shared between the GUI's host admission and viewer control threads. Register
// before any admission request; remove only after the viewer control is joined.
namespace SessionGraph {
inline QString identity(const QSslCertificate& cert) {
    return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
}
struct Edge {
    QString token, address;
    quint16 port = 0;
    QSslCertificate peer;
    QByteArray certificate, key;
};
inline QMutex mutex;
inline QMap<QString, Edge> edges;
inline QMap<QString, QSet<QString>> owners;
inline QMap<QString, quint64> revisions;
inline bool reserve(const QString& self, const Edge& edge) {
    QMutexLocker lock(&mutex);
    if (identity(edge.peer) == self || owners.value(self).contains(edge.token)) return false;
    if (edges.contains(self) && identity(edges.value(self).peer) != identity(edge.peer)) return false;
    owners[self].insert(edge.token);
    edges[self] = edge; ++revisions[self]; return true;
}
inline void release(const QString& self, const QString& token) {
    QMutexLocker lock(&mutex);
    if (!owners[self].remove(token)) return;
    if (owners[self].isEmpty()) { owners.remove(self); edges.remove(self); ++revisions[self]; }
    else if (edges[self].token == token) edges[self].token = *owners[self].begin();
}
inline QPair<Edge, quint64> snapshot(const QString& self) {
    QMutexLocker lock(&mutex); return {edges.value(self), revisions.value(self)};
}

// A one-shot authenticated probe. No UI thread waits on network I/O, and no
// credentials, endpoints or full graph are returned to the querying peer.
class Probe : public QObject {
public:
    Probe(const Edge& edge, QJsonArray path, QObject* context, std::function<void(QString)> done)
        : QObject(context), m_Done(std::move(done)), m_Peer(edge.peer), m_Path(std::move(path)) {
        m_Socket.setProxy(QNetworkProxy::NoProxy);
        m_Socket.setReadBufferSize(8193);
        m_Socket.setLocalCertificate(QSslCertificate(edge.certificate));
        m_Socket.setPrivateKey(QSslKey(edge.key, QSsl::Rsa));
        m_Socket.setProtocol(QSsl::TlsV1_2OrLater);
        m_Socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
        connect(&m_Socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), this,
                [this](const QList<QSslError>& errors) {
            if (m_Socket.peerCertificate() != m_Peer) return;
            for (const auto& error : errors)
                if (error.error() != QSslError::SelfSignedCertificate && error.error() != QSslError::HostNameMismatch) return;
            m_Socket.ignoreSslErrors(errors);
        });
        connect(&m_Socket, &QSslSocket::readyRead, this, [this] { read(); });
        connect(&m_Socket, &QSslSocket::disconnected, this, [this] { finish("topology-unavailable"); });
        connect(&m_Socket, &QAbstractSocket::errorOccurred, this, [this] { finish("topology-unavailable"); });
        m_Deadline.setSingleShot(true);
        connect(&m_Deadline, &QTimer::timeout, this, [this] { finish("topology-unavailable"); });
        m_Deadline.start(4500);
        m_Socket.connectToHostEncrypted(edge.address, edge.port);
    }
private:
    void finish(const QString& code) {
        if (m_Finished) return;
        m_Finished = true; m_Deadline.stop();
        m_Socket.abort(); auto done = std::move(m_Done); deleteLater(); done(code);
    }
    void read() {
        if (m_Finished) return;
        if (m_Socket.peerCertificate() != m_Peer) { finish("topology-unavailable"); return; }
        m_Buffer += m_Socket.readAll();
        if (m_Buffer.size() > 8192) { finish("topology-unavailable"); return; }
        while (!m_Finished) {
            const auto end = m_Buffer.indexOf('\n'); if (end < 0) return;
            const auto frame = QJsonDocument::fromJson(m_Buffer.left(end)).object(); m_Buffer.remove(0, end + 1);
            if (!m_Hello) {
                if (frame["type"] != "hello" || frame["meta"].toObject()["sessionTopology"].toInt() != DP_SESSION_TOPOLOGY_VERSION) {
                    finish("topology-unsupported"); return;
                }
                m_Hello = true;
                m_Socket.write(QJsonDocument(QJsonObject{{"type", DP_MESSAGE_SESSION_PATH}, {"path", m_Path}}).toJson(QJsonDocument::Compact)+'\n');
            } else {
                if (frame["type"] != DP_MESSAGE_SESSION_PATH_RESULT || !frame["safe"].isBool()) { finish("topology-unavailable"); return; }
                const auto code = frame["code"].toString();
                if (frame["safe"].toBool() && code.isEmpty()) finish({});
                else finish(code == "cycle" || code == "topology-unsupported" ? code : "topology-unavailable");
            }
        }
    }
    QSslSocket m_Socket;
    QTimer m_Deadline;
    std::function<void(QString)> m_Done;
    QSslCertificate m_Peer;
    QJsonArray m_Path;
    QByteArray m_Buffer;
    bool m_Hello = false, m_Finished = false;
};
inline void check(const QString& self, QJsonArray path, QObject* context, std::function<void(QString)> done) {
    QList<QByteArray> storage; QList<const char*> pointers;
    for (const auto& item : path) storage.append(item.toString().toLatin1());
    for (const auto& item : storage) pointers.append(item.constData());
    const auto result = dp_session_path_check(pointers.constData(), size_t(pointers.size()), self.toLatin1().constData());
    if (result) { done(result == 1 ? "cycle" : "topology-unavailable"); return; }
    const auto current = snapshot(self);
    if (current.first.token.isEmpty()) { done({}); return; }
    path.append(self);
    new Probe(current.first, path, context, [self, current, done](QString code) {
        if (snapshot(self).second != current.second) code = "topology-unavailable";
        done(code);
    });
}
}
