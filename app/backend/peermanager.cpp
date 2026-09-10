#include "peermanager.h"
#include "peerstore.h"
#include "nvaddress.h"
#include "localhostfilter.h"
#include <QSslSocket>
#include <QSslError>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QHostInfo>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <functional>
#include <QDebug>

namespace {
constexpr int MaxFrame = 32768;
QString fingerprint(const QSslCertificate& cert) { return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex()); }
QString trustId(const QString& fp) {
    return fp.mid(0,8)+"-"+fp.mid(8,4)+"-"+fp.mid(12,4)+"-"+fp.mid(16,4)+"-"+fp.mid(20,12);
}
class Listener : public QTcpServer {
public:
    std::function<void(qintptr)> incoming;
    using QTcpServer::QTcpServer;
    void incomingConnection(qintptr fd) override { incoming(fd); }
};
}
struct PeerManager::Link : QObject {
    using QObject::QObject;
    QSslSocket* socket = nullptr;
    QByteArray buffer;
    QJsonObject peer;
    QString transaction, fingerprint, requestedAddress;
    bool incoming = false, requested = false, accepted = false;
    bool localReady = false, remoteReady = false, ended = false;
};
PeerManager::PeerManager(HostManager* host, const QByteArray& cert, const QByteArray& key,
                         const QString& directory, quint16 port, const QHostAddress& listenAddress)
    : m_Host(host), m_Server(new Listener(this)), m_Certificate(cert), m_Key(key, QSsl::Rsa) {
    const QString path = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/binding" : directory;
    m_Path = path + "/peers.json";
    QDir().mkpath(path);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    bool ok;
    const auto saved = PeerStore::read(m_Path, &ok);
    m_Peers = saved["peers"].toObject();
    m_Healthy = ok && (saved.isEmpty() || (saved["version"].toInt() == 1 && saved["peers"].isObject())) && m_Host->available() && !m_Certificate.isNull() && !m_Key.isNull() && m_Host->prepareIdentity(cert, key);
    connect(host, &HostManager::trustUpdated, this, &PeerManager::granted);
    auto server = static_cast<Listener*>(m_Server);
    server->setProxy(QNetworkProxy::NoProxy);
    server->incoming = [this](qintptr fd) {
        auto socket = new QSslSocket;
        socket->setSocketDescriptor(fd);
        if (!m_Healthy || busy()) { socket->abort(); socket->deleteLater(); return; }
        auto link = new Link(this); link->socket = socket; socket->setParent(link);
        link->incoming = true; m_Link = link;
        attach(link);
        qInfo() << "Binding: incoming TCP connection";
        socket->startServerEncryption();
        emit changed();
    };
    if (!m_Healthy) m_Status = tr("Binding identity could not be loaded. Check host state and close other DeskPort instances.");
    else if (!server->listen(listenAddress, port)) {
        m_Healthy = false; m_Status = tr("Binding port is occupied. Existing services were left unchanged.");
    } else m_Status = tr("Ready for binding requests");
}
PeerManager::~PeerManager() { m_Server->close(); }
bool PeerManager::busy() const { return m_Link || m_TrustInFlight || !m_Revoking.isEmpty(); }
QString PeerManager::requestId() const {
    return m_Link && m_Link->incoming && m_Link->requested && !m_Link->accepted ? m_Link->transaction : QString();
}
QString PeerManager::pendingName() const {
    if (requestId().isEmpty()) return {};
    return tr("%1 (%2)\nDevice key: %3")
        .arg(m_Link->peer["name"].toString(), m_Link->socket->peerAddress().toString(), m_Link->fingerprint.left(16));
}
QVariantList PeerManager::peers() const {
    QVariantList result;
    for (auto it = m_Peers.begin(); it != m_Peers.end(); ++it) {
        auto peer = it.value().toObject(); peer["fingerprint"] = it.key(); result.append(peer.toVariantMap());
    }
    return result;
}
bool PeerManager::save() { return PeerStore::write(m_Path, {{"version", 1}, {"peers", m_Peers}}); }
QJsonObject PeerManager::metadata() const {
    auto meta = m_Host->identity();
    meta["name"] = QHostInfo::localHostName().left(64);
    meta["version"] = 1;
    meta["bindingPort"] = int(m_Server->serverPort());
    return meta;
}
void PeerManager::attach(Link* link) {
    auto socket = link->socket;
    socket->setProxy(QNetworkProxy::NoProxy);
    socket->setLocalCertificate(m_Certificate); socket->setPrivateKey(m_Key);
    socket->setProtocol(QSsl::TlsV1_2OrLater);
    socket->setReadBufferSize(MaxFrame + 1);
    socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
    connect(socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), link,
        [socket](const QList<QSslError>& errors) {
            // This is an explicitly approved TOFU protocol, not a web/CA identity.
            // Only self-signed and hostname errors are expected. Expired/invalid
            // certificates are rejected, and known endpoint keys are checked below.
            for (const auto& error : errors) {
                if (error.error() != QSslError::SelfSignedCertificate &&
                    error.error() != QSslError::HostNameMismatch) return;
            }
            socket->ignoreSslErrors(errors);
        });
    connect(socket, &QSslSocket::encrypted, link, [this, link] {
        qInfo() << "Binding: TLS established, incoming:" << link->incoming;
        const auto cert = link->socket->peerCertificate();
        if (cert.isNull() || cert == m_Certificate) { fail(link, tr("Invalid or local device identity")); return; }
        link->fingerprint = fingerprint(cert);
        const auto address = link->socket->peerAddress().toString();
        for (auto it = m_Peers.begin(); it != m_Peers.end(); ++it) {
            const auto peer = it.value().toObject();
            if (!link->incoming && ((peer["address"].toString() == address && peer["bindingPort"].toInt(48991) == link->socket->peerPort()) ||
                 (!link->requestedAddress.isEmpty() && peer["requestedAddress"].toString() == link->requestedAddress)) &&
                    it.key() != link->fingerprint) {
                fail(link, tr("This address has a different device key. Remove the old binding before replacing it.")); return;
            }
        }
        if (link->incoming) send(link, {{"type", "hello"}, {"meta", metadata()}});
        else send(link, {{"type", "request"}, {"tx", link->transaction}, {"meta", metadata()}});
        drain(link);
    });
    connect(socket, &QSslSocket::readyRead, link, [this, link] {
        drain(link);
    });
    connect(socket, &QSslSocket::disconnected, link, [this, link] {
        if (!link->ended) fail(link, tr("Binding connection closed. Check both devices; locally approved access may need removal."));
    });
    connect(socket, &QSslSocket::errorOccurred, link, [this, link](QAbstractSocket::SocketError) {
        if (!link->ended) fail(link, tr("Binding connection failed: %1").arg(link->socket->errorString()));
    });
    QTimer::singleShot(10000, link, [this, link] {
        if (!link->ended && link->peer.isEmpty()) fail(link, tr("Binding handshake timed out"));
    });
    QTimer::singleShot(120000, link, [this, link] {
        if (!link->ended) fail(link, tr("Binding request expired. No new request will be accepted automatically."));
    });
}
void PeerManager::drain(Link* link) {
        if (link->ended || !link->socket->isEncrypted()) return;
        link->buffer += link->socket->readAll();
        if (link->buffer.size() > MaxFrame) { fail(link, tr("Binding message too large")); return; }
        while (!link->ended && link->buffer.contains('\n')) {
            const int end = link->buffer.indexOf('\n');
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(link->buffer.left(end), &error);
            link->buffer.remove(0, end + 1);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { fail(link, tr("Invalid binding message")); return; }
            receive(link, doc.object());
        }
}
void PeerManager::request(const QString& value) {
    if (!m_Healthy || busy()) return;
    // This dialog uses the binding endpoint, not the video port.
    const auto url = QUrl::fromUserInput("https://" + value.trimmed());
    if (url.host().isEmpty() || !url.userInfo().isEmpty() || url.path().size() > 1 || url.hasQuery() || url.hasFragment() ||
            url.port(48991) <= 0 || url.port(48991) > 65535) {
        m_Status = tr("Enter an IP address or domain, optionally followed by the binding port."); emit changed(); return;
    }
    auto link = new Link(this); link->socket = new QSslSocket(link); m_Link = link;
    link->transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    link->requestedAddress = value.trimmed();
    m_Status = tr("Connecting to the other computer…");
    attach(link);
    link->socket->connectToHostEncrypted(url.host(), quint16(url.port(48991)));
    emit changed();
}
bool PeerManager::acceptMetadata(Link* link, const QJsonObject& metadata) {
    const auto hostCert = QSslCertificate(metadata["hostCert"].toString().toUtf8());
    const QString id = metadata["hostId"].toString();
    const int port = metadata["hostPort"].toInt();
    const int bindingPort = metadata["bindingPort"].toInt();
    if (bindingPort < 1 || bindingPort > 65535 || metadata["version"].toInt() != 1 || hostCert.isNull() || QUuid(id).isNull() ||
        id == m_Host->identity()["hostId"].toString() || port < 1024 || port > 65514 ||
        metadata["name"].toString().trimmed().isEmpty() || metadata["name"].toString().size() > 64) return false;
    link->peer = metadata;
    link->peer["address"] = link->socket->peerAddress().toString();
    link->peer["requestedAddress"] = link->requestedAddress;
    link->peer["clientCert"] = QString::fromUtf8(link->socket->peerCertificate().toPem());
    return true;
}
void PeerManager::send(Link* link, const QJsonObject& message) {
    if (!link->ended) {
        qInfo() << "Binding: sending" << message["type"].toString();
        link->socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}
void PeerManager::receive(Link* link, const QJsonObject& message) {
    const QString type = message["type"].toString();
    qInfo() << "Binding: received" << type;
    if (type == "hello" && !link->incoming && link->peer.isEmpty() && !link->accepted) {
        if (!acceptMetadata(link, message["meta"].toObject())) fail(link, tr("Unsupported peer identity"));
    } else if (type == "request" && link->incoming && !link->requested) {
        if (!acceptMetadata(link, message["meta"].toObject()) || QUuid(message["tx"].toString()).isNull()) {
            fail(link, tr("Unsupported binding request")); return;
        }
        link->transaction = message["tx"].toString(); link->requested = true;
        send(link, {{"type", "pending"}, {"tx", link->transaction}});
        m_Status = tr("A computer is requesting mutual desktop access"); emit changed(); emit incomingRequest();
    } else if (type == "pending" && !link->incoming && !link->requested && !link->accepted && !link->peer.isEmpty() && message["tx"].toString() == link->transaction) {
        link->requested = true;
        m_Status = tr("Request received. Waiting for the other computer to approve mutual desktop access…"); emit changed();
    } else if (type == "accept" && !link->incoming && !link->accepted && !link->peer.isEmpty() && message["tx"].toString() == link->transaction) {
        link->accepted = true; grant(link);
    } else if (type == "ready" && link->accepted && !link->remoteReady && message["tx"].toString() == link->transaction) {
        const int port = message["hostPort"].toInt();
        if (port < 1024 || port > 65514) { fail(link, tr("Invalid host port")); return; }
        link->peer["hostPort"] = port; link->remoteReady = true;
        finish(link);
    } else if (type == "reject" && !link->accepted) fail(link, tr("The other computer declined the binding request"));
    else fail(link, tr("Unexpected or repeated binding message"));
}
void PeerManager::approve(const QString& transaction) {
    if (transaction.isEmpty() || requestId() != transaction) return;
    auto link = m_Link; link->accepted = true;
    send(link, {{"type", "accept"}, {"tx", link->transaction}});
    grant(link); emit changed();
}
void PeerManager::reject(const QString& transaction) {
    if (transaction.isEmpty() || requestId() != transaction) return;
    send(m_Link, {{"type", "reject"}});
    fail(m_Link, tr("Binding declined"));
}
void PeerManager::grant(Link* link) {
    m_Status = tr("Saving mutual access and restarting the DeskPort host…");
    link->peer["ready"] = false;
    m_Peers[link->fingerprint] = link->peer;
    if (!save()) { fail(link, tr("Cannot save binding. No host access was added.")); return; }
    m_TrustInFlight = true;
    m_Host->updatePeerTrust(trustId(link->fingerprint), link->peer["name"].toString(), link->socket->peerCertificate());
    emit changed();
}
void PeerManager::granted(bool success) {
    if (!m_TrustInFlight) return;
    m_TrustInFlight = false;
    if (!m_Revoking.isEmpty()) {
        if (success) {
            const auto old = m_Peers;
            m_Peers.remove(m_Revoking);
            if (!save()) { m_Peers = old; success = false; }
        }
        m_Revoking.clear(); m_Status = success ? tr("This device's access to this computer was removed") : tr("Could not remove access; retry"); emit changed(); return;
    }
    auto link = m_Link;
    if (!link || link->ended) { m_Status = tr("Binding interrupted. Review saved device access before retrying."); emit changed(); return; }
    if (!success) { fail(link, tr("Host authorization could not be saved. Binding is incomplete.")); return; }
    link->localReady = true;
    link->peer["granted"] = true;
    m_Peers[link->fingerprint] = link->peer;
    if (!save()) { fail(link, tr("Access was approved but device metadata could not be saved. Binding is incomplete.")); return; }
    if (!m_Host->running()) m_Host->start(2560, 1440);
    send(link, {{"type", "ready"}, {"tx", link->transaction}, {"hostPort", m_Host->basePort()}});
    finish(link);
}
void PeerManager::finish(Link* link) {
    if (!link->localReady || !link->remoteReady) return;
    link->peer["ready"] = true; link->peer["granted"] = true;
    m_Peers[link->fingerprint] = link->peer;
    if (!save()) { fail(link, tr("Could not persist completed binding")); return; }
    emit peerBound(link->peer.toVariantMap());
    m_Status = tr("Bound in both directions. Desktop availability depends on sharing and system permissions.");
    link->ended = true; m_Link = nullptr;
    connect(link->socket, &QSslSocket::disconnected, link, &QObject::deleteLater);
    link->socket->disconnectFromHost();
    QTimer::singleShot(2000, link, &QObject::deleteLater); emit changed();
}
void PeerManager::fail(Link* link, const QString& message) {
    if (link->ended) return;
    qWarning() << "Binding:" << message;
    link->ended = true;
    if (m_Link == link) m_Link = nullptr;
    m_Status = message; connect(link->socket, &QSslSocket::disconnected, link, &QObject::deleteLater);
    link->socket->disconnectFromHost();
    QTimer::singleShot(2000, link, &QObject::deleteLater); emit changed();
}
void PeerManager::cancel() { if (m_Link) fail(m_Link, tr("Binding cancelled. Review saved access if approval had already completed.")); }
void PeerManager::restoreHosts() {
    for (const auto& peer : m_Peers) if (peer.toObject()["ready"].toBool()) emit peerBound(peer.toObject().toVariantMap());
}
void PeerManager::revoke(const QString& fp) {
    if (busy() || !m_Peers.contains(fp)) return;
    m_Revoking = fp; m_TrustInFlight = true;
    m_Host->updatePeerTrust(trustId(fp), QString(), QSslCertificate(), true);
    emit changed();
}
