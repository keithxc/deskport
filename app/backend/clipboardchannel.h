#pragma once
#include <QThread>
#include <QMutex>
#include <QSslCertificate>
#include <QSslKey>
#include <QJsonObject>
#include <atomic>
#include <functional>

class QEventLoop;

// Transport owns no platform clipboard. The SDL/main thread exchanges bounded
// snapshots with it; only one request can be outstanding and nothing is persisted.
class ClipboardChannel : private QThread {
public:
    ClipboardChannel(QString address, quint16 port, QSslCertificate peer, QByteArray cert, QByteArray key, bool nativeSharing = false);
    ~ClipboardChannel();
    bool ready();
    bool nativeSharing();
    QString notice();
    int maxText();
    bool submit(const QJsonObject& request);
    bool take(QJsonObject& reply);
    QString error();
private:
    void run() override;
    QString m_Address, m_Error;
    quint16 m_Port;
    QSslCertificate m_Peer;
    QByteArray m_Cert, m_Key;
    QMutex m_Mutex;
    QJsonObject m_Request, m_Reply;
    bool m_NativeRequested = false, m_NativeActive = false;
    QString m_Notice;
    bool m_Ready = false, m_Busy = false;
    int m_MaxText = 1024 * 1024;
    // Unlike QThread interruption, this also latches a stop before run starts.
    std::atomic<bool> m_Stopping{false};
    // Published/retired under m_Mutex, before their worker-thread owners die.
    QEventLoop* m_Loop = nullptr;
    QObject* m_RequestTarget = nullptr;
    std::function<void()> m_RequestWake;
};
