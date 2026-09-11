#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QSslCertificate>
#include <QSslKey>
#include <QTcpServer>
#include "hostmanager.h"

class PeerManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString pendingName READ pendingName NOTIFY changed)
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QVariantList peers READ peers NOTIFY changed)
public:
    PeerManager(HostManager* host, const QByteArray& cert, const QByteArray& key,
                const QString& directory = QString(), quint16 port = 48991,
                const QHostAddress& listenAddress = QHostAddress::AnyIPv4);
    ~PeerManager();
    QString status() const { return m_Status; }
    QString pendingName() const;
    QString requestId() const;
    bool busy() const;
    int port() const { return m_Server->serverPort(); }
    QVariantList peers() const;
    Q_INVOKABLE void request(const QString& address);
    Q_INVOKABLE void approve(const QString& transaction);
    Q_INVOKABLE void reject(const QString& transaction);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void restoreHosts();
    Q_INVOKABLE void revoke(const QString& fingerprint);
    Q_INVOKABLE bool editPeer(const QString& fingerprint, const QString& name,
                              const QString& address, int hostPort, int bindingPort);
signals:
    void changed();
    void incomingRequest();
    void peerBound(QVariantMap peer);
private:
    struct Link;
    void attach(Link* link);
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
    HostManager* m_Host;
    QTcpServer* m_Server;
    QSslCertificate m_Certificate;
    QSslKey m_Key;
    QString m_Path, m_Status, m_Revoking;
    QJsonObject m_Peers;
    Link* m_Link = nullptr;
    Link* m_DisplayLink = nullptr;
    bool m_Healthy = false;
    bool m_TrustInFlight = false;
};
