#pragma once

#include "nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "settings/compatfetcher.h"

#include <qmdnsengine/server.h>
#include <qmdnsengine/cache.h>
#include <qmdnsengine/browser.h>
#include <qmdnsengine/service.h>
#include <qmdnsengine/resolver.h>

#include <QThread>
#include <QReadWriteLock>
#include <QSettings>
#include <QRunnable>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>

class ComputerManager;

class DelayedFlushThread : public QThread
{
    Q_OBJECT

public:
    DelayedFlushThread(ComputerManager* cm)
        : m_ComputerManager(cm)
    {
        setObjectName("CM Delayed Flush Thread");
    }

    void run();

private:
    ComputerManager* m_ComputerManager;
};

class MdnsPendingComputer : public QObject
{
    Q_OBJECT

public:
    explicit MdnsPendingComputer(const QSharedPointer<QMdnsEngine::AbstractServer> server,
                                 const QMdnsEngine::Service& service)
        : m_Hostname(service.hostname()),
          m_Port(service.port()),
          m_ServerWeak(server),
          m_Resolver(nullptr)
    {
        m_Timeout.setSingleShot(true);
        connect(&m_Timeout, &QTimer::timeout, this, &MdnsPendingComputer::handleResolvedTimeout);
        // Start resolving
        resolve();
    }

    virtual ~MdnsPendingComputer()
    {
        delete m_Resolver;
    }

    QString hostname()
    {
        return m_Hostname;
    }

    uint16_t port()
    {
        return m_Port;
    }

private slots:
    void handleResolvedTimeout()
    {
        if (m_Completed) {
            return;
        }

        if (m_Addresses.isEmpty()) {
            if (m_Server && m_Retries-- > 0) {
                // Try again
                qInfo() << "Resolving" << hostname() << "timed out. Retrying...";
                resolve();
            }
            else {
                qWarning() << "Giving up on resolving" << hostname() << "after repeated failures";
                complete();
            }
        }
        else {
            complete();
        }
    }

    void handleResolvedAddress(const QHostAddress& address)
    {
        if (m_Completed) {
            return;
        }
        qInfo() << "Resolved" << hostname() << "to" << address;
        m_Addresses.push_back(address);
    }

signals:
    void resolvedHost(MdnsPendingComputer*,QVector<QHostAddress>&);

private:
    void complete()
    {
        // Completion is synchronous. The receiver removes us from its pending
        // list and uses deleteLater(), keeping the signal's address reference valid.
        m_Completed = true;
        cleanup();
        emit resolvedHost(this, m_Addresses);
    }

    void cleanup()
    {
        m_Timeout.stop();
        ++m_ResolutionGeneration;
        // Delete our resolver, so we're guaranteed that nothing is referencing m_Server.
        delete m_Resolver;
        m_Resolver = nullptr;

        // Now delete our strong reference that we held on behalf of m_Resolver.
        // The server may be destroyed after we make this call.
        m_Server.reset();
    }

    void resolve()
    {
        // Clean up any existing resolver object and server references
        cleanup();

        // Re-acquire a strong reference if the server still exists.
        m_Server = m_ServerWeak.toStrongRef();
        // Even a vanished server must complete after the caller has connected
        // the signal and registered this object in its pending list.
        m_Timeout.start(m_Server ? 2000 : 0);
        if (!m_Server) {
            return;
        }

        m_Resolver = new QMdnsEngine::Resolver(m_Server.data(), m_Hostname);
        const auto generation = m_ResolutionGeneration;
        connect(m_Resolver, &QMdnsEngine::Resolver::resolved,
                this, [this, generation](const QHostAddress& address) {
            // Disconnecting a resolver does not cancel already queued signals.
            if (generation == m_ResolutionGeneration) {
                handleResolvedAddress(address);
            }
        });
    }

    QByteArray m_Hostname;
    uint16_t m_Port;
    QWeakPointer<QMdnsEngine::AbstractServer> m_ServerWeak;
    QSharedPointer<QMdnsEngine::AbstractServer> m_Server;
    QMdnsEngine::Resolver* m_Resolver;
    QVector<QHostAddress> m_Addresses;
    int m_Retries = 10;
    QTimer m_Timeout;
    quint64 m_ResolutionGeneration = 0;
    bool m_Completed = false;
};

class PcMonitorThread : public QThread
{
    Q_OBJECT

public:
    explicit PcMonitorThread(NvComputer* computer);
    ~PcMonitorThread() override;

    // QThread's method is not virtual. Polling entries must retain this type
    // so every interruption also wakes the polling delay.
    void requestInterruption();

signals:
    void computerStateChanged(NvComputer* computer);

private:
    QSharedPointer<NvComputer> probeComputer(NvAddress address);
    bool pollAddresses(const QVector<NvAddress>& addresses, bool& changed);
    bool updateAppList(bool& changed);
    bool stopRequested();
    bool waitForNextPoll();
    void run() override;

    NvComputer* m_Computer;
    QMutex m_WaitMutex;
    QWaitCondition m_WaitCondition;
    bool m_StopRequested = false; // Protected by m_WaitMutex, including before start().
};

class ComputerPollingEntry
{
public:
    ComputerPollingEntry()
        : m_ActiveThread(nullptr)
    {

    }

    virtual ~ComputerPollingEntry()
    {
        interrupt();

        // interrupt() should have taken care of this
        Q_ASSERT(m_ActiveThread == nullptr);

        for (QThread* thread : m_InactiveList) {
            thread->wait();
            delete thread;
        }
    }

    bool isActive()
    {
        cleanInactiveList();

        return m_ActiveThread != nullptr;
    }

    void setActiveThread(PcMonitorThread* thread)
    {
        cleanInactiveList();

        Q_ASSERT(!isActive());
        m_ActiveThread = thread;
    }

    void interrupt()
    {
        cleanInactiveList();

        if (m_ActiveThread != nullptr) {
            // Interrupt the active thread
            m_ActiveThread->requestInterruption();

            // Place it on the inactive list awaiting death
            m_InactiveList.append(m_ActiveThread);

            m_ActiveThread = nullptr;
        }
    }

private:
    void cleanInactiveList()
    {
        QMutableListIterator<QThread*> i(m_InactiveList);

        // Reap any threads that have finished
        while (i.hasNext()) {
            i.next();

            QThread* thread = i.value();
            if (thread->isFinished()) {
                delete thread;
                i.remove();
            }
        }
    }

    PcMonitorThread* m_ActiveThread;
    QList<QThread*> m_InactiveList;
};

class ComputerManager : public QObject
{
    Q_OBJECT

    friend class DeferredHostDeletionTask;
    friend class PendingAddTask;
    friend class PendingPairingTask;
    friend class DelayedFlushThread;

public:
    explicit ComputerManager(StreamingPreferences* prefs, bool readOnly = false, const QString& targetHostId = QString());

    virtual ~ComputerManager();

    Q_INVOKABLE void startPolling();

    Q_INVOKABLE void stopPollingAsync();

    Q_INVOKABLE void addNewHostManually(QString address);
    Q_INVOKABLE bool addBoundHost(QVariantMap peer, bool explicitAdd = false, bool sessionSnapshot = false);

    void addNewHost(NvAddress address, bool mdns, NvAddress mdnsIpv6Address = NvAddress());

    QString generatePinString();

    void pairHost(NvComputer* computer, QString pin);

    void quitRunningApp(NvComputer* computer);

    QVector<NvComputer*> getComputers();

    // computer is deleted inside this call
    void deleteHost(NvComputer* computer);
    Q_INVOKABLE bool deleteHostById(const QString& hostId);

    void renameHost(NvComputer* computer, QString name);

    void clientSideAttributeUpdated(NvComputer* computer);

signals:
    void computerStateChanged(NvComputer* computer);
    void computerRemoved(NvComputer* computer);

    void pairingCompleted(NvComputer* computer, QString error);

    void computerAddCompleted(QVariant success, QVariant detectedPortBlocking);

    void quitAppCompleted(QVariant error);

private slots:
    void handleAboutToQuit();

    void handleComputerStateChanged(NvComputer* computer);

    void handleMdnsServiceResolved(MdnsPendingComputer* computer, QVector<QHostAddress>& addresses);

private:
    void saveHosts();

    void saveHost(NvComputer* computer);

    QHostAddress getBestGlobalAddressV6(QVector<QHostAddress>& addresses);

    void startPollingComputer(NvComputer* computer);

    StreamingPreferences* m_Prefs;
    bool m_ReadOnly = false;
    int m_PollingRef;
    QReadWriteLock m_Lock;
    QMap<QString, NvComputer*> m_KnownHosts;
    QMap<QString, ComputerPollingEntry*> m_PollEntries;
    QHash<QString, NvComputer> m_LastSerializedHosts; // Protected by m_DelayedFlushMutex
    QSharedPointer<QMdnsEngine::Server> m_MdnsServer;
    QMdnsEngine::Browser* m_MdnsBrowser;
    QVector<MdnsPendingComputer*> m_PendingResolution;
    CompatFetcher m_CompatFetcher;
    DelayedFlushThread* m_DelayedFlushThread;
    QMutex m_DelayedFlushMutex; // Lock ordering: Must never be acquired while holding NvComputer lock
    QWaitCondition m_DelayedFlushCondition;
    bool m_NeedsDelayedFlush;
};
