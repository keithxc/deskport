#pragma once
#include <QObject>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QSystemTrayIcon>

class HostManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool loginStart READ loginStart NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit HostManager(QObject *parent = nullptr, const QString &directory = QString());
    ~HostManager();
    bool available() const;
    bool running() const;
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
private:
    void setStatus(const QString &value);
    void startServer(int displayId);
    QString helperPath() const;
    QString serverPath() const;
    QString m_Directory, m_Password, m_Status;
    QProcess m_Display, m_Server;
    QByteArray m_Buffer;
    QNetworkAccessManager m_Network;
    QSystemTrayIcon m_Tray;
    bool m_Starting = false;
    bool m_Stopping = false;
    bool m_Isolated = false;
    quint64 m_Generation = 0;
    qint64 m_LogOffset = 0;
};
