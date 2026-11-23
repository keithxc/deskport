#pragma once

#include <QObject>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslCertificate>
#include <QSslKey>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <functional>

class QTcpServer;

// A deliberately small, same-origin HTTPS adapter. Host/session policy stays in
// the host adapter; this class owns web credentials and transport admission only.
class BrowserGateway : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY changed)
    Q_PROPERTY(qint64 accessCodeExpiresAt READ accessCodeExpiresAt NOTIFY changed)
    Q_PROPERTY(qint64 codeLockedUntil READ codeLockedUntil NOTIFY changed)
    Q_PROPERTY(QStringList urls READ urls NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    struct Options {
        QString stateDirectory;
        quint16 port = 48992;
        QList<QHostAddress> listenAddresses;
        QStringList allowedHosts;
        QString certificatePath;
        QString privateKeyPath;
        int sessionIdleSeconds = 900;
        int mediaIdleSeconds = 20;
        // Devices owned by this computer's Tailscale user may sign in without
        // the access code, but only over a tailnet listener.
        bool tailnetIdentity = true;
        QString tailscaleProgram;
        bool testLoopbackTailnet = false; // Isolated tests only: treat loopback peers as tailnet peers.
        // Wall-clock seconds for the time-based access code; tests inject a clock.
        std::function<qint64()> clock;
        int codeFailureLimit = 10; // Wrong codes within 15 minutes before code sign-in pauses.
    };
    struct Hooks {
        std::function<QJsonObject()> state;
        std::function<void(const QJsonObject&, QObject*, std::function<void(QJsonObject)>)> request;
        // Optional override of the local Tailscale ownership check (tests).
        std::function<void(const QHostAddress&, std::function<void(bool)>)> tailnetOwner;
    };
    explicit BrowserGateway(const Hooks& hooks, QObject* parent = nullptr);
    BrowserGateway(const Hooks& hooks, const Options& options, QObject* parent = nullptr);
    ~BrowserGateway() override;
    bool start();
    void stop();
    bool active() const { return !m_Listeners.isEmpty(); }
    bool tailnetIdentity() const { return m_Options.tailnetIdentity; }
    bool tailnetListening() const;
    // The current 30-second code (RFC 6238, six digits). Local UI/CLI only,
    // never an HTTP response; authenticatorUri() exports the shared secret.
    QString accessCode() const;
    qint64 accessCodeExpiresAt() const; // Milliseconds since the epoch.
    QString authenticatorUri(const QString& account) const;
    qint64 codeLockedUntil() const; // Milliseconds since the epoch; 0 when code sign-in is open.
    static QString totp(const QByteArray& secret, qint64 step);
    static constexpr qint64 RememberSeconds = 7LL * 24 * 60 * 60;
    QStringList urls() const { return m_Urls; }
    QString errorString() const { return m_Error; }
    int port() const { return m_Port; }
    Q_INVOKABLE bool resetAccessCode();
    QJsonArray pairedBrowsers() const;
    bool revokeBrowser(const QString& id);
    void hostStateChanged();
    // A Tailscale/Headscale address: RFC 6598 space on a tunnel, never on a
    // physical Wi-Fi/Ethernet link where the same range means carrier NAT.
    static bool overlayAddress(const QHostAddress& address, const QString& interfaceName,
                               bool pointToPoint, bool physical);
signals:
    void changed();
private:
    friend class BackendTimerTests;
    struct Connection;
    struct Session {
        QString id, csrf, pairingId;
        qint64 created = 0, lastSeen = 0, lastHeartbeat = 0;
        quint64 generation = 0;
        bool media = false, pending = false;
    };
    struct Rate {
        double credits = 5.0;
        qint64 updated = 0;
    };
    struct Pairing {
        QString id, name, hash;
        qint64 createdAt = 0, lastSeenAt = 0, expiresAt = 0;
    };
    void accept(qintptr descriptor);
    void receive(Connection* connection);
    void dispatch(Connection* connection);
    void respond(Connection* connection, int status, const QByteArray& body,
                 const QByteArray& contentType = "application/json", const QList<QByteArray>& cookies = {});
    void error(Connection* connection, int status, const QString& code, const QString& message,
               const QList<QByteArray>& cookies = {});
    void createSession(Connection* connection, const QString& pairingId, const QList<QByteArray>& cookies = {}, bool reuse = false);
    bool sessionAvailable(Connection* connection);
    bool loadPairings();
    bool savePairings(const QHash<QString, Pairing>& pairings);
    QString pairingFor(Connection* connection) const;
    void releasePairing(const QString& id);
    void forward(Connection* connection, const QString& key, const QString& action, QJsonObject body);
    void release(const QString& key);
    void cleanup();
    void updateCleanupTimer();
    bool loadCredentials(const QList<QHostAddress>& addresses);
    bool loadCertificate();
    bool addListener(const QHostAddress& address);
    void refreshAddresses();
    bool tailnetEligible(Connection* connection) const;
    void tailnetOwner(const QHostAddress& peer, std::function<void(bool)> done);
    bool saveSecret(const QByteArray& secret);
    bool acceptCode(const QByteArray& code);
    qint64 wallSeconds() const;
    void scheduleCodeTick();
    bool permittedHost(const QByteArray& host) const;
    bool loginAllowed(const QString& address);
    QString authenticated(Connection* connection) const;
    Hooks m_Hooks;
    Options m_Options;
    QList<QTcpServer*> m_Listeners;
    QSet<Connection*> m_Connections;
    QHash<QString, Session> m_Sessions;
    QHash<QString, Pairing> m_Pairings;
    QHash<QString, Rate> m_Rates;
    Rate m_GlobalRate;
    QList<QSslCertificate> m_Certificates;
    QSslKey m_PrivateKey;
    QTimer m_Cleanup;
    QTimer m_AddressRefresh;
    QString m_Directory, m_Error;
    QByteArray m_Secret;
    qint64 m_LastStep = -1; // A code is accepted once; replay of an older step is refused.
    QList<qint64> m_CodeFailures; // Monotonic milliseconds of recent wrong codes.
    qint64 m_LockedUntil = 0; // Monotonic milliseconds.
    QTimer m_CodeTick;
    QStringList m_Urls, m_Hosts;
    int m_Port = 0;
    static constexpr int MaxCertificateNames = 32;
};
