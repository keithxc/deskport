#pragma once
#include <QSize>
#include <QString>
#include <QSslCertificate>
#include <QSslKey>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <memory>

// A pinned, mutually authenticated control connection. Its lifetime spans video
// renegotiations, so a window resize does not release ownership of the display.
class AdaptiveDisplay : private QThread {
public:
    AdaptiveDisplay(QString address, quint16 port, QSslCertificate peer,
                    QByteArray certificate, QByteArray key);
    ~AdaptiveDisplay();
    bool resize(const QSize& pixels, int scale);
    static QSize boundedSize(QSize pixels);
private:
    void run() override;
    QString m_Address;
    quint16 m_Port;
    QSslCertificate m_Peer;
    QByteArray m_Certificate, m_Key;
    QMutex m_Mutex;
    QWaitCondition m_Wake;
    QSize m_Size;
    int m_Scale = 1;
    bool m_Pending = false, m_Complete = false, m_Result = false, m_Failed = false;
};
