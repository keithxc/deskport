#pragma once
#include <QObject>
#include <functional>
#include <QVariantList>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslCertificate>
#include <QLockFile>
#include "hostports.h"
#include <QProcess>
#include <QNetworkAccessManager>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>

class HostCaretMonitor;

class HostManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList permissions READ permissions NOTIFY permissionsChanged)
    Q_PROPERTY(bool setupComplete READ setupComplete NOTIFY permissionsChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QUrl applicationUrl READ applicationUrl CONSTANT)
    Q_PROPERTY(int displayScale READ displayScale NOTIFY changed)
    Q_PROPERTY(int displayWidth READ displayWidth NOTIFY changed)
    Q_PROPERTY(int displayHeight READ displayHeight NOTIFY changed)
    Q_PROPERTY(bool virtualDisplayActive READ virtualDisplayActive NOTIFY changed)
    Q_PROPERTY(bool physicalDisplaySharing READ physicalDisplaySharing NOTIFY changed)
    Q_PROPERTY(int sharingWidth READ sharingWidth NOTIFY changed)
    Q_PROPERTY(int sharingHeight READ sharingHeight NOTIFY changed)
    Q_PROPERTY(bool changing READ changing NOTIFY changed)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int basePort READ basePort NOTIFY changed)
    Q_PROPERTY(bool canPair READ canPair NOTIFY changed)
    Q_PROPERTY(bool unattendedEnabled READ unattendedEnabled NOTIFY changed)
    Q_PROPERTY(bool unattendedNeedsApproval READ unattendedNeedsApproval NOTIFY changed)
    Q_PROPERTY(QString unattendedStatus READ unattendedStatus NOTIFY changed)
    Q_PROPERTY(bool loginStart READ loginStart NOTIFY changed)
    Q_PROPERTY(bool loginStartManaged READ loginStartManaged NOTIFY changed)
    Q_PROPERTY(QString readiness READ readiness NOTIFY permissionsChanged)
    Q_PROPERTY(QString displayWarning READ displayWarning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit HostManager(QObject *parent = nullptr, const QString &directory = QString(), bool interactive = true);
    ~HostManager();
    virtual bool available() const;
#ifdef Q_OS_WIN
private:
    void* m_WindowsHostJob = nullptr;
    bool m_WindowsVirtualDisplay = false;
public:
#endif
    QVariantList permissions() const;
    bool setupComplete() const;
    QString deviceName() const;
    bool setDeviceName(const QString& name);
    QUrl applicationUrl() const;
    int displayScale() const { return m_DisplayScale; }
    QJsonArray displayModes() const { return m_DisplayModes; }
    int displayWidth() const { return m_DisplayWidth; }
    int displayHeight() const { return m_DisplayHeight; }
    int sharingWidth() const;
    int sharingHeight() const;
    bool changing() const { return m_Starting || m_Stopping || m_TrustBusy; }
    Q_INVOKABLE void refreshPermissions();
    Q_INVOKABLE qint64 hostProcessId() const { return m_Server.processId(); }
    Q_INVOKABLE void completeSetup();
    Q_INVOKABLE void revealApplication();
    bool prepareIdentity(const QByteArray& certificate, const QByteArray& key);
    QJsonObject identity() const;
    void updatePeerTrust(const QString& id, const QString& name, const QSslCertificate& certificate, bool remove = false);
    bool physicalDisplaySharing() const;
    virtual bool adaptiveDisplayAvailable() const;
    bool virtualDisplayActive() const { return adaptiveDisplayAvailable() && m_DisplayWidth > 0 && m_DisplayHeight > 0; }
    bool displayPoliciesAvailable() const;
    // Empty when accepted (including queued restoration), otherwise a fixed diagnostic code.
    QString displayResizeRejection(int width, int height, int scale, int sequence, int policy = 0) const;
    virtual bool resizeDisplay(int width, int height, int scale, int sequence, int policy = 0);
    void restoreDisplay();
    virtual void settleSessionDisplay(QObject* context, std::function<void(bool)> completion);
    virtual void sessionControl(const QJsonObject& request, QObject* context,
                                std::function<void(QJsonObject)> completion);
    virtual void browserControl(const QJsonObject& request, QObject* context,
                                std::function<void(QJsonObject)> completion);
    virtual bool running() const;
    virtual bool canPair() const;
    // Registered by the browser session supervisor so a native takeover can
    // end its session without the binding code depending on the browser host.
    void setBrowserTakeover(std::function<bool()> busy, std::function<void(std::function<void()>)> end) {
        m_BrowserBusy = std::move(busy); m_EndBrowser = std::move(end);
    }
    bool browserSessionBusy() const { return m_BrowserBusy && m_BrowserBusy(); }
    void endBrowserSession(std::function<void()> done) { if (m_EndBrowser) m_EndBrowser(std::move(done)); else done(); }
    int basePort() const { return m_BasePort; }
    bool loginStart() const;
    bool loginStartManaged() const;
    Q_INVOKABLE void setLoginStart(bool enabled);
    bool unattendedEnabled() const;
    bool unattendedNeedsApproval() const;
    QString unattendedStatus() const;
    Q_INVOKABLE void setUnattended(bool enabled);
    Q_INVOKABLE void openUnattendedSettings();
    Q_INVOKABLE void refreshUnattended();
    QString readiness() const;
    QString status() const { return m_Status; }
    QString displayWarning() const { return m_DisplayWarning; }
    Q_INVOKABLE void start(int width, int height);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pair(const QString &pin, const QString &name);
    Q_INVOKABLE void permission(const QString &kind);
    Q_INVOKABLE void openLogs();
    Q_INVOKABLE void disconnectViewer() { emit disconnectRequested(); }
    Q_INVOKABLE void reconnectViewer() { emit reconnectRequested(); }
    Q_INVOKABLE void toggleViewerFullscreen() { emit fullscreenRequested(); }
    Q_INVOKABLE void recallViewer() { emit viewerRecallRequested(); }
    void setResident(bool enabled) { m_Resident = enabled; }
    Q_INVOKABLE void requestExit();
    Q_INVOKABLE void requestRestart();
    // Called only after the viewer owners have drained; also checks our host.
    void completeExit();
    void updateSessionMenu(const QVariantList& sessions);
    // Mirrors the "hide title bar while viewing" preference in the tray menu.
    void setStreamTitleBarHidden(bool hidden);
    void allowExit() { m_ExitRequested = true; }
    bool restarting() const { return m_RestartRequested; }

signals:
    void openRequested();
    void toggleWindowRequested();
    void showDevicesRequested();
    void viewerRecallRequested();
    void hideRequested();
    void exitRequested();
    void operationRequested(const QString& text);
    void disconnectRequested();
    void reconnectRequested();
    void fullscreenRequested();
    void sessionSelected(QString id);
    void sessionDisconnectRequested(QString id);
    void sessionReconnectRequested(QString id);
    void streamTitleBarToggled(bool hidden);
    // Emitted synchronously before the tray menu opens so it can be refreshed.
    void trayMenuAboutToShow();
    void changed();
    void permissionsChanged();
    void trustUpdated(bool success);
    void caretChanged(const QJsonObject& caret);
    void displayResized(int sequence, int width, int height, const QString& error);
    // The supervisor is replacing its media/display processes, or stopping them
    // explicitly. Notify controllers before Sunshine sends a graceful EOF.
    void sessionHostStopping(bool recoverable);
private:
    void sendTrustUpdate(const QJsonObject& body, qint64 deadline = 0);
    void managementRequest(const QString& path, const QJsonObject& body, QObject* context,
                           std::function<void(QJsonObject)> completion);
    QString unattendedDirectory() const;
    bool unattendedMarker(const QString& name, bool present);
    QString m_UnattendedError;
    int m_UnattendedServiceStatus = 0;
    void updateTrayIcon();
#ifdef Q_OS_MACOS
    void showTrayMenu();
#endif
    void scheduleRecovery();
    bool eventFilter(QObject* watched, QEvent* event) override;
    void setStatus(const QString &value);
    void startServer(int displayId);
    void beginStop(const QString &status);
    void finishStop();
    QString helperPath() const;
    QString serverPath() const;
    QString m_Directory, m_Password, m_Status, m_StopStatus, m_DisplayWarning;
    QProcess m_Display, m_Server, m_Credentials;
    HostCaretMonitor* m_CaretMonitor = nullptr;
    HostPortReservation m_Ports;
    std::unique_ptr<QLockFile> m_HostLock;
    int m_BasePort = DeskPortNetwork::DefaultBasePort;
    std::function<bool()> m_BrowserBusy;
    std::function<void(std::function<void()>)> m_EndBrowser;
    QByteArray m_Buffer;
    QString m_LinuxOutputName;
    bool m_LinuxGnome = false;
    bool m_LinuxGnomePolicies = false;
    bool m_LinuxHyprland = false;
    bool m_LinuxHyprlandPolicies = false;
    bool m_LinuxHyprlandPolicyRequests = false;
    bool m_LinuxX11 = false;
    QString m_LinuxX11Lease;
    quint32 m_LinuxPipewireNode = 0;
    QString m_LinuxPipewireSerial;
    bool saveLinuxDisplayState();
    QJsonArray m_DisplayModes;
    int m_DisplayWireSequence = 0, m_DisplaySequence = 0, m_DisplayWidth = 0, m_DisplayHeight = 0;
    quint64 m_DisplayGeneration = 0;
    QJsonObject m_QueuedDisplayRequest;
    int m_DisplayScale = 1;
    QNetworkAccessManager m_Network;
    QSystemTrayIcon m_Tray;
    QMenu* m_Menu = nullptr;
    bool m_NativeMenuTracking = false;
#ifdef Q_OS_MACOS
    bool m_NativeMenuPending = false;
#endif
    // Connected desktops are listed at the top of the tray menu, in device-list
    // order. Fixed slots are updated in place; actions are never destroyed.
    static constexpr int MaxTraySessions = 7;
    QList<QAction*> m_SessionSlots;
    QAction* m_AllSessions = nullptr;
    QAction* m_SessionSeparator = nullptr;
    QAction* m_DisconnectSession = nullptr;
    QAction* m_ReconnectSession = nullptr;
    QAction* m_StreamTitleBar = nullptr;
    QString m_CurrentSession;
    QVariantList m_PendingSessionRows;
    QString m_SessionMenuKey;
    bool m_SessionMenuRefreshQueued = false;
    bool m_TrustBusy = false;
    bool m_Starting = false;
    bool m_Stopping = false;
    bool m_DisplayClosing = false;
    bool m_Isolated = false;
    bool m_ServerRequested = false;
    bool m_RestartRequested = false;
    quint64 m_Generation = 0;
    QByteArray m_DiagnosticStatusBuffer;
    QTimer m_RecoveryTimer;
    bool m_DesiredSharing = false, m_ShuttingDown = false;
    bool m_Resident = false, m_ExitRequested = false;
    int m_RecoveryAttempt = 0, m_RequestedWidth = 2560, m_RequestedHeight = 1440;
    int m_DisplayFailures = 0;
    bool m_PhysicalFallback = false;

};
