#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QHash>
#include <QSize>
#include <QTimer>

class SeamlessHostManager : public QObject {
    Q_OBJECT
public:
    explicit SeamlessHostManager(QObject* parent = nullptr);
    ~SeamlessHostManager() override;

    static QString binaryPath();
    bool available() const;
    bool running() const;
    bool ready() const { return m_Ready; }
    QString status() const { return m_Status; }
    QString socketName() const { return m_SocketName; }
    int windowCount() const { return m_Windows.size(); }
    // Detached, bounded local diagnostics in ascending session window-ID order.
    // Read on the manager's thread; accepted events update this before eventReceived.
    QJsonArray windowStates() const;
    QJsonObject localStatus() const;
    qint64 processId() const { return m_Process.processId(); }

    bool start(const QStringList& application = {}, const QSize& size = QSize(1280, 720));
    void stop();

signals:
    void changed();
    void eventReceived(const QJsonObject& event);
    void stopped();

private:
    void consumeOutput();
    bool acceptEvent(const QJsonObject& event);
    void beginStop(bool requested);
    void failAndStop(const QString& status);
    void setStatus(const QString& status);
    void finishStop();

    QProcess m_Process;
    QTimer m_StartupTimer;
    QTimer m_KillTimer;
    QByteArray m_Output;
    QString m_Status;
    QString m_TerminalStatus;
    QString m_SocketName;
    bool m_Active = false;
    bool m_Finishing = false;
    quint64 m_RunId = 0;
    bool m_Ready = false;
    bool m_Stopping = false;
    bool m_StopRequested = false;
    bool m_DiscardingLine = false;
    struct WindowState {
        QString title;
        QString appId;
        QSize initialSize;
        QJsonObject lastFrame;
        QString captureError;
        QString captureFileMessage;
        quint64 lastSequence = 0;
        quint64 pendingSequence = 0;
        bool captureFileError = false;
    };
    QHash<int, WindowState> m_Windows;
    int m_LastWindowId = 0;
};
