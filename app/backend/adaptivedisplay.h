#pragma once
#include <QSize>
#include <QVector>
#include <QString>
#include <QSslCertificate>
#include <QSslKey>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <memory>
#include <functional>

// A pinned, mutually authenticated control connection. Its lifetime spans video
// renegotiations, so a window resize does not release ownership of the display.
class AdaptiveDisplay : private QThread {
public:
    AdaptiveDisplay(QString address, quint16 port, QSslCertificate peer,
                    QByteArray certificate, QByteArray key, int policy = 0, QString resumeToken = {});
    ~AdaptiveDisplay();
    bool resize(const QSize& pixels, int scale, const std::function<void()>& progress = {},
                const std::function<bool()>& confirmTakeover = {});
    static QSize boundedSize(QSize pixels);
    QSize selectedSize(QSize requested);
    int selectedScale(int requested);
    QSize negotiatedSize();
    bool failed();
    bool takeLeaveFullscreen();
    // Reported with each resize so the host only offers "leave full screen" when it applies.
    void setFullScreen(bool fullScreen);
    bool retryable();
    QString resumeToken();
    QString warning();
    QString topologyError();
    void release();
    // A sleep stop may retain retry permission for an existing admitted token.
    void cancel(bool keepRecovery = false);
    bool wasTakenOver(int timeoutMs = 0);
    bool admissionRequired();
    void setVideoPaused(bool paused);
    // Hello enables the protocol; a rejected backend stays fixed for this controller.
    bool liveRateSupported();
    void beginRateStream(int initialKbps, bool saver);
    void markRateStreamReady();
    // saver tells the host that data saver is on (optional field of the contract).
    void setRateCeiling(int kbps, bool saver);
    // True once if the host recorded a ceiling but cannot apply it live; the
    // viewer then applies it by reconnecting.
    bool takeRateFallback();
private:
    void run() override;
    QString m_Address;
    quint16 m_Port;
    QSslCertificate m_Peer;
    QByteArray m_Certificate, m_Key;
    QMutex m_Mutex;
    QWaitCondition m_Wake;
    QSize m_Size, m_NegotiatedSize;
    QVector<QSize> m_Modes;
    int m_Scale = 1;
    int m_Policy = 0;
    bool m_TakenOver = false;
    bool m_LeaveFullscreen = false, m_FullScreen = false;
    bool m_Retryable = true, m_Release = true, m_Lifecycle = false;
    QString m_ResumeToken, m_Warning;
    QString m_GraphIdentity, m_GraphToken, m_TopologyError;
    bool m_GraphReserved = false;
    bool m_AdmissionRequired = true, m_ConfirmationNeeded = false, m_ConfirmationReady = false, m_Confirmed = false;
    bool m_VideoPaused = false;
    bool m_RateSupported = false, m_RateFallback = false, m_RateReady = false;
    quint64 m_RateGeneration = 0;
    int m_RateCeiling = 0;
    bool m_RateSaver = false;
    bool m_Pending = false, m_Complete = false, m_Result = false, m_Failed = false;
};
