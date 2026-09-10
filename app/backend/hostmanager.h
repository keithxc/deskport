#pragma once
#include <QObject>
#include <QJsonObject>
#include <QSslCertificate>
#include <QLockFile>
#include "hostports.h"
#include <QProcess>
#include <QNetworkAccessManager>
#include <QSystemTrayIcon>

class HostManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int basePort READ basePort NOTIFY changed)
    Q_PROPERTY(bool canPair READ canPair NOTIFY changed)
    Q_PROPERTY(bool loginStart READ loginStart NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit HostManager(QObject *parent = nullptr, const QString &directory = QString());
    ~HostManager();
    bool available() const;
    bool prepareIdentity(const QByteArray& certificate, const QByteArray& key);
    QJsonObject identity() const;
    void updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove = false);
    bool running() const;
    bool canPair() const;
    int basePort() const { return m_BasePort; }
    bool loginStart() const;
    Q_INVOKABLE void setLoginStart(bool enabled);
    QString status() const { return m_Status; }
    Q_INVOKABLE void start(int width, int height);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pair(const QString &pin, const QString &name);
    Q_INVOKABLE void permission(const QString &kind);
    Q_INVOKABLE void openLogs();
signals:
    void changed();
    void trustUpdated(bool success);
private:
    void setStatus(const QString &value);
    void startServer(int displayId);
    void beginStop(const QString &status);
    void finishStop();
    QString helperPath() const;
    QString serverPath() const;
    QString m_Directory, m_Password, m_Status, m_StopStatus;
    QProcess m_Display, m_Server, m_Credentials;
    HostPortReservation m_Ports;
    std::unique_ptr<QLockFile> m_HostLock;
    int m_BasePort = DeskPortNetwork::DefaultBasePort;
    QByteArray m_Buffer;
    QNetworkAccessManager m_Network;
    QSystemTrayIcon m_Tray;
    bool m_TrustBusy = false;
    bool m_Starting = false;
    bool m_Stopping = false;
    bool m_Isolated = false;
    bool m_ServerRequested = false;
    quint64 m_Generation = 0;
    qint64 m_LogOffset = 0;
};
