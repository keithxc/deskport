#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>

// Holds a logind delay inhibitor so an active stream can release remote input
// and stop its transport before the client suspends or hibernates.
class SleepMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SleepMonitor(QObject* parent = nullptr);
    // Allow a pending sleep to continue. Reacquired after resume.
    void release();

signals:
    void sleeping();
    void resumed();

private slots:
    void prepareForSleep(bool start);

private:
    void acquire();
    QDBusUnixFileDescriptor m_Inhibitor;
};
