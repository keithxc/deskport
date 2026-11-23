#pragma once
#include <QWindow>
#include <QLocalSocket>
#include <QPointer>
#include <QJsonObject>
class Session;
class ComputerManager;
// A deliberately minimal, hidden Qt window provides screen geometry to SDL.
// Returning to Devices is IPC to the shell, never loading QML in this process.
class SessionWorker : public QWindow {
    Q_OBJECT
public:
    SessionWorker(QString endpoint, QString token, QString host, QString app);
    ~SessionWorker() override;
    int run();
    Q_INVOKABLE void showDevicesDuringSession();
    Q_INVOKABLE void prepareViewerRecall();
    Q_INVOKABLE void showNextSession();
    Q_INVOKABLE void requestShortcutToggle();
private:
    void attach(Session* session);
    void command(const QJsonObject& message);
    void send(QJsonObject message);
    void finish(const QString& error = {});
    void sendCanvasDescription();
    QLocalSocket m_Socket;
    QByteArray m_Buffer;
    QString m_Endpoint, m_Token, m_Host, m_App;
    QPointer<Session> m_Session;
    ComputerManager* m_Computers = nullptr;
    bool m_Visible = false, m_Stopping = false, m_Finished = false;
    bool m_Sleeping = false, m_Locked = false;
    // A saved tuning value that arrived between sessions.
    double m_PendingTuning = 0;
    quint64 m_HideEpoch = 0, m_InputEpoch = 0;
    QJsonObject m_LastCanvas;
    bool m_CanvasSent = false;
    bool m_CanvasDirty = true;
};
