#include "computermanager.h"
#include "hostliststate.h"
#include "hostalias.h"
#include "sessionwindowstate.h"
#include "gui/hostlayout.h"
#include "localhostfilter.h"
#include "hostports.h"
#include "boxartmanager.h"
#include "nvhttp.h"
#include "nvpairingmanager.h"

#include <Limelight.h>
#include <QtEndian>

#include <QSharedPointer>
#include <QThread>
#include <QThreadPool>
#include <QCoreApplication>
#include <QDeadlineTimer>

#include <random>
#include <vector>

#define SER_HOSTS "hosts"
#define SER_HOSTS_BACKUP "hostsbackup"

// Caller holds the computer lock when polling can mutate its fields.
static bool outsideDiscoveryScope(const NvComputer* host)
{
    // Preserve explicitly added or paired services, even on third-party ports.
    if (!host->manualAddress.isNull() || !host->serverCert.isNull()) return false;
    for (const auto& address : {host->localAddress, host->remoteAddress, host->ipv6Address}) {
        if (!address.isNull() && DeskPortNetwork::isPrivateBase(address.port())) return false;
    }
    return true;
}


#define TRIES_BEFORE_OFFLINING 2
#define POLLS_PER_APPLIST_FETCH 10

PcMonitorThread::PcMonitorThread(NvComputer* computer)
    : m_Computer(computer)
{
    setObjectName("Polling thread for " + computer->name);
}

PcMonitorThread::~PcMonitorThread()
{
    requestInterruption();
    wait();
}

void PcMonitorThread::requestInterruption()
{
    QMutexLocker lock(&m_WaitMutex);
    // Unlike QThread's flag, this also remembers a stop before start().
    m_StopRequested = true;
    QThread::requestInterruption();
    m_WaitCondition.wakeAll();
}

bool PcMonitorThread::stopRequested()
{
    QMutexLocker lock(&m_WaitMutex);
    return m_StopRequested || isInterruptionRequested();
}

bool PcMonitorThread::waitForNextPoll()
{
    QMutexLocker lock(&m_WaitMutex);
    const QDeadlineTimer deadline(3000);
    // Test and publish the stop predicate under the wait mutex, so interruption
    // cannot be lost between the predicate check and entering the wait.
    while (!m_StopRequested && !isInterruptionRequested() && !deadline.hasExpired()) {
        m_WaitCondition.wait(&m_WaitMutex, deadline);
    }
    return !m_StopRequested && !isInterruptionRequested();
}

// Probes one endpoint without touching shared state, so several endpoints
// can be checked concurrently.
QSharedPointer<NvComputer> PcMonitorThread::probeComputer(NvAddress address)
{
    NvHTTP http(address, 0, m_Computer->serverCert);

    QString serverInfo;
    try {
        serverInfo = http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true);
    } catch (...) {
        return {};
    }

    auto newState = QSharedPointer<NvComputer>::create(http, serverInfo);

    // Ensure the machine that responded is the one we intended to contact
    if (m_Computer->uuid != newState->uuid) {
        qInfo() << "Found unexpected PC" << newState->name << "looking for" << m_Computer->name;
        return {};
    }
    return newState;
}

// Checks every endpoint at once and keeps the highest-priority one that
// answered, so an unreachable LAN entry costs one timeout, not one per
// address, while still winning whenever it is reachable.
bool PcMonitorThread::pollAddresses(const QVector<NvAddress>& addresses, bool& changed)
{
    std::vector<QSharedPointer<NvComputer>> results(addresses.size());
    if (addresses.size() == 1) {
        results[0] = probeComputer(addresses[0]);
    } else {
        QVector<QThread*> probes;
        for (int i = 0; i < addresses.size(); i++) {
            probes.append(QThread::create([this, &results, &addresses, i] {
                results[i] = probeComputer(addresses[i]);
            }));
            probes.last()->start();
        }
        for (auto probe : probes) {
            probe->wait();
            delete probe;
        }
    }
    for (const auto& result : results) {
        if (result) {
            changed = m_Computer->update(*result);
            return true;
        }
    }
    return false;
}

bool PcMonitorThread::updateAppList(bool& changed)
{
    NvHTTP http(m_Computer);

    QVector<NvApp> appList;

    try {
        appList = http.getAppList();
        if (appList.isEmpty()) {
            return false;
        }
    } catch (...) {
        return false;
    }

    QWriteLocker lock(&m_Computer->lock);
    changed = m_Computer->updateAppList(appList);
    return true;
}

void PcMonitorThread::run()
{
    // Always fetch the applist the first time
    int pollsSinceLastAppListFetch = POLLS_PER_APPLIST_FETCH;
    while (!stopRequested()) {
        bool stateChanged = false;
        bool online = false;
        bool wasOnline = m_Computer->state == NvComputer::CS_ONLINE;
        for (int i = 0; i < (wasOnline ? TRIES_BEFORE_OFFLINING : 1) && !online; i++) {
            if (stopRequested()) {
                return;
            }

            if (pollAddresses(m_Computer->uniqueAddresses(), stateChanged)) {
                if (!wasOnline) {
                    qInfo() << m_Computer->name << "is now online at" << m_Computer->activeAddress.toString();
                }
                online = true;
            }
        }

        // Check if we failed after all retry attempts
        // Note: we don't need to acquire the read lock here,
        // because we're on the writing thread.
        if (!online && m_Computer->state != NvComputer::CS_OFFLINE) {
            qInfo() << m_Computer->name << "is now offline";
            m_Computer->state = NvComputer::CS_OFFLINE;
            stateChanged = true;
        }

        // Grab the applist if it's empty or it's been long enough that we need to refresh
        pollsSinceLastAppListFetch++;
        if (m_Computer->state == NvComputer::CS_ONLINE &&
                m_Computer->pairState == NvComputer::PS_PAIRED &&
                (m_Computer->appList.isEmpty() || pollsSinceLastAppListFetch >= POLLS_PER_APPLIST_FETCH)) {
            // Notify prior to the app list poll since it may take a while, and we don't
            // want to delay onlining of a machine, especially if we already have a cached list.
            if (stateChanged) {
                emit computerStateChanged(m_Computer);
                stateChanged = false;
            }

            if (updateAppList(stateChanged)) {
                pollsSinceLastAppListFetch = 0;
            }
        }

        if (stateChanged) {
            // Tell anyone listening that we've changed state
            emit computerStateChanged(m_Computer);
        }

        if (!waitForNextPoll()) {
            return;
        }
    }
}

ComputerManager::ComputerManager(StreamingPreferences* prefs, bool readOnly, const QString& targetHostId)
    : m_Prefs(prefs),
      m_ReadOnly(readOnly),
      m_PollingRef(0),
      m_MdnsBrowser(nullptr),
      m_CompatFetcher(nullptr),
      m_NeedsDelayedFlush(false)
{
    QSettings settings;

    // If there's a hosts backup copy, we must have failed to commit
    // a previous update before exiting. Restore the backup now.
    int hosts = settings.beginReadArray(SER_HOSTS_BACKUP);
    if (hosts == 0) {
        // If there's no host backup, read from the primary location.
        settings.endArray();
        hosts = settings.beginReadArray(SER_HOSTS);
    }

    // Inflate our hosts from QSettings
    for (int i = 0; i < hosts; i++) {
        settings.setArrayIndex(i);
        NvComputer* computer = new NvComputer(settings);
        if ((readOnly && !targetHostId.isEmpty() && computer->uuid.compare(targetHostId, Qt::CaseInsensitive) != 0) || HostListState::removed(computer->uuid)) { delete computer; continue; }
        m_KnownHosts[computer->uuid] = computer;
        m_LastSerializedHosts[computer->uuid] = *computer;
    }
    settings.endArray();

    // Fetch latest compatibility data asynchronously
    m_CompatFetcher.start();

    // Start the delayed flush thread to handle saveHosts() calls
    m_DelayedFlushThread = new DelayedFlushThread(this);
    m_DelayedFlushThread->start();

    // To quit in a timely manner, we must block additional requests
    // after we receive the aboutToQuit() signal. This is necessary
    // because NvHTTP uses aboutToQuit() to abort requests in progress
    // while quitting, however this is a one time signal - additional
    // requests would not be aborted and block termination.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &ComputerManager::handleAboutToQuit);
}

ComputerManager::~ComputerManager()
{
    // Stop the delayed flush thread before acquiring the lock in write mode
    // to avoid deadlocking with a flush that needs the lock in read mode.
    {
        // Wake the delayed flush thread
        m_DelayedFlushThread->requestInterruption();
        m_DelayedFlushCondition.wakeOne();

        // Wait for it to terminate (and finish any pending flush)
        m_DelayedFlushThread->wait();
        delete m_DelayedFlushThread;

        // Delayed flushes should have completed by now
        Q_ASSERT(!m_NeedsDelayedFlush);
    }

    QWriteLocker lock(&m_Lock);

    // Delete machines that haven't been resolved yet
    while (!m_PendingResolution.isEmpty()) {
        MdnsPendingComputer* computer = m_PendingResolution.first();
        delete computer;
        m_PendingResolution.removeFirst();
    }

    // Delete the browser to stop discovery
    delete m_MdnsBrowser;
    m_MdnsBrowser = nullptr;

    // Interrupt polling
    for (ComputerPollingEntry* entry : m_PollEntries) {
        entry->interrupt();
    }

    // Delete all polling entries (and associated threads)
    for (ComputerPollingEntry* entry : m_PollEntries) {
        delete entry;
    }

    // Destroy all NvComputer objects now that polling is halted
    for (NvComputer* computer : m_KnownHosts) {
        delete computer;
    }
}

void DelayedFlushThread::run() {
    for (;;) {
        // Wait for a delayed flush request or an interruption
        {
            QMutexLocker locker(&m_ComputerManager->m_DelayedFlushMutex);

            while (!QThread::currentThread()->isInterruptionRequested() && !m_ComputerManager->m_NeedsDelayedFlush) {
                m_ComputerManager->m_DelayedFlushCondition.wait(&m_ComputerManager->m_DelayedFlushMutex);
            }

            // Bail without flushing if we woke up for an interruption alone.
            // If we have both an interruption and a flush request, do the flush.
            if (!m_ComputerManager->m_NeedsDelayedFlush) {
                Q_ASSERT(QThread::currentThread()->isInterruptionRequested());
                break;
            }

            // Reset the delayed flush flag to ensure any racing saveHosts() call will set it again
            m_ComputerManager->m_NeedsDelayedFlush = false;

            // Update the last serialized hosts map under the delayed flush mutex
            QReadLocker hostsLock(&m_ComputerManager->m_Lock);
            m_ComputerManager->m_LastSerializedHosts.clear();
            for (const NvComputer* computer : m_ComputerManager->m_KnownHosts) {
                // Copy the current state of the NvComputer to allow us to check later if we need
                // to serialize it again when attribute updates occur.
                QReadLocker computerLock(&computer->lock);
                m_ComputerManager->m_LastSerializedHosts[computer->uuid] = *computer;
            }
        }

        // Perform the flush
        {
            QSettings settings;

            // First, write to the backup location
            settings.beginWriteArray(SER_HOSTS_BACKUP);
            {
                QReadLocker lock(&m_ComputerManager->m_Lock);
                int i = 0;
                for (const NvComputer* computer : m_ComputerManager->m_KnownHosts) {
                    settings.setArrayIndex(i++);
                    computer->serialize(settings, false);
                }
            }
            settings.endArray();

            // Next, write to the primary location
            settings.remove(SER_HOSTS);
            settings.beginWriteArray(SER_HOSTS);
            {
                QReadLocker lock(&m_ComputerManager->m_Lock);
                int i = 0;
                for (const NvComputer* computer : m_ComputerManager->m_KnownHosts) {
                    settings.setArrayIndex(i++);
                    computer->serialize(settings, true);
                }
            }
            settings.endArray();

            // Finally, delete the backup copy
            settings.remove(SER_HOSTS_BACKUP);
        }
    }
}

void ComputerManager::saveHosts()
{
    // Only the shell owns the persistent device registry. A background viewer
    // must never restore a removed device from its older in-memory snapshot.
    if (m_ReadOnly) return;
    Q_ASSERT(m_DelayedFlushThread != nullptr && m_DelayedFlushThread->isRunning());

    // Punt to a worker thread because QSettings on macOS can take ages (> 500 ms)
    // to persist our host list to disk (especially when a host has a bunch of apps).
    QMutexLocker locker(&m_DelayedFlushMutex);
    m_NeedsDelayedFlush = true;
    m_DelayedFlushCondition.wakeOne();
}

QHostAddress ComputerManager::getBestGlobalAddressV6(QVector<QHostAddress> &addresses)
{
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            if (address.isInSubnet(QHostAddress("fe80::"), 10)) {
                // Link-local
                continue;
            }

            if (address.isInSubnet(QHostAddress("fec0::"), 10)) {
                qInfo() << "Ignoring site-local address:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("fc00::"), 7)) {
                qInfo() << "Ignoring ULA:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("2002::"), 16)) {
                qInfo() << "Ignoring 6to4 address:" << address;
                continue;
            }

            if (address.isInSubnet(QHostAddress("2001::"), 32)) {
                qInfo() << "Ignoring Teredo address:" << address;
                continue;
            }

            return address;
        }
    }

    return QHostAddress();
}

void ComputerManager::startPolling()
{
    QWriteLocker lock(&m_Lock);

    if (++m_PollingRef > 1) {
        return;
    }

    if (m_Prefs->enableMdns && !m_ReadOnly) {
        // Start an MDNS query for GameStream hosts
        m_MdnsServer.reset(new QMdnsEngine::Server());
        m_MdnsBrowser = new QMdnsEngine::Browser(m_MdnsServer.data(), "_nvstream._tcp.local.");
        connect(m_MdnsBrowser, &QMdnsEngine::Browser::serviceAdded,
                this, [this](const QMdnsEngine::Service& service) {
            if (!DeskPortNetwork::isPrivateBase(service.port())) return;
            qInfo() << "Discovered mDNS host:" << service.hostname();

            MdnsPendingComputer* pendingComputer = new MdnsPendingComputer(m_MdnsServer, service);
            connect(pendingComputer, &MdnsPendingComputer::resolvedHost,
                    this, &ComputerManager::handleMdnsServiceResolved, Qt::DirectConnection);
            m_PendingResolution.append(pendingComputer);
        });
    }
    else {
        qWarning() << "mDNS is disabled by user preference";
    }

    // Start polling threads for each known host
    QMapIterator<QString, NvComputer*> i(m_KnownHosts);
    while (i.hasNext()) {
        i.next();
        startPollingComputer(i.value());
    }
}

// Must hold m_Lock for write
void ComputerManager::startPollingComputer(NvComputer* computer)
{
    if (m_PollingRef == 0) {
        return;
    }
    {
        QReadLocker hostLock(&computer->lock);
        if (outsideDiscoveryScope(computer)) return;
    }

    ComputerPollingEntry* pollingEntry;

    if (!m_PollEntries.contains(computer->uuid)) {
        pollingEntry = m_PollEntries[computer->uuid] = new ComputerPollingEntry();
    }
    else {
        pollingEntry = m_PollEntries[computer->uuid];
    }

    if (!pollingEntry->isActive()) {
        PcMonitorThread* thread = new PcMonitorThread(computer);
        connect(thread, &PcMonitorThread::computerStateChanged,
                this, &ComputerManager::handleComputerStateChanged);
        pollingEntry->setActiveThread(thread);
        thread->start();
    }
}

void ComputerManager::handleMdnsServiceResolved(MdnsPendingComputer* computer,
                                                QVector<QHostAddress>& addresses)
{
    // The signal carries an address reference owned by computer. Keep delivery
    // on the discovery thread and defer deletion until the emission returns.
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_PendingResolution.removeOne(computer)) {
        return;
    }

    QHostAddress v6Global = getBestGlobalAddressV6(addresses);
    bool added = false;

    // Add the host using the IPv4 address
    for (const QHostAddress& address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            // NB: We don't just call addNewHost() here with v6Global because the IPv6
            // address may not be reachable (if the user hasn't installed the IPv6 helper yet
            // or if this host lacks outbound IPv6 capability). We want to add IPv6 even if
            // it's not currently reachable.
            addNewHost(NvAddress(address, computer->port()), true, NvAddress(v6Global, computer->port()));
            added = true;
            break;
        }
    }

    if (!added) {
        // If we get here, there wasn't an IPv4 address so we'll do it v6-only
        for (const QHostAddress& address : addresses) {
            if (address.protocol() == QAbstractSocket::IPv6Protocol) {
                // Use a link-local or site-local address for the "local address"
                if (address.isInSubnet(QHostAddress("fe80::"), 10) ||
                        address.isInSubnet(QHostAddress("fec0::"), 10) ||
                        address.isInSubnet(QHostAddress("fc00::"), 7)) {
                    addNewHost(NvAddress(address, computer->port()), true, NvAddress(v6Global, computer->port()));
                    break;
                }
            }
        }
    }

    computer->deleteLater();
}

void ComputerManager::saveHost(NvComputer *computer)
{
    // If no serializable properties changed, don't bother saving hosts
    QMutexLocker lock(&m_DelayedFlushMutex);
    QReadLocker computerLock(&computer->lock);
    if (!m_LastSerializedHosts.value(computer->uuid).isEqualSerialized(*computer)) {
        // Queue a request for a delayed flush to QSettings outside of the lock
        computerLock.unlock();
        lock.unlock();
        saveHosts();
    }
}

void ComputerManager::handleComputerStateChanged(NvComputer* computer)
{
    // A queued polling/add notification may arrive after removal and disposal.
    // Compare identity without dereferencing a potentially retired pointer.
    { QReadLocker lock(&m_Lock);
      if (!m_KnownHosts.values().contains(computer)) return; }
    emit computerStateChanged(computer);

    if (computer->pendingQuit && computer->currentGameId == 0) {
        computer->pendingQuit = false;
        emit quitAppCompleted(QVariant());
    }

    // Save updates to this host
    saveHost(computer);
}

QVector<NvComputer*> ComputerManager::getComputers()
{
    QReadLocker lock(&m_Lock);

    // Return a sorted host list
    auto hosts = QVector<NvComputer*>::fromList(m_KnownHosts.values());
    const auto interfaces = QNetworkInterface::allAddresses();
    hosts.erase(std::remove_if(hosts.begin(), hosts.end(), [&](NvComputer* host) {
        QReadLocker hostLock(&host->lock);
        return outsideDiscoveryScope(host) ||
               DeskPortNetwork::isLocalHostAddress(host->localAddress.address(), interfaces) ||
               DeskPortNetwork::isLocalHostAddress(host->manualAddress.address(), interfaces) ||
               DeskPortNetwork::isLocalHostAddress(host->ipv6Address.address(), interfaces);
    }), hosts.end());
    std::stable_sort(hosts.begin(), hosts.end(), [](const NvComputer* host1, const NvComputer* host2) {
        return host1->name.toLower() < host2->name.toLower();
    });
    return hosts;
}

class DeferredHostDeletionTask : public QRunnable
{
public:
    DeferredHostDeletionTask(NvComputer* computer, ComputerPollingEntry* pollingEntry)
        : m_Computer(computer), m_PollingEntry(pollingEntry) {}
    void run() override {
        // The manager and every model have already detached this object.
        delete m_PollingEntry;
        BoxArtManager::deleteBoxArt(m_Computer);
        delete m_Computer;
    }
private:
    NvComputer* m_Computer;
    ComputerPollingEntry* m_PollingEntry;
};

bool ComputerManager::deleteHostById(const QString& hostId)
{
    if (hostId.isEmpty() || !HostListState::setRemoved(hostId, true)) return false;
    NvComputer* computer = nullptr;
    { QReadLocker lock(&m_Lock);
      for (auto host : m_KnownHosts)
          if (host->uuid.compare(hostId, Qt::CaseInsensitive) == 0) { computer = host; break; } }
    if (computer) deleteHost(computer);
    HostAlias::set(hostId, {});
    HostLayout::load().forget(hostId);
    QSettings settings;
    settings.remove("devices/" + QString::fromLatin1(QCryptographicHash::hash(hostId.toLower().toUtf8(), QCryptographicHash::Sha256).toHex()));
    settings.remove(DeskPortDisplay::sessionWindowKey(hostId));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

void ComputerManager::deleteHost(NvComputer* computer)
{
    ComputerPollingEntry* pollingEntry;
    {
        QWriteLocker lock(&m_Lock);
        if (!m_KnownHosts.values().contains(computer)) return;
        if (!HostListState::setRemoved(computer->uuid, true)) {
            qWarning() << "Could not persist device list removal";
            return;
        }
        pollingEntry = m_PollEntries.take(computer->uuid);
        m_KnownHosts.remove(computer->uuid);
    }
    // Detach all grid/folder models before any worker can release the object.
    emit computerRemoved(computer);
    saveHosts();
    QThreadPool::globalInstance()->start(new DeferredHostDeletionTask(computer, pollingEntry));
}

void ComputerManager::renameHost(NvComputer* computer, QString name)
{
    {
        QWriteLocker lock(&computer->lock);

        computer->name = name;
        computer->hasCustomName = true;
    }

    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

void ComputerManager::clientSideAttributeUpdated(NvComputer* computer)
{
    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

void ComputerManager::handleAboutToQuit()
{
    QReadLocker lock(&m_Lock);

    // Interrupt polling threads immediately, so they
    // avoid making additional requests while quitting
    for (ComputerPollingEntry* entry : m_PollEntries) {
        entry->interrupt();
    }
}

class PendingPairingTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    PendingPairingTask(ComputerManager* computerManager, NvComputer* computer, QString pin)
        : m_ComputerManager(computerManager),
          m_Computer(computer),
          m_Pin(pin)
    {
        connect(this, &PendingPairingTask::pairingCompleted,
                computerManager, &ComputerManager::pairingCompleted);
    }

signals:
    void pairingCompleted(NvComputer* computer, QString error);

private:
    void run()
    {
        NvPairingManager pairingManager(m_Computer);

        try {
           NvPairingManager::PairState result = pairingManager.pair(m_Computer->appVersion, m_Pin, m_Computer->serverCert);
           switch (result)
           {
           case NvPairingManager::PairState::PIN_WRONG:
               emit pairingCompleted(m_Computer, tr("The PIN from the PC didn't match. Please try again."));
               break;
           case NvPairingManager::PairState::FAILED:
               if (m_Computer->currentGameId != 0) {
                   emit pairingCompleted(m_Computer, tr("You cannot pair while a previous session is still running on the host PC. Quit any running games or reboot the host PC, then try pairing again."));
               }
               else {
                   emit pairingCompleted(m_Computer, tr("Pairing failed. Please try again."));
               }
               break;
           case NvPairingManager::PairState::ALREADY_IN_PROGRESS:
               emit pairingCompleted(m_Computer, tr("Another pairing attempt is already in progress."));
               break;
           case NvPairingManager::PairState::PAIRED:
               // Persist the newly pinned server certificate for this host
               m_ComputerManager->saveHost(m_Computer);

               emit pairingCompleted(m_Computer, nullptr);
               break;
           }
        } catch (const GfeHttpResponseException& e) {
            emit pairingCompleted(m_Computer, tr("GeForce Experience returned error: %1").arg(e.toQString()));
        } catch (const QtNetworkReplyException& e) {
            emit pairingCompleted(m_Computer, e.toQString());
        }
    }

    ComputerManager* m_ComputerManager;
    NvComputer* m_Computer;
    QString m_Pin;
};

void ComputerManager::pairHost(NvComputer* computer, QString pin)
{
    // Punt to a worker thread to avoid stalling the
    // UI while waiting for pairing to complete
    PendingPairingTask* pairing = new PendingPairingTask(this, computer, pin);
    QThreadPool::globalInstance()->start(pairing);
}

class PendingQuitTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    PendingQuitTask(ComputerManager* computerManager, NvComputer* computer)
        : m_Computer(computer)
    {
        connect(this, &PendingQuitTask::quitAppFailed,
                computerManager, &ComputerManager::quitAppCompleted);
    }

signals:
    void quitAppFailed(QString error);

private:
    void run()
    {
        NvHTTP http(m_Computer);

        try {
            if (m_Computer->currentGameId != 0) {
                http.quitApp();
            }
        } catch (const GfeHttpResponseException& e) {
            {
                QWriteLocker lock(&m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            if (e.getStatusCode() == 599) {
                // 599 is a special code we make a custom message for
                emit quitAppFailed(tr("The running game wasn't started by this PC. "
                                      "You must quit the game on the host PC manually or use the device that originally started the game."));
            }
            else {
                emit quitAppFailed(e.toQString());
            }
        } catch (const QtNetworkReplyException& e) {
            {
                QWriteLocker lock(&m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            emit quitAppFailed(e.toQString());
        }
    }

    NvComputer* m_Computer;
};

void ComputerManager::quitRunningApp(NvComputer* computer)
{
    QWriteLocker lock(&computer->lock);
    computer->pendingQuit = true;

    PendingQuitTask* quit = new PendingQuitTask(this, computer);
    QThreadPool::globalInstance()->start(quit);
}

void ComputerManager::stopPollingAsync()
{
    QWriteLocker lock(&m_Lock);

    // Refresh/focus transitions may request a stop after polling has already
    // stopped. Never underflow: a subsequent start at zero would create a
    // second browser, replacing its server while the old timer still runs.
    if (m_PollingRef <= 0) {
        qWarning() << "Ignoring stop of inactive computer polling";
        return;
    }
    if (--m_PollingRef > 0) {
        return;
    }

    // Delete machines that haven't been resolved yet
    while (!m_PendingResolution.isEmpty()) {
        MdnsPendingComputer* computer = m_PendingResolution.first();
        computer->deleteLater();
        m_PendingResolution.removeFirst();
    }

    // Delete the browser and server to stop discovery and refresh polling
    delete m_MdnsBrowser;
    m_MdnsBrowser = nullptr;
    m_MdnsServer.reset();

    // Interrupt all threads, but don't wait for them to terminate
    for (ComputerPollingEntry* entry : m_PollEntries) {
        entry->interrupt();
    }
}

void ComputerManager::addNewHostManually(QString address)
{
    if (m_ReadOnly) return;
    const NvAddress endpoint = NvAddress::fromUserInput(address);
    if (!endpoint.isNull()) {
        // New manual entries target DeskPort. Explicit ports and saved hosts retain their endpoints.
        addNewHost(endpoint, false);
    }
    else {
        emit computerAddCompleted(false, false);
    }
}

class PendingAddTask : public QObject, public QRunnable
{
    Q_OBJECT

public:
    PendingAddTask(ComputerManager* computerManager, NvAddress address, NvAddress mdnsIpv6Address, bool mdns)
        : m_ComputerManager(computerManager),
          m_Address(address),
          m_MdnsIpv6Address(mdnsIpv6Address),
          m_Mdns(mdns),
          m_AboutToQuit(false)
    {
        connect(this, &PendingAddTask::computerAddCompleted,
                computerManager, &ComputerManager::computerAddCompleted);
        connect(this, &PendingAddTask::computerStateChanged,
                computerManager, &ComputerManager::handleComputerStateChanged);
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &PendingAddTask::handleAboutToQuit);
    }

signals:
    void computerAddCompleted(QVariant success, QVariant detectedPortBlocking);

    void computerStateChanged(NvComputer* computer);

private:
    void handleAboutToQuit()
    {
        m_AboutToQuit = true;
    }

    QString fetchServerInfo(NvHTTP& http)
    {
        QString serverInfo;

        // Do nothing if we're quitting
        if (m_AboutToQuit) {
            return QString();
        }

        try {
            // There's a race condition between GameStream servers reporting presence over
            // mDNS and the HTTPS server being ready to respond to our queries. To work
            // around this issue, we will issue the request again after a few seconds if
            // we see a ServiceUnavailableError error.
            try {
                serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
            } catch (const QtNetworkReplyException& e) {
                if (e.getError() == QNetworkReply::ServiceUnavailableError) {
                    qWarning() << "Retrying request in 5 seconds after ServiceUnavailableError";
                    QThread::sleep(5);
                    serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
                    qInfo() << "Retry successful";
                }
                else {
                    // Rethrow other errors
                    throw e;
                }
            }
            return serverInfo;
        } catch (...) {
            if (!m_Mdns) {
                unsigned int portTestResult;

                if (m_ComputerManager->m_Prefs->detectNetworkBlocking) {
                    // We failed to connect to the specified PC. Let's test to make sure this network
                    // isn't blocking Moonlight, so we can tell the user about it.
                    portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443,
                                                              ML_PORT_FLAG_TCP_47984 | ML_PORT_FLAG_TCP_47989);
                }
                else {
                    portTestResult = 0;
                }

                emit computerAddCompleted(false, portTestResult != 0 && portTestResult != ML_TEST_RESULT_INCONCLUSIVE);
            }
            return QString();
        }
    }

    void run()
    {
        // Ignore our own mDNS advertisements before probing or saving another UUID.
        if (m_Mdns && (!DeskPortNetwork::isPrivateBase(m_Address.port()) ||
                       DeskPortNetwork::isLocalHostAddress(m_Address.address()))) return;
        NvHTTP http(m_Address, 0, QSslCertificate());

        qInfo() << "Processing new PC at" << m_Address.toString() << "from" << (m_Mdns ? "mDNS" : "user") << "with IPv6 address" << m_MdnsIpv6Address.toString();

        // Perform initial serverinfo fetch over HTTP since we don't know which cert to use
        QString serverInfo = fetchServerInfo(http);
        if (serverInfo.isEmpty() && !m_MdnsIpv6Address.isNull()) {
            // Retry using the global IPv6 address if the IPv4 or link-local IPv6 address fails
            http.setAddress(m_MdnsIpv6Address);
            serverInfo = fetchServerInfo(http);
        }
        if (serverInfo.isEmpty()) {
            return;
        }

        // Create initial newComputer using HTTP serverinfo with no pinned cert
        NvComputer* newComputer = new NvComputer(http, serverInfo);

        // Check if we have a record of this host UUID to pull the pinned cert
        NvComputer* existingComputer;
        {
            QReadLocker lock(&m_ComputerManager->m_Lock);
            existingComputer = m_ComputerManager->m_KnownHosts.value(newComputer->uuid);
            if (existingComputer != nullptr) {
                http.setServerCert(existingComputer->serverCert);
            }
        }

        // Fetch serverinfo again over HTTPS with the pinned cert
        if (existingComputer != nullptr) {
            Q_ASSERT(http.httpsPort() != 0);
            serverInfo = fetchServerInfo(http);
            if (serverInfo.isEmpty()) {
                return;
            }

            // Update the polled computer with the HTTPS information
            NvComputer httpsComputer(http, serverInfo);
            newComputer->update(httpsComputer);
        }

        // Update addresses depending on the context
        if (m_Mdns) {
            // Only update local address if we actually reached the PC via this address.
            // If we reached it via the IPv6 address after the local address failed,
            // don't store the non-working local address.
            if (http.address() == m_Address) {
                newComputer->localAddress = m_Address;
            }

            // Get the WAN IP address using STUN if we're on mDNS over IPv4
            if (QHostAddress(newComputer->localAddress.address()).protocol() == QAbstractSocket::IPv4Protocol) {
                quint32 addr;
                int err = LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &addr);
                if (err == 0) {
                    newComputer->setRemoteAddress(QHostAddress(qFromBigEndian(addr)));
                }
                else {
                    qWarning() << "STUN failed to get WAN address:" << err;
                }
            }

            if (!m_MdnsIpv6Address.isNull()) {
                Q_ASSERT(QHostAddress(m_MdnsIpv6Address.address()).protocol() == QAbstractSocket::IPv6Protocol);
                newComputer->ipv6Address = m_MdnsIpv6Address;
            }
        }
        else {
            newComputer->manualAddress = m_Address;
        }

        QHostAddress hostAddress(m_Address.address());
        bool addressIsSiteLocalV4 =
                hostAddress.isInSubnet(QHostAddress("10.0.0.0"), 8) ||
                hostAddress.isInSubnet(QHostAddress("172.16.0.0"), 12) ||
                hostAddress.isInSubnet(QHostAddress("192.168.0.0"), 16);

        {
            // Check if this PC already exists using opportunistic read lock
            m_ComputerManager->m_Lock.lockForRead();
            NvComputer* existingComputer = m_ComputerManager->m_KnownHosts.value(newComputer->uuid);

            // If it doesn't already exist, convert to a write lock in preparation for updating.
            //
            // NB: ComputerManager's lock protects the host map itself, not the elements inside.
            // Those are protected by their individual locks. Since we only mutate the map itself
            // when the PC doesn't exist, we need the lock in write-mode for that case only.
            if (existingComputer == nullptr) {
                m_ComputerManager->m_Lock.unlock();
                m_ComputerManager->m_Lock.lockForWrite();

                // Since we had to unlock to lock for write, someone could have raced and added
                // this PC before us. We have to check again whether it already exists.
                existingComputer = m_ComputerManager->m_KnownHosts.value(newComputer->uuid);
            }

            if (!HostListState::admit(newComputer->uuid, !m_Mdns && !m_ComputerManager->m_ReadOnly)) {
                m_ComputerManager->m_Lock.unlock();
                delete newComputer;
                return;
            }

            if (existingComputer != nullptr) {
                // Fold it into the existing PC
                bool changed = existingComputer->update(*newComputer);
                delete newComputer;

                // Drop the lock before notifying
                m_ComputerManager->m_Lock.unlock();

                // For non-mDNS clients, let them know it succeeded
                if (!m_Mdns) {
                    emit computerAddCompleted(true, false);
                }

                // Tell our client if something changed
                if (changed) {
                    qInfo() << existingComputer->name << "is now at" << existingComputer->activeAddress.toString();
                    emit computerStateChanged(existingComputer);
                }
            }
            else {
                // Store this in our active sets
                m_ComputerManager->m_KnownHosts[newComputer->uuid] = newComputer;

                // Start polling if enabled (write lock required)
                m_ComputerManager->startPollingComputer(newComputer);

                // Drop the lock before notifying
                m_ComputerManager->m_Lock.unlock();

                // If this wasn't added via mDNS but it is a RFC 1918 IPv4 address and not a VPN,
                // go ahead and do the STUN request now to populate an external address.
                if (!m_Mdns && addressIsSiteLocalV4 && newComputer->getActiveAddressReachability() != NvComputer::RI_VPN) {
                    quint32 addr;
                    int err = LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &addr);
                    if (err == 0) {
                        newComputer->setRemoteAddress(QHostAddress(qFromBigEndian(addr)));
                    }
                    else {
                        qWarning() << "STUN failed to get WAN address:" << err;
                    }
                }

                // For non-mDNS clients, let them know it succeeded
                if (!m_Mdns) {
                    emit computerAddCompleted(true, false);
                }

                // Tell our client about this new PC
                emit computerStateChanged(newComputer);
            }
        }
    }

    ComputerManager* m_ComputerManager;
    NvAddress m_Address;
    NvAddress m_MdnsIpv6Address;
    bool m_Mdns;
    bool m_AboutToQuit;
};

void ComputerManager::addNewHost(NvAddress address, bool mdns, NvAddress mdnsIpv6Address)
{
    // Punt to a worker thread to avoid stalling the
    // UI while waiting for serverinfo query to complete
    PendingAddTask* addTask = new PendingAddTask(this, address, mdnsIpv6Address, mdns);
    QThreadPool::globalInstance()->start(addTask);
}

// TODO: Use QRandomGenerator when we drop Qt 5.9 support
QString ComputerManager::generatePinString()
{
    std::uniform_int_distribution<int> dist(0, 9999);
    std::random_device rd;
    std::mt19937 engine(rd());

    return QString::asprintf("%04u", dist(engine));
}

#include "computermanager.moc"

bool ComputerManager::addBoundHost(QVariantMap peer, bool explicitAdd, bool sessionSnapshot) {
    if (m_ReadOnly && !sessionSnapshot) return false;
    const QString uuid = peer.value("hostId").toString();
    const QString address = peer.value("address").toString();
    const int port = peer.value("hostPort").toInt();
    const QSslCertificate certificate(peer.value("hostCert").toString().toUtf8());
    // Match the URL-based HTTP transport: bound endpoints may be DNS names.
    auto validHost = [](const QString& host) {
        QUrl endpoint;
        endpoint.setScheme("https");
        endpoint.setHost(host);
        return endpoint.isValid() && !endpoint.host().isEmpty();
    };
    if (uuid.isEmpty() || !validHost(address) || port < 1024 || port > 65514 || certificate.isNull()) return false;
    // Further configured endpoints share the ports and are tried in order.
    QVector<NvAddress> alternates;
    for (const auto& value : peer.value("addresses").toList()) {
        const QString alternate = value.toString();
        const NvAddress candidate(alternate, quint16(port));
        if (alternate.compare(address, Qt::CaseInsensitive) != 0 && validHost(alternate) && !alternates.contains(candidate))
            alternates.append(candidate);
    }
    NvComputer* host;
    {
        QWriteLocker lock(&m_Lock);
        if (m_ReadOnly ? HostListState::removed(uuid) : !HostListState::admit(uuid, explicitAdd)) return false;
        host = m_KnownHosts.value(uuid);
        if (host) {
            QWriteLocker hostLock(&host->lock);
            // A binding must not silently replace an unrelated pinned host identity.
            if (!host->serverCert.isNull() && host->serverCert != certificate) return false;
            host->manualAddress = NvAddress(address, quint16(port));
            host->alternateAddresses = alternates;
            // Forget a last-used endpoint the user removed; discovered ones stay.
            const auto active = host->activeAddress;
            if (!active.isNull() && active != host->manualAddress && !alternates.contains(active) &&
                    active != host->localAddress && active != host->remoteAddress && active != host->ipv6Address)
                host->activeAddress = NvAddress();
            host->name = peer.value("name").toString(); host->hasCustomName = true;
            host->serverCert = certificate;
        } else {
            host = new NvComputer();
            host->name = peer.value("name").toString(); host->hasCustomName = true; host->uuid = uuid;
            host->manualAddress = NvAddress(address, quint16(port));
            host->alternateAddresses = alternates;
            host->serverCert = certificate;
            host->state = NvComputer::CS_UNKNOWN; host->pairState = NvComputer::PS_UNKNOWN;
            host->isSupportedServerVersion = true; host->externalPort = quint16(port);
            host->activeHttpsPort = quint16(port - 5);
            m_KnownHosts[uuid] = host;
            startPollingComputer(host);
        }
    }
    { QWriteLocker hostLock(&host->lock);
      if (!peer.value("os").toString().isEmpty()) host->operatingSystem = peer.value("os").toString().left(80); }
    saveHost(host);
    emit computerStateChanged(host);
    return true;
}
