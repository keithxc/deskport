#pragma once

#include <QtGlobal>
#ifdef Q_OS_LINUX
#include <QDBusUnixFileDescriptor>
#endif
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <atomic>

// Normalizes native client sleep/wake events. Linux also holds a logind delay
// inhibitor while active streams release input and stop transport.
class SleepMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SleepMonitor(QObject* parent = nullptr, bool native = true);
    ~SleepMonitor();
    static bool asleep() { return s_Asleep.load(); }
    // Allow a pending sleep to continue. Reacquired after resume.
    void release();

signals:
    void sleeping();
    void resumed();

public slots:
    // Normalized native event boundary, also used by isolated tests.
    void prepareForSleep(bool start);

private:
    void acquire();
#ifdef Q_OS_LINUX
    QDBusUnixFileDescriptor m_Inhibitor;
#endif
    void* m_Native = nullptr;
    bool m_Subscribe = false;
    static std::atomic<bool> s_Asleep;
};

// Normalizes native lock/session-inactive state. Linux follows logind's
// graphical Display session rather than "auto" because the GUI may be a user
// service outside the session. Windows/macOS own their native subscriptions.
class ScreenLockMonitor : public QObject
{
    Q_OBJECT
public:
    explicit ScreenLockMonitor(QObject* parent = nullptr, bool native = true);
    ~ScreenLockMonitor();
    void setLocked(bool locked);
    static bool locked() { return s_Locked.load(); }

signals:
    void changed(bool locked);
private slots:
    void propertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated);

private:
    void refresh();
    QString m_Session;
    void* m_Native = nullptr;
    static std::atomic<bool> s_Locked;
};
