#pragma once
#include <QSet>
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>
#include "hostmanager.h"

class PeerManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canReleaseClientFullscreen READ canReleaseClientFullscreen NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool clientOnly READ clientOnly CONSTANT)
    Q_PROPERTY(bool pendingClientOnly READ pendingClientOnly NOTIFY changed)
    Q_PROPERTY(QString pendingName READ pendingName NOTIFY changed)
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY changed)
public:
    enum class Mode { PlatformDefault, ClientOnly };
    PeerManager(HostManager* host, const QByteArray& cert, const QByteArray& key,
                const QString& directory = QString(), quint16 port = 48991,
                const QHostAddress& listenAddress = QHostAddress::AnyIPv4,
                Mode mode = Mode::PlatformDefault);
    ~PeerManager();
    bool canReleaseClientFullscreen() const;
    Q_INVOKABLE void releaseClientFullscreen();
    bool clientOnly() const { return m_ClientOnly; }
    QString status() const { return m_Status; }
    QString pendingName() const;
    bool pendingClientOnly() const;
    QString requestId() const;
    bool busy() const;
    int port() const { return m_Server->serverPort(); }
    QVariantList peers() const;
    Q_INVOKABLE bool setConnectionPort(int port);
    Q_INVOKABLE void request(const QString& address);
    Q_INVOKABLE void approve(const QString& transaction);
    Q_INVOKABLE void reject(const QString& transaction);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void restoreHosts();
    Q_INVOKABLE void refreshEndpoints();
    Q_INVOKABLE void revoke(const QString& fingerprint);
    Q_INVOKABLE bool editPeer(const QString& fingerprint, const QString& name,
                              const QString& address, int hostPort, int bindingPort);
signals:
    void changed();
    void incomingRequest();
    void peerBound(QVariantMap peer);
protected:
    virtual qint64 nativeClipboardRevision() const;
private:
    struct Link;
    void attach(Link* link);
    void transportLost(Link* link);
    QTcpServer* createListener();
    void drain(Link* link);
    void receive(Link* link, const QJsonObject& message);
    void send(Link* link, const QJsonObject& message);
    void fail(Link* link, const QString& message);
    bool acceptMetadata(Link* link, const QJsonObject& metadata);
    void grant(Link* link);
    void granted(bool success);
    void finish(Link* link);
    QJsonObject metadata() const;
    bool save();
    void sessionRequest(Link* link, const QJsonObject& message);
    void sessionRequestVerified(Link* link, const QJsonObject& message);
    int m_TopologyOperations = 0;
    void acquireSession(Link* link, const QString& snapshot, bool takeover);
    void sessionError(Link* link, const QString& code);
    HostManager* m_Host;
    QTcpServer* m_Server;
    QList<QTcpServer*> m_PreviousServers;
    QHostAddress m_ListenAddress;
    bool m_Persistent;
    const bool m_ClientOnly;
    QSslCertificate m_Certificate;
    QSslKey m_Key;
    QString m_Path, m_Status, m_Revoking;
    QJsonObject m_Peers;
    Link* m_Link = nullptr;
    Link* m_RefreshLink = nullptr;
    int m_RefreshCursor = 0;
    Link* m_DisplayLink = nullptr;
    Link* m_SessionLink = nullptr;
    bool m_SessionOperation = false;
    quint64 m_SessionEpoch = 0;
    Link* m_ClipboardLink = nullptr;
    QSet<Link*> m_IncomingLinks;
    bool m_Healthy = false;
    bool m_IdentityHealthy = false;
    bool m_TrustInFlight = false;
};
