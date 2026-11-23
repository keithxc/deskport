// Link real desktop objects; synthetic DNS data never adds or polls a host.
#include <qmdnsengine/abstractserver.h>
#include <QtCore>
#include <QtNetwork>
#include "backend/nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "settings/compatfetcher.h"
#include <qmdnsengine/server.h>
#include <qmdnsengine/cache.h>
#include <qmdnsengine/browser.h>
#include <qmdnsengine/service.h>
#include <qmdnsengine/resolver.h>
#include <qmdnsengine/message.h>
#include <qmdnsengine/record.h>
#include <qmdnsengine/dns.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>

// Inspect existing state without introducing a production test API. Load all
// dependencies above so this changes access only in computermanager.h.
#define private public
#include "backend/computermanager.h"
#undef private

class SilentMdnsServer final : public QMdnsEngine::AbstractServer
{
public:
    void sendMessage(const QMdnsEngine::Message&) override {}
    void sendMessageToAll(const QMdnsEngine::Message&) override {}
};

static void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void flushDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

static void expire(MdnsPendingComputer* pending)
{
    // Dispatch through the production moc object, including its inline slot.
    check(QMetaObject::invokeMethod(pending, "handleResolvedTimeout", Qt::DirectConnection),
          "production timeout slot exists");
}

static MdnsPendingComputer* discover(ComputerManager& manager,
                                    const QSharedPointer<QMdnsEngine::AbstractServer>& server)
{
    QMdnsEngine::Service service;
    service.setHostname("fixture.invalid.");
    service.setPort(48989);
    auto pending = new MdnsPendingComputer(server, service);
    QObject::connect(pending, &MdnsPendingComputer::resolvedHost,
                     &manager, &ComputerManager::handleMdnsServiceResolved, Qt::DirectConnection);
    manager.m_PendingResolution.append(pending);
    return pending;
}

static void queueAddress(QMdnsEngine::Resolver* resolver)
{
    // AutoConnection queues a real resolver signal across threads. Join before
    // retiring the resolver; only its already-posted callback survives.
    auto emitter = QThread::create([resolver] {
        emit resolver->resolved(QHostAddress("2001:db8::99"));
    });
    emitter->start();
    check(emitter->wait(1000), "resolver emitter joined");
    delete emitter;
}

static void retriesAndCompletion(ComputerManager& manager)
{
    auto server = QSharedPointer<SilentMdnsServer>::create();
    QWeakPointer<SilentMdnsServer> weakServer(server);
    QPointer<MdnsPendingComputer> pending = discover(manager, server);
    int completions = 0;
    QObject::connect(pending, &MdnsPendingComputer::resolvedHost, &manager,
                     [&](MdnsPendingComputer* sender, QVector<QHostAddress>& addresses) {
        ++completions;
        check(manager.m_PendingResolution.isEmpty(), "real manager removed pending synchronously");
        check(sender == pending && pending && addresses.isEmpty(), "failure signal remains valid");
        check(QThread::currentThread() == manager.thread(), "completion stays on manager thread");
        check(weakServer.isNull() && !pending->m_Resolver, "completion releases transport and resolver");
        expire(sender); // Reentrant completion must be harmless.
    }, Qt::DirectConnection);
    for (int attempt = 0; attempt < 10; ++attempt) {
        QPointer<QMdnsEngine::Resolver> old = pending->m_Resolver;
        check(old && pending->m_Timeout.isActive() && pending->m_Timeout.isSingleShot() &&
              pending->m_Timeout.interval() == 2000, "two-second collection window is armed");
        expire(pending);
        check(old.isNull() && pending->m_Resolver, "retry replaces the actual resolver");
        check(completions == 0 && manager.m_PendingResolution.size() == 1,
              "first ten timeouts retain pending registration");
    }
    QPointer<QMdnsEngine::Resolver> finalResolver = pending->m_Resolver;
    queueAddress(finalResolver);
    server.reset();
    expire(pending);
    check(completions == 1 && pending && finalResolver.isNull(), "eleventh timeout completes without inline deletion");
    check(!pending->m_Timeout.isActive(), "completion stops collection timer");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    check(pending->m_Addresses.isEmpty(), "queued final-attempt callback cannot revive completion");
    expire(pending);
    check(completions == 1 && manager.getComputers().isEmpty(), "duplicate completion adds nothing");
    flushDeletes();
    check(pending.isNull(), "real manager defers pending deletion");
}

static void retryAndMultipleAddresses(ComputerManager& manager)
{
    auto server = QSharedPointer<SilentMdnsServer>::create();
    QPointer<MdnsPendingComputer> pending = discover(manager, server);
    QPointer<QMdnsEngine::Resolver> old = pending->m_Resolver;
    queueAddress(old);
    expire(pending);
    check(old.isNull(), "retry destroys old resolver");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    check(pending->m_Addresses.isEmpty() && !pending->m_Completed,
          "queued old-attempt callback cannot contaminate retry");

    int completions = 0;
    QVector<QHostAddress> result;
    QObject::connect(pending, &MdnsPendingComputer::resolvedHost, &manager,
                     [&](MdnsPendingComputer*, QVector<QHostAddress>& addresses) {
        ++completions;
        result = addresses;
        check(pending && manager.m_PendingResolution.isEmpty(), "successful removal precedes deferred deletion");
    }, Qt::DirectConnection);
    QMdnsEngine::Message response;
    response.setResponse(true);
    const QVector<QHostAddress> expected{QHostAddress("2001:db8::1"), QHostAddress("2001:db8::2")};
    for (const auto& address : expected) {
        QMdnsEngine::Record record;
        record.setName("fixture.invalid.");
        record.setType(QMdnsEngine::AAAA);
        record.setTtl(60);
        record.setAddress(address);
        response.addRecord(record);
        response.addRecord(record);
    }
    // Exercise real resolver parsing and duplicate filtering. Global-only IPv6
    // cannot trigger the manager's local-endpoint addNewHost network path.
    emit server->messageReceived(response);
    emit server->messageReceived(response);
    check(pending->m_Addresses == expected && completions == 0 && pending->m_Timeout.isActive(),
          "real resolver collects multiple addresses once until timeout");
    QPointer<QMdnsEngine::Resolver> resolver = pending->m_Resolver;
    expire(pending);
    check(completions == 1 && result == expected && resolver.isNull(), "success publishes all addresses once");
    expire(pending);
    manager.handleMdnsServiceResolved(pending, result);
    check(completions == 1 && manager.getComputers().isEmpty(), "duplicate callback cannot add a host");
    flushDeletes();
    check(pending.isNull(), "successful pending object is reclaimed");
}

static void missingServerAndRepeatedDiscovery(ComputerManager& manager)
{
    for (const bool loseServer : {false, true}) {
        auto server = QSharedPointer<SilentMdnsServer>::create();
        QWeakPointer<SilentMdnsServer> weak(server);
        QPointer<MdnsPendingComputer> pending = discover(
            manager, loseServer ? server : QSharedPointer<SilentMdnsServer>());
        check(!pending->m_Completed && manager.m_PendingResolution.size() == 1,
              "missing transport does not complete before registration");
        if (loseServer) {
            server.reset();
            expire(pending); // Retry drops the last strong reference before reacquisition.
            check(weak.isNull(), "transport can disappear between attempts");
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (!manager.m_PendingResolution.isEmpty() && elapsed.elapsed() < 1000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        check(manager.m_PendingResolution.isEmpty(), "missing transport completes via queued zero-timeout");
        flushDeletes();
        check(pending.isNull(), "missing transport pending is reclaimed");
    }
    auto server = QSharedPointer<SilentMdnsServer>::create();
    for (int discovery = 0; discovery < 20; ++discovery) {
        QPointer<MdnsPendingComputer> pending = discover(manager, server);
        QPointer<QMdnsEngine::Resolver> resolver = pending->m_Resolver;
        for (int attempt = 0; attempt < 11; ++attempt) expire(pending);
        flushDeletes();
        check(pending.isNull() && resolver.isNull() && manager.m_PendingResolution.isEmpty(),
              "repeated failed discovery retains no pending object or resolver");
    }
    QPointer<MdnsPendingComputer> pending = discover(manager, server);
    queueAddress(pending->m_Resolver);
    manager.m_PendingResolution.removeOne(pending);
    QPointer<QMdnsEngine::Resolver> resolver = pending->m_Resolver;
    delete pending.data();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    check(pending.isNull() && resolver.isNull(), "destruction cancels outstanding resolver callback");
}

static void pollingWaits()
{
    NvComputer host{};
    host.name = "Synthetic empty-address monitor";
    host.state = NvComputer::CS_OFFLINE;
    host.pairState = NvComputer::PS_NOT_PAIRED;
    check(host.uniqueAddresses().isEmpty(), "real fixture has no endpoint to probe");
    PcMonitorThread monitor(&host);
    std::atomic<bool> finished{false};
    auto waker = QThread::create([&] {
        while (!finished.load()) {
            monitor.m_WaitCondition.wakeAll();
            QThread::msleep(5);
        }
    });
    waker->start();
    QElapsedTimer elapsed;
    elapsed.start();
    const bool keepPolling = monitor.waitForNextPoll();
    const auto duration = elapsed.elapsed();
    finished.store(true);
    waker->wait();
    delete waker;
    check(keepPolling && duration >= 2900 && duration < 4500, "spurious wakes preserve three-second poll deadline");
    monitor.requestInterruption();
    monitor.start();
    check(monitor.wait(500) && !monitor.waitForNextPoll(), "stop before thread start is remembered");

    // Stop exactly between the real polling loop and its wait predicate.
    host.state = NvComputer::CS_UNKNOWN;
    PcMonitorThread boundary(&host);
    QSemaphore entering, proceed;
    QObject::connect(&boundary, &PcMonitorThread::computerStateChanged, &boundary,
                     [&](NvComputer*) { entering.release(); proceed.acquire(); }, Qt::DirectConnection);
    boundary.start();
    check(entering.tryAcquire(1, 1000), "empty-address poll reaches wait boundary");
    boundary.requestInterruption();
    proceed.release();
    check(boundary.wait(500), "interruption at wait boundary is not lost");

    for (const bool useEntry : {false, true}) {
        host.state = NvComputer::CS_UNKNOWN;
        auto running = new PcMonitorThread(&host);
        ComputerPollingEntry entry;
        if (useEntry) entry.setActiveThread(running);
        QSemaphore polled;
        QObject::connect(running, &PcMonitorThread::computerStateChanged, running,
                         [&](NvComputer*) { polled.release(); }, Qt::DirectConnection);
        running->start();
        check(polled.tryAcquire(1, 1000), "real empty-address polling loop ran");
        QThread::msleep(20);
        elapsed.restart();
        if (useEntry) {
            entry.interrupt();
            check(running->wait(500), "polling entry wakes active delay");
            check(!entry.isActive(), "polling entry reaps inactive thread");
        }
        else {
            delete running;
        }
        check(elapsed.elapsed() < 500, "entry interruption and destructor join promptly");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    check(argc == 2, "runner supplies isolated settings directory");
    QCoreApplication::setOrganizationName("DeskPortLifecycleTest");
    QCoreApplication::setApplicationName("ProductionObjects");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    for (const auto scope : {QSettings::UserScope, QSettings::SystemScope})
        QSettings::setPath(QSettings::IniFormat, scope, argv[1]);
    // Contain the manager constructor's unrelated compatibility fetch locally.
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
    qRegisterMetaType<QHostAddress>();
    auto prefs = StreamingPreferences::get();
    prefs->enableMdns = false;
    {
        ComputerManager manager(prefs, true);
        check(manager.getComputers().isEmpty(), "isolated manager starts empty");
        retriesAndCompletion(manager);
        retryAndMultipleAddresses(manager);
        missingServerAndRepeatedDiscovery(manager);
    }
    pollingWaits();
    std::puts("PASS: real manager/resolver lifecycle and empty-address polling wakeups");
    return 0;
}
