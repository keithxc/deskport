#pragma once
#include <QThread>
#include <QMutex>
#include <QSslCertificate>
#include <QSslKey>
#include <QJsonObject>

// Transport owns no platform clipboard. The SDL/main thread exchanges bounded
// snapshots with it; only one request can be outstanding and nothing is persisted.
class ClipboardChannel : private QThread {
public:
    ClipboardChannel(QString address, quint16 port, QSslCertificate peer, QByteArray cert, QByteArray key);
    ~ClipboardChannel();
    bool ready();
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
    bool m_Ready = false, m_Busy = false;
};
