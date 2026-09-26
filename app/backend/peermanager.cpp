#include "smalltcp.h"
#include "../../shared/deskport-core/include/deskport/protocol.h"
#include "peermanager.h"
#include <QSysInfo>
#include <QPointer>
#include "peerstore.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#ifdef Q_OS_MACOS
#include "macclipboard.h"
#endif
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include "nvaddress.h"
#include "localhostfilter.h"
#include <QSslSocket>
#include <QUrl>
#include <QSslError>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QHostInfo>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <functional>
#include <QDebug>
#include <QRegularExpression>

namespace {
constexpr int MaxFrame = 32768;
QString fingerprint(const QSslCertificate& cert) { return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex()); }
QString requestedHost(const QString& endpoint) {
    if (endpoint.isEmpty()) return {};
    const auto url = QUrl::fromUserInput("https://" + endpoint.trimmed());
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        url.path().size() > 1 || url.hasQuery() || url.hasFragment() ||
        url.port(48991) <= 0 || url.port(48991) > 65535) return {};
    return url.host();
}
QString trustId(const QString& fp) {
    return fp.mid(0,8)+"-"+fp.mid(8,4)+"-"+fp.mid(12,4)+"-"+fp.mid(16,4)+"-"+fp.mid(20,12);
}
QString dnsName(const QString& value) {
    const QString name = value.trimmed();
    static const QRegularExpression syntax(QStringLiteral(
        "^(?=.{1,253}$)[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*\\.?$"));
    return QHostAddress(name).isNull() && syntax.match(name).hasMatch() ? name : QString();
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
    bool endpointRefresh = false;
    QString expectedFingerprint;
    QJsonObject expectedPeer;
    int displayPolicy = -1;
    bool clipboardControl = false;
    ClipboardProcess* clipboardHelper = nullptr;
    QString clipboardText, clipboardEncoded;
    int clipboardMaxText = DeskPortClipboard::LegacyMaxText;
    bool clipboardSupported = false, clipboardIsText = false, clipboardDirty = true;
    qint64 clipboardNativeRevision = -1;
    int clipboardRevision = 0, clipboardSequence = 0;
    qint64 lastClipboardRequest = 0;
    bool sessionOptIn = false, sessionAdmitted = false;
    QString sessionChallenge, sessionSnapshot, sessionLease;
    qint64 sessionDeadline = 0;
    quint64 sessionEpoch = 0;
    bool displayControl = false;
    bool caretUpdates = false;
    bool clientWindow = false;
    // Last full-screen state reported with display-resize; older clients do not
    // report it and keep the request available.
    bool clientFullScreen = true;
    bool lifecycle = false, recovering = false;
    int displaySequence = 0;
    qint64 lastDisplayRequest = 0;
    bool localReady = false, remoteReady = false, ended = false;
    bool hostReadyRequired = false, hostReadySent = false;
};
qint64 PeerManager::nativeClipboardRevision() const {
#ifdef Q_OS_MACOS
    // Offscreen tests must never touch the user's native clipboard.
    if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
        return deskPortClipboardChangeCount();
#endif
    return -1;
}

PeerManager::PeerManager(HostManager* host, const QByteArray& cert, const QByteArray& key,
                         const QString& directory, quint16 port, const QHostAddress& listenAddress, Mode mode)
    : m_Host(host), m_Server(nullptr), m_ListenAddress(listenAddress), m_Persistent(directory.isEmpty()),
      m_ClientOnly(mode == Mode::ClientOnly), m_Certificate(cert), m_Key(key, QSsl::Rsa) {
    const QString path = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/binding" : directory;
    m_Path = path + "/peers.json";
    QDir().mkpath(path);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    bool ok;
    const auto saved = PeerStore::read(m_Path, &ok);
    m_Peers = saved["peers"].toObject();
    // Older versions saved the resolved socket IP, including temporary DNS IPs.
    // Recover the locally entered endpoint without changing the pinned identity.
    for (auto it = m_Peers.begin(); it != m_Peers.end(); ++it) {
        auto peer = it.value().toObject();
        const auto requested = requestedHost(peer["requestedAddress"].toString());
        const auto storedName = dnsName(peer["address"].toString());
        const auto hostName = m_ClientOnly && !requested.isEmpty() ? requested :
                              !storedName.isEmpty() ? storedName : requested;
        if (!hostName.isEmpty()) {
            if (!peer.contains("resolvedAddress")) peer["resolvedAddress"] = peer["address"];
            peer["address"] = hostName;
            it.value() = peer;
        }
    }
    m_Healthy = ok && (saved.isEmpty() || (saved["version"].toInt() == 1 && saved["peers"].isObject())) && !m_Certificate.isNull() && !m_Key.isNull() &&
        (m_ClientOnly || (m_Host->available() && m_Host->prepareIdentity(cert, key)));
    connect(host, &HostManager::trustUpdated, this, &PeerManager::granted);
    connect(host, &HostManager::caretChanged, this, [this](const QJsonObject& caret) {
        if (m_DisplayLink && m_DisplayLink->displayControl && m_DisplayLink->caretUpdates)
            send(m_DisplayLink, {{"type", DP_MESSAGE_TEXT_CARET}, {"caret", caret}});
    });
    connect(host, &HostManager::displayResized, this, [this](int seq, int width, int height, const QString& error) {
        auto link = m_DisplayLink;
        if (!link || link->ended || seq != link->displaySequence) return;
        link->displaySequence = 0;
        QJsonObject response{{"type", DP_MESSAGE_DISPLAY_RESULT}, {"seq", seq}, {"width", width}, {"height", height}};
        if (!error.isEmpty()) response["error"] = error;
        if (!m_Host->displayWarning().isEmpty()) response["warning"] = m_Host->displayWarning();
        send(link, response);
    });
    auto watchdog = new QTimer(this);
    connect(watchdog, &QTimer::timeout, this, [this] {
        if (m_SessionLink && QDateTime::currentMSecsSinceEpoch() - m_SessionLink->lastDisplayRequest > 20000)
            fail(m_SessionLink, tr("Session controller disconnected"));
        if (m_DisplayLink && (!m_Host->adaptiveDisplayAvailable() ||
            QDateTime::currentMSecsSinceEpoch() - m_DisplayLink->lastDisplayRequest > 20000))
            fail(m_DisplayLink, tr("Display controller disconnected"));
    });
    connect(watchdog, &QTimer::timeout, this, [this] {
        if (m_ClipboardLink && (!m_Host->running() ||
            QDateTime::currentMSecsSinceEpoch() - m_ClipboardLink->lastClipboardRequest > 150000))
            fail(m_ClipboardLink, tr("Clipboard session ended"));
    });
    watchdog->start(2000);
    // Isolated managers are driven explicitly by tests; production retries one
    // remembered peer per tick without blocking the UI or active media sessions.
    if (directory.isEmpty()) {
        auto refresh = new QTimer(this);
        connect(refresh, &QTimer::timeout, this, &PeerManager::refreshEndpoints);
        refresh->start(10000);
        QTimer::singleShot(1000, this, &PeerManager::refreshEndpoints);
    }
    m_IdentityHealthy = m_Healthy;
    auto server = createListener();
    m_Server = server;
    if (m_Persistent) {
        const int configured = QSettings().value("binding/port", int(port)).toInt();
        if (configured >= 1024 && configured <= 65535) port = quint16(configured);
    }
    if (!m_Healthy) m_Status = m_ClientOnly ? tr("Binding identity or saved devices could not be loaded.") :
        tr("Binding identity could not be loaded. Check host state and close other DeskPort instances.");
    else if (m_ClientOnly) {
        // Outbound TLS uses the viewer identity. There is no local host to
        // advertise, provision or accept connections for (including aliases).
        m_Status = tr("Ready to request access to another computer");
    } else if (!server->listen(listenAddress, port)) {
        m_Healthy = false; m_Status = tr("Binding port is occupied. Existing services were left unchanged.");
    } else {
        m_Status = tr("Ready for binding requests");
        if (m_Persistent) {
            const auto previous = QSettings().value("binding/previousPorts").toStringList();
            for (const auto& value : previous.mid(0, 8)) {
                const int oldPort = value.toInt();
                if (oldPort < 1024 || oldPort > 65535 || oldPort == port) continue;
                auto alias = createListener();
                if (alias->listen(m_ListenAddress, quint16(oldPort))) m_PreviousServers.append(alias);
                else { qWarning() << "Previous connection port could not be restored:" << oldPort; alias->deleteLater(); }
            }
        }
    }
}
QTcpServer* PeerManager::createListener() {
    auto server = new Listener(this);
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
    return server;
}
bool PeerManager::setConnectionPort(int value) {
    if (!m_IdentityHealthy || m_ClientOnly) return false;
    if (value < 1024 || value > 65535) {
        m_Status = tr("Enter a connection port between 1024 and 65535."); emit changed(); return false;
    }
    if (value == port()) return true;
    QTcpServer* replacement = nullptr;
    for (auto server : m_PreviousServers) {
        if (server->serverPort() == value) { replacement = server; break; }
    }
    const bool reused = replacement != nullptr;
    if (!reused) {
        // Keep a bounded durable set of old rendezvous ports for offline peers.
        if (m_PreviousServers.size() >= 8) {
            m_Status = tr("Previous connection ports are still reserved. Reuse an earlier port."); emit changed(); return false;
        }
        replacement = createListener();
        if (!replacement->listen(m_ListenAddress, quint16(value))) {
            replacement->deleteLater();
            m_Status = tr("Connection port is unavailable. The current port is unchanged."); emit changed(); return false;
        }
    }
    QStringList previous;
    for (auto server : m_PreviousServers)
        if (server != replacement) previous.append(QString::number(server->serverPort()));
    if (m_Server->isListening()) previous.append(QString::number(port()));
    if (m_Persistent) {
        QSettings settings;
        const auto oldPort = settings.value("binding/port", 48991);
        const auto oldPrevious = settings.value("binding/previousPorts");
        settings.setValue("binding/port", value); settings.setValue("binding/previousPorts", previous);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            settings.setValue("binding/port", oldPort); settings.setValue("binding/previousPorts", oldPrevious);
            if (!reused) { replacement->close(); replacement->deleteLater(); }
            m_Status = tr("Could not save the connection port. The current port is unchanged."); emit changed(); return false;
        }
    }
    m_PreviousServers.removeAll(replacement);
    if (m_Server->isListening()) m_PreviousServers.append(m_Server);
    else m_Server->deleteLater();
    m_Server = replacement;
    m_Healthy = m_IdentityHealthy;
    m_Status = tr("Connection port saved. Existing connections and previous entry ports remain available.");
    emit changed(); return true;
}
PeerManager::~PeerManager() { m_Server->close(); }
bool PeerManager::busy() const { return m_Link || m_TrustInFlight || !m_Revoking.isEmpty(); }
QString PeerManager::requestId() const {
    return m_Link && m_Link->incoming && m_Link->requested && !m_Link->accepted ? m_Link->transaction : QString();
}
bool PeerManager::pendingClientOnly() const {
    return m_Link && m_Link->peer.value("role").toString() == "client";
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
bool PeerManager::canReleaseClientFullscreen() const {
    return m_DisplayLink && !m_DisplayLink->ended && !m_DisplayLink->recovering && m_DisplayLink->clientWindow &&
        m_DisplayLink->clientFullScreen;
}
void PeerManager::releaseClientFullscreen() {
    if (canReleaseClientFullscreen())
        send(m_DisplayLink, {{"type", DP_MESSAGE_CLIENT_WINDOW}, {"action", "leave-fullscreen"}});
}
bool PeerManager::save() { return PeerStore::write(m_Path, {{"version", 1}, {"peers", m_Peers}}); }
QJsonObject PeerManager::metadata() const {
    if (m_ClientOnly) return {{"version", 1}, {"clientBinding", 1}, {"role", "client"},
                              {"name", QHostInfo::localHostName().left(64)}};
    auto meta = m_Host->identity();
    meta["name"] = QHostInfo::localHostName().left(64);
    meta["dnsName"] = dnsName(QHostInfo::localHostName());
    meta["os"] = QSysInfo::prettyProductName();
    meta["version"] = 1;
    meta["clientBinding"] = 1;
    meta["endpointRefresh"] = 1;
    meta["clipboard"] = 1;
    meta["sessionTakeover"] = DP_SESSION_TAKEOVER_VERSION;
    meta["clientWindow"] = DP_CLIENT_WINDOW_VERSION;
    meta["sessionLifecycle"] = DP_SESSION_LIFECYCLE_VERSION;
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    meta["clipboardV2"] = 1;
#endif
    meta["adaptiveDisplay"] = m_Host->adaptiveDisplayAvailable() ? 1 : 0;
    if (!m_Host->displayModes().isEmpty()) meta["displayModes"] = m_Host->displayModes();
    if (m_Host->displayPoliciesAvailable()) meta["displayPolicy"] = DP_DISPLAY_POLICY_VERSION;
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
        if (link->endpointRefresh && link->fingerprint != link->expectedFingerprint) {
            fail(link, tr("Endpoint refresh identity mismatch")); return;
        }
        const auto address = link->socket->peerAddress().toString();
        for (auto it = m_Peers.begin(); it != m_Peers.end(); ++it) {
            const auto peer = it.value().toObject();
            if (!link->incoming && (((peer["address"].toString() == address || peer["resolvedAddress"].toString() == address ||
                 peer["address"].toString().compare(requestedHost(link->requestedAddress), Qt::CaseInsensitive) == 0) && peer["bindingPort"].toInt(48991) == link->socket->peerPort()) ||
                 (!link->requestedAddress.isEmpty() && peer["requestedAddress"].toString() == link->requestedAddress)) &&
                    it.key() != link->fingerprint) {
                fail(link, tr("This address has a different device key. Remove the old binding before replacing it.")); return;
            }
        }
        if (!link->incoming) SmallTcp::accepted(*link->socket,
            link->endpointRefresh ? link->expectedPeer["address"].toString() : requestedHost(link->requestedAddress), link->socket->peerPort());
        if (link->incoming) send(link, {{"type", "hello"}, {"meta", metadata()}});
        else if (!link->endpointRefresh && !m_ClientOnly) send(link, {{"type", "request"}, {"tx", link->transaction}, {"meta", metadata()}});
        drain(link);
    });
    connect(socket, &QSslSocket::readyRead, link, [this, link] {
        drain(link);
    });
    connect(socket, &QSslSocket::disconnected, link, [this, link] {
        if (!link->ended) transportLost(link);
    });
    connect(socket, &QSslSocket::errorOccurred, link, [this, link](QAbstractSocket::SocketError) {
        if (!link->ended) transportLost(link);
    });
    QTimer::singleShot(10000, link, [this, link] {
        if (!link->ended && !link->sessionOptIn && !link->displayControl && !link->clipboardControl && link->peer.isEmpty()) fail(link, tr("Binding handshake timed out"));
    });
    QTimer::singleShot(120000, link, [this, link] {
        if (!link->ended && !link->sessionAdmitted && !link->displayControl && !link->clipboardControl) fail(link, tr("Binding request expired. No new request will be accepted automatically."));
    });
}
void PeerManager::drain(Link* link) {
        if (link->ended || !link->socket->isEncrypted()) return;
        const int previousSize = link->buffer.size();
        link->buffer += link->socket->readAll();
        if (link->buffer.size() > (link->clipboardControl ? DeskPortClipboard::MaxFrame : MaxFrame)) { fail(link, tr("Binding message too large")); return; }
        int end = link->buffer.indexOf('\n', previousSize);
        while (!link->ended && end >= 0) {
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(link->buffer.left(end), &error);
            link->buffer.remove(0, end + 1);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) { fail(link, tr("Invalid binding message")); return; }
            receive(link, doc.object());
            end = link->buffer.indexOf('\n');
        }
}
void PeerManager::request(const QString& value) {
    if (!m_Healthy || busy()) return;
    // This dialog uses the binding endpoint, not the video port.
    const auto url = QUrl::fromUserInput("https://" + value.trimmed());
    const int remotePort = url.port(m_ClientOnly ? 48991 : port());
    if (url.host().isEmpty() || !url.userInfo().isEmpty() || url.path().size() > 1 || url.hasQuery() || url.hasFragment() ||
            remotePort <= 0 || remotePort > 65535) {
        m_Status = tr("Enter an IP address or domain, optionally followed by the binding port."); emit changed(); return;
    }
    auto link = new Link(this); link->socket = new QSslSocket(link); m_Link = link;
    link->transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    link->requestedAddress = value.trimmed();
    m_Status = tr("Connecting to the other computer…");
    attach(link);
    SmallTcp::connectAsync(link->socket, url.host(), quint16(remotePort));
    emit changed();
}
bool PeerManager::acceptMetadata(Link* link, const QJsonObject& metadata) {
    const auto hostCert = QSslCertificate(metadata["hostCert"].toString().toUtf8());
    const QString id = metadata["hostId"].toString();
    const int port = metadata["hostPort"].toInt();
    const int bindingPort = metadata["bindingPort"].toInt();
    const bool clientOnly = metadata["role"].toString() == "client";
    if (metadata["version"].toInt() != 1 ||
        metadata["name"].toString().trimmed().isEmpty() || metadata["name"].toString().size() > 64 ||
        metadata["name"].toString().contains(QRegularExpression("[\\x00-\\x1f\\x7f]"))) return false;
    if (m_ClientOnly && metadata["clientBinding"].toInt() != 1) return false;
    if (clientOnly && (!link->incoming || metadata["clientBinding"].toInt() != 1 ||
        metadata.contains("hostId") || metadata.contains("hostCert") || metadata.contains("hostPort") ||
        metadata.contains("bindingPort"))) return false;
    if (!clientOnly && (bindingPort < 1 || bindingPort > 65535 || hostCert.isNull() || QUuid(id).isNull() ||
        id == m_Host->identity()["hostId"].toString() || port < 1024 || port > 65514 ||
        metadata["name"].toString().trimmed().isEmpty() || metadata["name"].toString().size() > 64)) return false;
    link->peer = clientOnly ? QJsonObject{{"version",1},{"role","client"},{"clientBinding",1},{"name",metadata["name"]}} : metadata;
    // Trust flags are local state, never remote capability advertisements.
    link->peer.remove("ready"); link->peer.remove("granted");
    link->peer.remove("outboundOnly");
    if (m_ClientOnly) link->peer["outboundOnly"] = true;
    link->peer["resolvedAddress"] = link->socket->peerAddress().toString();
    const auto previous = m_Peers.value(link->fingerprint).toObject();
    const auto requested = requestedHost(link->requestedAddress);
    // Client-only viewers may not resolve the host's advertised LAN name.
    // Preserve an explicitly entered numeric address as well as DNS names.
    QString address = m_ClientOnly ? requested : dnsName(requested);
    if (address.isEmpty()) address = dnsName(previous["address"].toString());
    if (address.isEmpty()) address = dnsName(requestedHost(previous["requestedAddress"].toString()));
    if (address.isEmpty()) address = dnsName(metadata["dnsName"].toString());
    // Older DeskPort versions advertised localHostName in name. Accept only
    // hostname syntax here, never a friendly display label containing spaces.
    if (address.isEmpty() && !metadata.contains("dnsName")) address = dnsName(metadata["name"].toString());
    if (address.isEmpty()) address = requested.isEmpty() ? link->socket->peerAddress().toString() : requested;
    // Only locally chosen aliases may override the remote display name.
    if (previous["customName"].toBool()) {
        link->peer["name"] = previous["name"];
        link->peer["customName"] = true;
    }
    link->peer["address"] = address;
    link->peer["requestedAddress"] = link->requestedAddress;
    link->peer["clientCert"] = QString::fromUtf8(link->socket->peerCertificate().toPem());
    return true;
}
void PeerManager::send(Link* link, const QJsonObject& message) {
    if (!link->ended) {
        if (link->socket->bytesToWrite() > DeskPortClipboard::MaxFrame) {
            fail(link, tr("Control connection is not consuming messages")); return;
        }
        if (!message["type"].toString().startsWith("clipboard-"))
            qInfo() << "Binding: sending" << message["type"].toString();
        link->socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}
void PeerManager::receive(Link* link, const QJsonObject& message) {
    const QString type = message["type"].toString();
    if (link->endpointRefresh) {
        if (type == "hello") {
            if (message["meta"].toObject()["endpointRefresh"].toInt() != 1) {
                fail(link, tr("Peer does not support automatic endpoint refresh")); return;
            }
            send(link, {{"type", "endpoint-query"}}); return;
        }
        const auto current = m_Peers.value(link->expectedFingerprint).toObject();
        const auto meta = message["meta"].toObject();
        const int port = meta["hostPort"].toInt();
        const int entryPort = meta["bindingPort"].toInt();
        // Pin BOTH TLS client identity and the streaming identity. An in-flight
        // reply must not resurrect revoked access or overwrite a local edit.
        if (type != "endpoint-result" || current != link->expectedPeer ||
            !current["ready"].toBool() || !current["granted"].toBool() ||
            meta["version"].toInt() != 1 || port < 1024 || port > 65514 || entryPort < 1 || entryPort > 65535 ||
            meta["hostId"] != current["hostId"] ||
            QSslCertificate(meta["hostCert"].toString().toUtf8()).isNull() ||
            QSslCertificate(meta["hostCert"].toString().toUtf8()) !=
                QSslCertificate(current["hostCert"].toString().toUtf8())) {
            fail(link, tr("Endpoint refresh rejected")); return;
        }
        if (port != current["hostPort"].toInt()) {
            auto updated = current; updated["hostPort"] = port;
            // Preserve the contacted entry, which may be independently forwarded.
            // The advertised listener is not a replacement for that working route.
            m_Peers[link->expectedFingerprint] = updated;
            if (!save()) {
                m_Peers[link->expectedFingerprint] = current;
                fail(link, tr("Could not save refreshed endpoint")); return;
            }
            emit peerBound(updated.toVariantMap()); emit changed();
        }
        link->ended = true; m_RefreshLink = nullptr;
        link->socket->disconnectFromHost();
        QTimer::singleShot(2000, link, &QObject::deleteLater); return;
    }
    if (type == DP_MESSAGE_SESSION_RELEASE) {
        if (link == m_SessionLink && link->lifecycle && link->sessionAdmitted)
            fail(link, tr("Client disconnected"));
        else fail(link, tr("Session release requires the admitted controller"));
        return;
    }
    if (type == DP_MESSAGE_SESSION_STATUS || type == DP_MESSAGE_SESSION_TAKEOVER) {
        sessionRequest(link, message); return;
    }
    if (type == "endpoint-query") {
        const auto peer = m_Peers.value(link->fingerprint).toObject();
        if (!link->incoming || link->requested || link->displayControl || link->clipboardControl ||
            !peer["ready"].toBool() || !peer["granted"].toBool()) {
            fail(link, tr("Endpoint refresh requires an approved device")); return;
        }
        send(link, {{"type", "endpoint-result"}, {"meta", metadata()}});
        link->ended = true;
        if (m_Link == link) m_Link = nullptr;
        // notify busy bindings after releasing the incoming refresh connection
        emit changed();
        link->socket->disconnectFromHost();
        QTimer::singleShot(2000, link, &QObject::deleteLater); return;
    }
    if (!type.startsWith("clipboard-")) qInfo() << "Binding: received" << type;
    if (type == "clipboard-v2-start") {
        const auto peer = m_Peers[link->fingerprint].toObject();
        if (!link->incoming || link->requested || link->displayControl || link->clipboardControl ||
            !peer["ready"].toBool() || !peer["granted"].toBool() || !m_Host->running() || m_ClipboardLink ||
            m_SessionOperation || (m_SessionLink && m_SessionLink->fingerprint != link->fingerprint)) {
            fail(link, tr("Clipboard sharing requires an enabled host and an approved exclusive session")); return;
        }
        link->clipboardControl = true; m_ClipboardLink = link; ++m_SessionEpoch;
        if (m_Link == link) m_Link = nullptr;
        link->lastClipboardRequest = QDateTime::currentMSecsSinceEpoch();
        link->socket->setReadBufferSize(DeskPortClipboard::MaxFrame + 1);
        auto helper = new ClipboardProcess(link); link->clipboardHelper = helper;
        helper->setArguments({"--clipboard-helper", "--clipboard-helper-host"});
        connect(helper, &QProcess::readyReadStandardOutput, link, [this, link, helper] {
            if (!helper->drain([this, link](const QJsonObject& message) {
                if (message["type"] != "clipboard-v2-status") send(link, message);
            })) fail(link, tr("Invalid clipboard helper response"));
        });
        connect(helper, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), link, [this, link] {
            if (!link->ended) fail(link, tr("Native clipboard helper stopped"));
        });
        connect(helper, &QProcess::errorOccurred, link, [this, link](QProcess::ProcessError) {
            if (!link->ended) fail(link, tr("Native clipboard helper unavailable"));
        });
        helper->start();
        send(link, {{"type", "clipboard-v2-ready"}}); emit changed(); return;
    }
    if (link->clipboardHelper) {
        link->lastClipboardRequest = QDateTime::currentMSecsSinceEpoch();
        if (type == "clipboard-v2-ping") { send(link, {{"type", "clipboard-v2-pong"}}); return; }
        if (type != "clipboard-v2-offer" && type != "clipboard-v2-read" && type != "clipboard-v2-data") {
            fail(link, tr("Unexpected clipboard message")); return;
        }
        if (!link->clipboardHelper->put(message)) fail(link, tr("Clipboard helper is not consuming messages"));
        return;
    }
    if (type == "clipboard-start" || type == "clipboard-poll") {
        const auto peer = m_Peers[link->fingerprint].toObject();
        if (!link->incoming || link->requested || link->displayControl || !peer["ready"].toBool() ||
            !peer["granted"].toBool() || !m_Host->running() ||
            (m_ClipboardLink && m_ClipboardLink != link) || m_SessionOperation ||
            (m_SessionLink && m_SessionLink->fingerprint != link->fingerprint)) {
            fail(link, tr("Clipboard sharing requires an enabled host and an approved exclusive session")); return;
        }
        auto clipboard = QGuiApplication::clipboard();
        if (type == "clipboard-start")
            link->clipboardMaxText = DeskPortClipboard::negotiatedLimit(message["maxText"]);
        // Cocoa dataChanged is activation-dependent. Poll only the cheap native
        // generation while sharing, then let Qt synchronize MIME data on change.
        const auto nativeRevision = nativeClipboardRevision();
        if (nativeRevision >= 0 && nativeRevision != link->clipboardNativeRevision) {
            link->clipboardNativeRevision = nativeRevision;
            link->clipboardDirty = true;
        }
        bool isText = link->clipboardIsText;
        QString current = link->clipboardText;
        if (link->clipboardDirty) {
            auto mime = clipboard->mimeData(QClipboard::Clipboard);
            isText = mime && mime->hasText() && !mime->hasUrls();
            current = isText ? mime->text() : QString();
            link->clipboardDirty = false;
        }
        // Keep a bounded encoded snapshot. Polls must not re-encode a multi-MiB
        // clipboard four times per second when its contents have not changed.
        QString encoded = link->clipboardEncoded;
        bool supported = link->clipboardSupported;
        if (type == "clipboard-start" || current != link->clipboardText || isText != link->clipboardIsText) {
            encoded.clear();
            supported = isText && DeskPortClipboard::encode(current, encoded, link->clipboardMaxText);
        }
        link->clipboardIsText = isText;
        if (type == "clipboard-start") {
            if (link->clipboardControl) { fail(link, tr("Clipboard session already started")); return; }
            link->clipboardControl = true; m_ClipboardLink = link; ++m_SessionEpoch;
            connect(clipboard, &QClipboard::dataChanged, link, [link] { link->clipboardDirty = true; });
            if (m_Link == link) m_Link = nullptr;
            link->socket->setReadBufferSize(DeskPortClipboard::MaxFrame + 1);
            link->clipboardText = current; link->clipboardSupported = supported; link->clipboardEncoded = encoded;
            link->lastClipboardRequest = QDateTime::currentMSecsSinceEpoch();
            send(link, {{"type", "clipboard-ready"}, {"maxText", link->clipboardMaxText}}); emit changed(); return;
        }
        if (!link->clipboardControl || message["seq"].toInt() != link->clipboardSequence + 1 ||
            message["rev"].toInt(-1) < 0 || message["rev"].toInt() > link->clipboardRevision) {
            fail(link, tr("Invalid clipboard sequence")); return;
        }
        link->lastClipboardRequest = QDateTime::currentMSecsSinceEpoch();
        ++link->clipboardSequence;
        if (current != link->clipboardText || supported != link->clipboardSupported) {
            link->clipboardText = current; link->clipboardSupported = supported; link->clipboardEncoded = encoded; ++link->clipboardRevision;
        }
        QJsonObject reply{{"type", "clipboard-result"}, {"seq", link->clipboardSequence}};
        // Host revision is authoritative: an intervening host copy wins a concurrent copy.
        if (message["rev"].toInt() != link->clipboardRevision) {
            if (supported) reply["text"] = encoded;
            else reply["error"] = QStringLiteral("This remote copy is not supported text or exceeds the %1 MiB limit and was skipped. Text sharing remains active.").arg(link->clipboardMaxText / (1024 * 1024));
        } else if (message.contains("text")) {
            QString text;
            if (!DeskPortClipboard::decode(message["text"], text, link->clipboardMaxText)) {
                fail(link, tr("Invalid or oversized clipboard text")); return;
            }
            clipboard->setText(text, QClipboard::Clipboard);
            link->clipboardText = text; link->clipboardEncoded = message["text"].toString();
            link->clipboardSupported = true; link->clipboardIsText = true; ++link->clipboardRevision;
        }
        reply["rev"] = link->clipboardRevision;
        send(link, reply); return;
    }
    if (link->clipboardControl) { fail(link, tr("Unexpected clipboard message")); return; }
    if (type == DP_MESSAGE_DISPLAY_RESIZE || type == DP_MESSAGE_DISPLAY_PING) {
        const auto peer = m_Peers[link->fingerprint].toObject();
        if (!link->incoming || link->requested || !peer["ready"].toBool() || !peer["granted"].toBool() ||
            !m_Host->adaptiveDisplayAvailable() || (m_DisplayLink && m_DisplayLink != link) ||
            m_SessionOperation || (m_SessionLink && m_SessionLink != link) ||
            (link->sessionOptIn && !link->sessionAdmitted)) {
            fail(link, tr("Virtual display control requires an available host and an approved, exclusive device")); return;
        }
        if (type == DP_MESSAGE_DISPLAY_PING) {
            if (!link->displayControl && !link->sessionAdmitted) { fail(link, tr("Display control has not started")); return; }
            link->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
            send(link, {{"type", DP_MESSAGE_DISPLAY_PONG}}); return;
        }
        const int policy = message.contains("displayPolicy") ? message["displayPolicy"].toInt(-1) : DP_DISPLAY_PRIMARY_MIRROR;
        const int seq = message["seq"].toInt();
        if ((!m_Host->displayPoliciesAvailable() && policy != 0) || !DPDisplayPolicyValid(policy) || (link->displayPolicy >= 0 && link->displayPolicy != policy)) {
            send(link, {{"type", DP_MESSAGE_DISPLAY_RESULT}, {"seq", seq}, {"error", "Invalid or changed session display policy"}}); return;
        }
        if (seq <= 0 || link->displaySequence || !m_Host->resizeDisplay(message["width"].toInt(), message["height"].toInt(), message["scale"].toInt(), seq, policy)) {
            send(link, {{"type", DP_MESSAGE_DISPLAY_RESULT}, {"seq", seq}, {"error", "Display size is invalid or the display is busy"}}); return;
        }
        link->clientWindow = message["clientWindow"].toInt() == DP_CLIENT_WINDOW_VERSION;
        link->clientFullScreen = !message.contains("clientFullScreen") || message["clientFullScreen"].toBool();
        link->displayPolicy = policy;
        link->displayControl = true; link->caretUpdates = message["textCaret"].toBool(); link->displaySequence = seq;
        link->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
        if (m_DisplayLink != link) ++m_SessionEpoch;
        m_DisplayLink = link;
        emit changed();
        if (m_Link == link) m_Link = nullptr;
        emit changed(); return;
    }
    if (link->displayControl) { fail(link, tr("Unexpected display-control message")); return; }
    if (type == "hello" && !link->incoming && link->peer.isEmpty() && !link->accepted) {
        if (!acceptMetadata(link, message["meta"].toObject())) { fail(link, tr("Unsupported peer identity")); return; }
        // Client-only requests require a host that advertises the existing
        // mobile-client contract; do not claim host capabilities as a fallback.
        if (m_ClientOnly) send(link, {{"type", "request"}, {"tx", link->transaction}, {"meta", metadata()}});
    } else if (type == "request" && link->incoming && !link->requested) {
        if (!acceptMetadata(link, message["meta"].toObject()) || QUuid(message["tx"].toString()).isNull()) {
            fail(link, tr("Unsupported binding request")); return;
        }
        link->transaction = message["tx"].toString(); link->requested = true;
        send(link, {{"type", "pending"}, {"tx", link->transaction}});
        m_Status = pendingClientOnly() ? tr("A client is requesting access to this computer") : tr("A computer is requesting mutual desktop access"); emit changed(); emit incomingRequest();
    } else if (type == "pending" && !link->incoming && !link->requested && !link->accepted && !link->peer.isEmpty() && message["tx"].toString() == link->transaction) {
        link->requested = true;
        m_Status = m_ClientOnly ? tr("Request received. Waiting for the other computer to approve access…") :
                                  tr("Request received. Waiting for the other computer to approve mutual desktop access…"); emit changed();
    } else if (type == "accept" && !link->incoming && !link->accepted && !link->peer.isEmpty() &&
               (!m_ClientOnly || link->requested) && message["tx"].toString() == link->transaction) {
        link->accepted = true;
        if (m_ClientOnly) {
            m_Status = tr("Access approved. Waiting for the host to save authorization…"); emit changed();
        } else grant(link);
    } else if (type == "ready" && m_ClientOnly && !link->incoming && link->accepted &&
               !link->remoteReady && message["tx"].toString() == link->transaction) {
        const int hostPort = message["hostPort"].toInt();
        if (hostPort < 1024 || hostPort > 65514) { fail(link, tr("Invalid host port")); return; }
        link->peer["hostPort"] = hostPort;
        link->peer["ready"] = false; link->peer["granted"] = true;
        const auto previous = m_Peers;
        m_Peers[link->fingerprint] = link->peer;
        if (!save()) {
            m_Peers = previous;
            fail(link, tr("Could not save the approved host. Binding is incomplete.")); return;
        }
        link->localReady = true; link->remoteReady = true;
        send(link, {{"type", "client-ready"}, {"tx", link->transaction}});
        m_Status = tr("Confirming the saved binding with the host…"); emit changed();
    } else if (type == "bound" && m_ClientOnly && !link->incoming && link->accepted &&
               link->localReady && link->remoteReady && message["tx"].toString() == link->transaction) {
        finish(link);
    } else if (type == "client-ready" && link->incoming && pendingClientOnly() &&
               link->accepted && link->localReady && !link->remoteReady && message["tx"].toString() == link->transaction) {
        link->remoteReady = true;
        finish(link);
    } else if (type == "ready" && !m_ClientOnly && !pendingClientOnly() && link->accepted && !link->remoteReady && message["tx"].toString() == link->transaction) {
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
    if (!m_ClientOnly && !m_Host->available()) {
        const auto message = tr("The bundled DeskPort host is missing. Repair the installation to enable sharing.");
        m_Status = message; emit changed();
        fail(link, message);
        return;
    }
    m_Status = pendingClientOnly() ? tr("Saving client access…") : tr("Saving mutual access…");
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
    link->hostReadyRequired = !m_ClientOnly && link->peer.value("role").toString() != "client";
    const auto sendReady = [this, link] {
        if (link->ended || link->hostReadySent ||
            (link->hostReadyRequired && !m_Host->canPair())) return;
        link->hostReadySent = true;
        send(link, {{"type", "ready"}, {"tx", link->transaction}, {"hostPort", m_Host->basePort()}});
        finish(link);
    };
    if (link->hostReadyRequired && !m_Host->canPair()) {
        connect(m_Host, &HostManager::changed, link, [sendReady] { sendReady(); });
        QTimer::singleShot(60000, link, [this, link] {
            if (!link->ended && !link->hostReadySent)
                fail(link, QCoreApplication::translate("HostManager", "Host startup timed out; see logs"));
        });
    } else sendReady();
}
void PeerManager::finish(Link* link) {
    if (!link->localReady || !link->remoteReady ||
        (link->hostReadyRequired && !link->hostReadySent)) return;
    link->peer["ready"] = true; link->peer["granted"] = true;
    const auto previous = m_Peers;
    m_Peers[link->fingerprint] = link->peer;
    if (!save()) {
        m_Peers = previous;
        fail(link, tr("Could not persist completed binding")); return;
    }
    const bool clientOnly = link->peer.value("role").toString() == "client";
    if (clientOnly) send(link, {{"type","bound"},{"tx",link->transaction}});
    else emit peerBound(link->peer.toVariantMap());
    m_Status = m_ClientOnly ? tr("Host access saved. This computer can connect to the approved host.") :
        clientOnly ? tr("Client access approved. This device can connect to this computer.") : tr("Bound in both directions. Desktop availability depends on sharing and system permissions.");
    link->ended = true; m_Link = nullptr;
    connect(link->socket, &QSslSocket::disconnected, link, &QObject::deleteLater);
    link->socket->disconnectFromHost();
    QTimer::singleShot(2000, link, &QObject::deleteLater); emit changed();
}
void PeerManager::transportLost(Link* link) {
    if (link->recovering) return;
    if (link == m_SessionLink && link->lifecycle && link->sessionAdmitted && !m_SessionOperation &&
        m_Revoking != link->fingerprint) {
        link->recovering = true;
        // Preserve the original display snapshot and reservation for this identity.
        if (m_ClipboardLink && m_ClipboardLink->fingerprint == link->fingerprint)
            fail(m_ClipboardLink, tr("Clipboard transport interrupted"));
        QTimer::singleShot(15000, link, [this, link] {
            if (!link->ended && link->recovering) fail(link, tr("Session recovery timed out"));
        });
        emit changed();
        return;
    }
    fail(link, tr("Control connection closed"));
}
void PeerManager::fail(Link* link, const QString& message) {
    if (link->ended) return;
    if (!link->endpointRefresh) qWarning() << "Binding:" << message;
    if (link->lifecycle && link->sessionAdmitted && !link->recovering &&
        link->socket->state() == QAbstractSocket::ConnectedState && link->socket->bytesToWrite() <= DeskPortClipboard::MaxFrame)
        send(link, {{"type", DP_MESSAGE_SESSION_ENDED}, {"reason", "ended"}});
    link->ended = true;
    if (m_RefreshLink == link) {
        m_RefreshLink = nullptr;
        link->socket->abort(); link->deleteLater();
        return; // Background reachability failures must not replace UI status.
    }
    if (m_SessionLink == link) {
        m_SessionLink = nullptr; ++m_SessionEpoch;
        const auto lease = link->sessionLease;
        m_Host->sessionControl({{"action", "release"}, {"lease", lease}}, this, [](QJsonObject) {});
    }
    if (m_ClipboardLink == link) { m_ClipboardLink = nullptr; ++m_SessionEpoch; }
    if (link->clipboardHelper) link->clipboardHelper->closeWriteChannel();
    if (m_DisplayLink == link) { m_DisplayLink = nullptr; ++m_SessionEpoch; m_Host->restoreDisplay(); }
    if (m_Link == link) m_Link = nullptr;
    m_Status = message; connect(link->socket, &QSslSocket::disconnected, link, &QObject::deleteLater);
    link->socket->disconnectFromHost();
    QTimer::singleShot(2000, link, &QObject::deleteLater); emit changed();
}
void PeerManager::sessionError(Link* link, const QString& code) {
    send(link, {{"type", DP_MESSAGE_SESSION_RESULT}, {"admitted", false}, {"code", code},
                {"error", tr("Session access was not granted (%1). Reconnect and try again.").arg(code)}});
}
void PeerManager::sessionRequest(Link* link, const QJsonObject& message) {
    const auto peer = m_Peers.value(link->fingerprint).toObject();
    if (!link->incoming || link->requested || link->clipboardControl ||
        !peer["ready"].toBool() || !peer["granted"].toBool() || m_Revoking == link->fingerprint || !m_Host->running()) {
        sessionError(link, "unauthorized"); return;
    }
    const bool takeover = message["type"].toString() == DP_MESSAGE_SESSION_TAKEOVER;
    if ((!takeover && message["sessionTakeover"].toInt() != DP_SESSION_TAKEOVER_VERSION) ||
        (takeover && !link->sessionOptIn)) { sessionError(link, "unauthorized"); return; }
    link->lifecycle = message["sessionLifecycle"].toInt() == DP_SESSION_LIFECYCLE_VERSION;
    link->sessionOptIn = true;
    if (m_Link == link) m_Link = nullptr;
    if (m_SessionOperation) { sessionError(link, "busy"); return; }
    auto previous = m_SessionLink;
    if (!takeover && link != previous && previous && previous->lifecycle && !previous->ended &&
        link->lifecycle && previous->fingerprint == link->fingerprint &&
        !previous->sessionLease.isEmpty() && message["resumeToken"].toString() == previous->sessionLease) {
        link->sessionLease = previous->sessionLease;
        link->sessionAdmitted = true;
        link->displayControl = previous->displayControl;
        link->displayPolicy = previous->displayPolicy;
        link->clientWindow = previous->clientWindow; link->clientFullScreen = previous->clientFullScreen;
        link->caretUpdates = previous->caretUpdates;
        m_SessionLink = link;
        if (m_DisplayLink == previous) m_DisplayLink = link;
        if (m_ClipboardLink && m_ClipboardLink->fingerprint == link->fingerprint)
            fail(m_ClipboardLink, tr("Clipboard transport replaced"));
        previous->sessionLease.clear();
        fail(previous, tr("Session transport recovered"));
        ++m_SessionEpoch;
    }
    if (link == m_SessionLink && link->sessionAdmitted) {
        link->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
        QJsonObject response{{"type", DP_MESSAGE_SESSION_STATE}, {"busy", false}, {"admitted", true}};
        if (link->lifecycle) response["resumeToken"] = link->sessionLease;
        send(link, response); return;
    }
    if (takeover) {
        const bool valid = !link->sessionChallenge.isEmpty() &&
            message["challenge"].toString() == link->sessionChallenge &&
            QDateTime::currentMSecsSinceEpoch() <= link->sessionDeadline && link->sessionEpoch == m_SessionEpoch;
        link->sessionChallenge.clear(); // Every attempted confirmation consumes it.
        if (!valid) { sessionError(link, "stale"); return; }
        acquireSession(link, link->sessionSnapshot, true); return;
    }
    m_SessionOperation = true;
    const QPointer<Link> alive(link);
    m_Host->sessionControl({}, this, [this, alive](QJsonObject result) {
        m_SessionOperation = false;
        if (!alive || alive->ended) return;
        auto link = alive.data();
        if (!result["status"].toBool() || result["snapshot"].toString().isEmpty() || !result["sessions"].isDouble()) {
            sessionError(link, "unavailable"); return;
        }
        if (m_SessionLink || m_DisplayLink || m_ClipboardLink || result["sessions"].toInt() > 0 || result["reserved"].toBool()) {
            link->sessionSnapshot = result["snapshot"].toString();
            link->sessionChallenge = QUuid::createUuid().toString(QUuid::WithoutBraces);
            link->sessionDeadline = QDateTime::currentMSecsSinceEpoch() + DP_SESSION_CONFIRMATION_TTL_MS;
            link->sessionEpoch = m_SessionEpoch;
            send(link, {{"type", DP_MESSAGE_SESSION_STATE}, {"busy", true}, {"admitted", false},
                        {"challenge", link->sessionChallenge}, {"expiresInMs", DP_SESSION_CONFIRMATION_TTL_MS}});
        } else acquireSession(link, result["snapshot"].toString(), false);
    });
}
void PeerManager::acquireSession(Link* link, const QString& snapshot, bool takeover) {
    m_SessionOperation = true;
    link->sessionLease = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto lease = link->sessionLease;
    const QPointer<Link> alive(link);
    m_Host->sessionControl({{"action", "acquire"}, {"uuid", trustId(link->fingerprint)},
        {"lease", lease}, {"snapshot", snapshot}, {"takeover", takeover}}, this,
        [this, alive, lease, takeover](QJsonObject result) {
        if (!result["status"].toBool()) {
            m_SessionOperation = false;
            if (alive && !alive->ended) sessionError(alive, result["code"].toString("unavailable"));
            return;
        }
        // The management response means all old streams have stopped and joined.
        // Their input contexts are gone before any new display lease is admitted.
        const bool hadDisplay = m_DisplayLink;
        auto oldSession = m_SessionLink;
        auto oldDisplay = m_DisplayLink;
        auto oldClipboard = m_ClipboardLink;
        const QPointer<ClipboardProcess> oldHelper(oldClipboard ? oldClipboard->clipboardHelper : nullptr);
        for (auto old : {oldSession, oldDisplay, oldClipboard}) {
            if (!old || old->ended || old == alive) continue;
            if (old->sessionOptIn) send(old, {{"type", DP_MESSAGE_SESSION_ENDED}, {"reason", "taken-over"}});
            fail(old, tr("Another device took over this session"));
        }
        auto finish = [this, alive, lease, takeover](bool restored) {
            m_SessionOperation = false;
            const auto peer = alive ? m_Peers.value(alive->fingerprint).toObject() : QJsonObject();
            if (!restored || !alive || alive->ended || !peer["ready"].toBool() || !peer["granted"].toBool() || m_Revoking == alive->fingerprint) {
                m_Host->sessionControl({{"action", "release"}, {"lease", lease}}, this, [](QJsonObject) {});
                if (alive && !alive->ended) sessionError(alive, "unavailable");
                return;
            }
            m_SessionLink = alive; ++m_SessionEpoch;
            alive->sessionAdmitted = true;
            alive->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
            QJsonObject response{{"type", takeover ? DP_MESSAGE_SESSION_RESULT : DP_MESSAGE_SESSION_STATE},
                                 {"busy", false}, {"admitted", true}};
            if (alive->lifecycle) response["resumeToken"] = alive->sessionLease;
            send(alive, response);
            emit changed();
        };
        // Closing a helper's stdin is asynchronous. Do not hand a new client the
        // clipboard while the previous delayed-rendering process can still write.
        auto barrier = new QTimer(this);
        const auto displayDone = std::make_shared<bool>(!hadDisplay);
        const auto displayOkay = std::make_shared<bool>(true);
        if (hadDisplay) m_Host->settleSessionDisplay(barrier, [displayDone, displayOkay](bool ok) {
            *displayDone = true; *displayOkay = ok;
        });
        const auto deadline = QDateTime::currentMSecsSinceEpoch() + 10500;
        connect(barrier, &QTimer::timeout, barrier, [barrier, oldHelper, displayDone, displayOkay, deadline, finish] {
            const bool ready = *displayDone && (!oldHelper || oldHelper->state() == QProcess::NotRunning);
            if (!ready && QDateTime::currentMSecsSinceEpoch() < deadline) return;
            barrier->stop(); finish(ready && *displayOkay); barrier->deleteLater();
        });
        barrier->start(25);
    });
}

void PeerManager::cancel() { if (m_Link) fail(m_Link, tr("Binding cancelled. Review saved access if approval had already completed.")); }
void PeerManager::refreshEndpoints() {
    if (!m_Healthy || busy() || m_RefreshLink || m_Peers.isEmpty()) return;
    const auto keys = m_Peers.keys();
    const auto fp = keys.at(m_RefreshCursor++ % keys.size());
    if (m_RefreshCursor >= keys.size()) m_RefreshCursor = 0;
    const auto peer = m_Peers.value(fp).toObject();
    const auto address = peer["address"].toString();
    const int port = peer["bindingPort"].toInt(48991);
    if (peer["role"].toString() == "client" || !peer["ready"].toBool() || !peer["granted"].toBool() ||
        address.isEmpty() || port < 1 || port > 65535) return;
    auto link = new Link(this); link->socket = new QSslSocket(link);
    link->endpointRefresh = true; link->expectedFingerprint = fp;
    link->expectedPeer = peer; m_RefreshLink = link;
    attach(link);
    SmallTcp::connectAsync(link->socket, address, quint16(port));
    QTimer::singleShot(5000, link, [this, link] {
        if (!link->ended) fail(link, tr("Endpoint refresh timed out"));
    });
}
void PeerManager::restoreHosts() {
    if (!m_Healthy) return;
    for (const auto& peer : m_Peers) if (peer.toObject()["role"].toString() != "client" &&
        peer.toObject()["ready"].toBool() && peer.toObject()["granted"].toBool()) emit peerBound(peer.toObject().toVariantMap());
}
bool PeerManager::editPeer(const QString& fp, const QString& nameValue,
                           const QString& addressValue, int hostPort, int bindingPort) {
    auto reject = [this](const QString& message) {
        m_Status = message; emit changed(); return false;
    };
    if (busy() || m_DisplayLink || m_ClipboardLink) return reject(tr("Finish the current connection before editing this device."));
    if (!m_Peers.contains(fp)) return reject(tr("This saved device no longer exists."));
    if (m_Peers[fp].toObject()["role"].toString() == "client") return reject(tr("Client-only devices have no host endpoint to edit."));
    const auto name = nameValue.trimmed();
    QString address = addressValue.trimmed();
    if (address.startsWith('[') && address.endsWith(']')) address = address.mid(1, address.size() - 2);
    if (name.isEmpty() || name.size() > 64 || name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
        return reject(tr("Enter a device name between 1 and 64 characters."));
    if (dnsName(address).isEmpty() && QHostAddress(address).isNull())
        return reject(tr("Enter a domain name or IP address without a port or URL path."));
    if (hostPort < 1024 || hostPort > 65514 || bindingPort < 1 || bindingPort > 65535)
        return reject(tr("Enter valid host and binding ports."));
    auto peer = m_Peers[fp].toObject();
    peer["name"] = name; peer["customName"] = true;
    peer["address"] = address;
    peer["hostPort"] = hostPort; peer["bindingPort"] = bindingPort;
    peer["requestedAddress"] = (address.contains(':') ? "[" + address + "]" : address) + ":" + QString::number(bindingPort);
    peer.remove("resolvedAddress");
    const auto old = m_Peers;
    m_Peers[fp] = peer;
    if (!save()) { m_Peers = old; return reject(tr("Could not save device information. Please retry.")); }
    if (peer["ready"].toBool()) emit peerBound(peer.toVariantMap());
    m_Status = tr("Device information saved. Reconnect to use the updated address.");
    emit changed(); return true;
}
void PeerManager::revoke(const QString& fp) {
    if (busy() || !m_Peers.contains(fp)) return;
    if (m_ClientOnly) {
        // Forget only this local record. The remote host remains the authority
        // for revocation; never provision or restart a nonexistent local host.
        const auto previous = m_Peers;
        m_Peers.remove(fp);
        if (!save()) {
            m_Peers = previous; m_Status = tr("Could not remove the saved binding; retry");
        } else m_Status = tr("Saved binding removed. Remove the device from Devices separately; revoke access on the host to deny this client's certificate.");
        emit changed(); return;
    }
    if (m_SessionLink && m_SessionLink->fingerprint == fp) fail(m_SessionLink, tr("Device access removed"));
    if (m_ClipboardLink && m_ClipboardLink->fingerprint == fp) fail(m_ClipboardLink, tr("Device access removed"));
    if (m_DisplayLink && m_DisplayLink->fingerprint == fp) fail(m_DisplayLink, tr("Device access removed"));
    m_Revoking = fp; m_TrustInFlight = true;
    m_Host->updatePeerTrust(trustId(fp), QString(), QSslCertificate(), true);
    emit changed();
}
