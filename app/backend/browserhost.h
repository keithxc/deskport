#pragma once

#include "browsergateway.h"
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <QVariantList>
#include <functional>

class HostManager;

// Coordinates a browser transport with the same host/display admission boundary
// as native sessions. The HTTPS gateway never receives host management secrets.
class BrowserHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accessCode READ accessCode NOTIFY changed)
    Q_PROPERTY(qint64 accessCodeExpiresAt READ accessCodeExpiresAt NOTIFY changed)
    Q_PROPERTY(qint64 codeLockedUntil READ codeLockedUntil NOTIFY changed)
    Q_PROPERTY(QStringList urls READ urls NOTIFY changed)
    Q_PROPERTY(QString qrSource READ qrSource NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(bool tailnetListening READ tailnetListening NOTIFY changed)
    Q_PROPERTY(bool tailnetIdentity READ tailnetIdentity NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantList pairedBrowsers READ pairedBrowsers NOTIFY changed)
public:
    using Completion = std::function<void(QJsonObject)>;
    explicit BrowserHost(HostManager* host, QObject* parent = nullptr,
                         const QString& directory = QString());
    ~BrowserHost();
    bool start();
    void stop();
    QString accessCode() const;
    qint64 accessCodeExpiresAt() const;
    qint64 codeLockedUntil() const;
    // Exports the shared secret for an authenticator app; local UI/CLI only.
    Q_INVOKABLE QString authenticatorUri() const;
    Q_INVOKABLE QString authenticatorQr() const;
    Q_INVOKABLE bool resetAuthenticator();
    QStringList urls() const;
    QString qrSource() const;
    QString errorString() const;
    bool listening() const;
    bool tailnetListening() const;
    bool tailnetIdentity() const;
    bool busy() const { return !m_Id.isEmpty() || m_Operation; }
    // Ends the browser session, display restoration included, so a native
    // client can take over; the page is told it was taken over.
    void endForTakeover(Completion done);
    QJsonObject localInfo() const;
    QVariantList pairedBrowsers() const;
    Q_INVOKABLE bool revokeBrowser(const QString& id);
signals:
    void changed();
private:
    friend class BackendTimerTests;
    friend class BrowserHostTests;
    QJsonObject state() const;
    static QString qrSvg(const QString& value);
    void request(const QJsonObject& body, QObject* context, Completion completion);
    void begin(const QJsonObject& body, Completion completion);
    void beginMedia(QJsonObject body, quint64 epoch, Completion completion);
    void end(Completion completion = {});
    void resize(const QJsonObject& body, Completion completion);
    void dropResize() {
        auto pending = std::move(m_Resize.done); m_Resize = {};
        if (pending) pending(QJsonObject{{"version", 1}, {"status", false}, {"code", "cancelled"}});
    }
    void poll();
    void updatePollTimer();
    HostManager* m_Host;
    BrowserGateway m_Gateway;
    QString m_Id;
    // The session a native client took over, until the page learns of it.
    QString m_TakenOver;
    quint64 m_Epoch = 0;
    bool m_Operation = false, m_Ending = false, m_Polling = false;
    bool m_Cancelled = false;
    bool m_DisplayOwned = false, m_Listening = false;
    int m_Sequence = 1000000;
    int m_Policy = 0;
    bool m_Resizing = false;
    struct PendingResize { QJsonObject body; Completion done; } m_Resize;
    QTimer m_Poll;
    QVector<Completion> m_EndWaiters;
};
