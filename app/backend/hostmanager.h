#pragma once
#include <QObject>
#include <QVariantList>
#include <QUrl>
#include <QJsonObject>
#include <QSslCertificate>
#include <QLockFile>
#include "hostports.h"
#include <QProcess>
#include <QNetworkAccessManager>
#include <QSystemTrayIcon>
#include <QTimer>

class HostManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList permissions READ permissions NOTIFY permissionsChanged)
    Q_PROPERTY(bool setupComplete READ setupComplete NOTIFY permissionsChanged)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(QUrl applicationUrl READ applicationUrl CONSTANT)
    Q_PROPERTY(int displayScale READ displayScale NOTIFY changed)
    Q_PROPERTY(int displayWidth READ displayWidth NOTIFY changed)
    Q_PROPERTY(int displayHeight READ displayHeight NOTIFY changed)
    Q_PROPERTY(bool virtualDisplayActive READ adaptiveDisplayAvailable NOTIFY changed)
    Q_PROPERTY(int sharingWidth READ sharingWidth NOTIFY changed)
    Q_PROPERTY(int sharingHeight READ sharingHeight NOTIFY changed)
    Q_PROPERTY(bool changing READ changing NOTIFY changed)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int basePort READ basePort NOTIFY changed)
    Q_PROPERTY(bool canPair READ canPair NOTIFY changed)
    Q_PROPERTY(bool streamAudio READ streamAudio WRITE setStreamAudio NOTIFY changed)
    Q_PROPERTY(bool loginStart READ loginStart NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit HostManager(QObject *parent = nullptr, const QString &directory = QString());
    ~HostManager();
    bool available() const;
    QVariantList permissions() const;
    bool setupComplete() const;
    QString deviceName() const;
    QUrl applicationUrl() const;
    int displayScale() const { return m_DisplayScale; }
    int displayWidth() const { return m_DisplayWidth; }
    int displayHeight() const { return m_DisplayHeight; }
    int sharingWidth() const;
    int sharingHeight() const;
    bool changing() const { return m_Starting || m_Stopping || m_TrustBusy; }
    Q_INVOKABLE void refreshPermissions();
    Q_INVOKABLE void completeSetup();
    Q_INVOKABLE void revealApplication();
    bool prepareIdentity(const QByteArray& certificate, const QByteArray& key);
    QJsonObject identity() const;
    void updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove = false);
    bool adaptiveDisplayAvailable() const;
    bool resizeDisplay(int width, int height, int scale, int sequence);
    void restoreDisplay();
    bool running() const;
    bool canPair() const;
    int basePort() const { return m_BasePort; }
    bool streamAudio() const;
    void setStreamAudio(bool enabled);
    bool loginStart() const;
    Q_INVOKABLE void setLoginStart(bool enabled);
    QString status() const { return m_Status; }
    Q_INVOKABLE void start(int width, int height);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pair(const QString &pin, const QString &name);
    Q_INVOKABLE void permission(const QString &kind);
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void recallViewer() { emit viewerRecallRequested(); }
    void setResident(bool enabled) { m_Resident = enabled; }
    Q_INVOKABLE void requestExit();
    void allowExit() { m_ExitRequested = true; }

signals:
    void openRequested();
    void showDevicesRequested();
    void viewerRecallRequested();
    void hideRequested();
    void exitRequested();
    void disconnectRequested();
    void changed();
    void permissionsChanged();
    void trustUpdated(bool success);
    void displayResized(int sequence, int width, int height, const QString& error);
private:
    void updateTrayIcon();
    void scheduleRecovery();
    bool eventFilter(QObject* watched, QEvent* event) override;
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
    int m_DisplayWireSequence = 0, m_DisplaySequence = 0, m_DisplayWidth = 0, m_DisplayHeight = 0;
    quint64 m_DisplayGeneration = 0;
    int m_DisplayScale = 1;
    QNetworkAccessManager m_Network;
    QSystemTrayIcon m_Tray;
    bool m_TrustBusy = false;
    bool m_Starting = false;
    bool m_Stopping = false;
    bool m_Isolated = false;
    bool m_ServerRequested = false;
    quint64 m_Generation = 0;
    qint64 m_LogOffset = 0;
    QTimer m_RecoveryTimer;
    bool m_DesiredSharing = false, m_ShuttingDown = false;
    bool m_Resident = false, m_ExitRequested = false;
    int m_RecoveryAttempt = 0, m_RequestedWidth = 2560, m_RequestedHeight = 1440;

};
