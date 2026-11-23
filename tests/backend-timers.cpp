// Synthetic timer lifecycle tests. No personal host, clipboard or native session.
#include "browsergateway.h"
#include "browserhost.h"
#include "hostmanager.h"
#include "peermanager.h"
#include "peermanager-link.h"
#include <QApplication>
#include <QDateTime>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TimerTestHost : public HostManager {
public:
    explicit TimerTestHost(const QString& directory) : HostManager(nullptr, directory, false) {}
    bool running() const override { return online; }
    bool canPair() const override { return online; }
    bool adaptiveDisplayAvailable() const override { return adaptive; }
    bool resizeDisplay(int width, int height, int scale, int sequence, int policy = 0) override {
        Q_UNUSED(width); Q_UNUSED(height); Q_UNUSED(scale); Q_UNUSED(policy);
        if (sequence < 0) ++restores;
        return true;
    }
    void sessionControl(const QJsonObject&, QObject*, std::function<void(QJsonObject)> done) override {
        ++releases; done({{"status", true}});
    }
    void browserControl(const QJsonObject& body, QObject*, std::function<void(QJsonObject)> done) override {
        const auto action = body.value("action").toString();
        if (action == "status") {
            ++polls;
            if (holdStatus) { pendingStatus = std::move(done); return; }
        }
        const bool okay = action != "stop" || !failStop;
        done({{"status", okay}, {"state", closed ? "closed" : "connected"}, {"code", okay ? "" : "unavailable"}});
    }
    bool online = true, adaptive = false, closed = false, failStop = false, holdStatus = false;
    int polls = 0, releases = 0, restores = 0;
    std::function<void(QJsonObject)> pendingStatus;
};

class BackendTimerTests : public QObject {
    Q_OBJECT
    static bool listen(BrowserGateway& gateway) {
        // No TLS handshake or generated credentials: a loopback listener is
        // sufficient to exercise the real active/stop lifecycle and scheduler.
        gateway.m_Port = 0;
        return gateway.addListener(QHostAddress::LocalHost);
    }
    static QJsonObject startBody() {
        return {{"id", "synthetic-browser-timer-session"}, {"width", 1280}, {"height", 720}};
    }
    static PeerManager::Link* link(PeerManager& manager) {
        auto result = new PeerManager::Link(&manager);
        result->socket = new QSslSocket(result);
        result->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
        result->lastClipboardRequest = result->lastDisplayRequest;
        return result;
    }
    static bool marker(const QString& path) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("synthetic\n") == 10 && file.commit();
    }
private slots:
    void gatewayIdleAndRateDeadline() {
        BrowserGateway gateway(BrowserGateway::Hooks{});
        QVERIFY(listen(gateway));
        gateway.updateCleanupTimer();
        QVERIFY(!gateway.m_Cleanup.isActive());
        QVERIFY(gateway.loginAllowed("synthetic-address"));
        QVERIFY(gateway.m_Cleanup.isActive());
        QVERIFY(gateway.m_Cleanup.remainingTime() > 3500000);
        // A live session must shorten an existing hour-long rate deadline.
        BrowserGateway::Session session;
        gateway.m_Sessions.insert("synthetic", session);
        gateway.updateCleanupTimer();
        QVERIFY(gateway.m_Cleanup.remainingTime() <= 1000);
        gateway.m_Sessions.clear();
        gateway.m_Rates["synthetic-address"].updated = -3600001;
        gateway.cleanup();
        QVERIFY(gateway.m_Rates.isEmpty());
        QVERIFY(!gateway.m_Cleanup.isActive());
        gateway.stop();
        QVERIFY(!gateway.m_AddressRefresh.isActive());
        QVERIFY(!gateway.m_CodeTick.isActive());
    }
    void gatewayExpiredSessions_data() {
        QTest::addColumn<QString>("reason");
        for (const auto& reason : {"idle", "lifetime", "media", "pending", "pairing"})
            QTest::newRow(reason) << QString::fromLatin1(reason);
    }
    void gatewayExpiredSessions() {
        QFETCH(QString, reason);
        int stopped = 0;
        BrowserGateway::Hooks hooks;
        hooks.request = [&stopped](const QJsonObject& body, QObject*, std::function<void(QJsonObject)> done) {
            if (body.value("action").toString() == "stop") ++stopped;
            done({{"status", true}});
        };
        BrowserGateway gateway(hooks);
        QVERIFY(listen(gateway));
        BrowserGateway::Session session;
        session.id = "synthetic";
        session.created = session.lastSeen = 1;
        if (reason == "idle") session.lastSeen = -900001;
        if (reason == "lifetime") session.created = -8LL * 60 * 60 * 1000;
        if (reason == "media") session.media = true;
        if (reason == "pending") session.pending = true;
        session.lastHeartbeat = -20001;
        if (reason == "pairing") {
            session.pairingId = "expired";
            BrowserGateway::Pairing pairing;
            pairing.expiresAt = QDateTime::currentMSecsSinceEpoch() - 1;
            gateway.m_Pairings.insert("expired", pairing);
        }
        gateway.m_Sessions.insert("synthetic", session);
        gateway.updateCleanupTimer();
        QTRY_VERIFY_WITH_TIMEOUT(gateway.m_Sessions.isEmpty(), 1500);
        QCOMPARE(stopped, reason == "media" || reason == "pending" ? 1 : 0);
        QVERIFY(!gateway.m_Cleanup.isActive());
    }
    void gatewayCodeFailureAndBanExpiry() {
        BrowserGateway::Options options;
        options.codeFailureLimit = 2;
        BrowserGateway gateway({}, options);
        QVERIFY(listen(gateway));
        QVERIFY(!gateway.acceptCode("invalid"));
        QVERIFY(gateway.m_Cleanup.remainingTime() > 800000);
        gateway.m_CodeFailures[0] = -900001;
        gateway.cleanup();
        QVERIFY(gateway.m_CodeFailures.isEmpty());
        QVERIFY(!gateway.m_Cleanup.isActive());
        QVERIFY(!gateway.acceptCode("invalid"));
        QVERIFY(!gateway.acceptCode("invalid"));
        QVERIFY(gateway.m_LockedUntil > 0);
        QVERIFY(gateway.m_CodeFailures.isEmpty());
        QSignalSpy changed(&gateway, &BrowserGateway::changed);
        gateway.m_LockedUntil = 1;
        gateway.cleanup();
        QCOMPARE(gateway.m_LockedUntil, qint64(0));
        QCOMPARE(changed.count(), 1);
        QVERIFY(!gateway.m_Cleanup.isActive());
        // Stop preserves admission limits, and reactivation schedules them.
        QVERIFY(gateway.loginAllowed("synthetic-address"));
        gateway.stop();
        QVERIFY(!gateway.m_Cleanup.isActive());
        QVERIFY(listen(gateway));
        gateway.updateCleanupTimer();
        QVERIFY(gateway.m_Cleanup.isActive());
    }
    void browserPollTracksReservationAndHostExit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TimerTestHost host(directory.path());
        BrowserHost browser(&host, nullptr, directory.path() + "/browser");
        browser.m_Listening = true;
        emit browser.changed();
        QVERIFY(!browser.m_Poll.isActive());
        browser.begin(startBody(), [](QJsonObject) {});
        QVERIFY(browser.m_Poll.isActive());
        host.closed = true;
        browser.m_Poll.start(1);
        QTRY_VERIFY_WITH_TIMEOUT(browser.m_Id.isEmpty(), 500);
        QVERIFY(host.polls > 0);
        QVERIFY(!browser.m_Poll.isActive());
        host.closed = false;
        browser.begin(startBody(), [](QJsonObject) {});
        QVERIFY(browser.m_Poll.isActive());
        host.online = false;
        emit host.changed();
        QVERIFY(browser.m_Id.isEmpty());
        QVERIFY(!browser.m_Poll.isActive());
    }
    void browserCleanupRetryAndStaleReply() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TimerTestHost host(directory.path());
        BrowserHost browser(&host, nullptr, directory.path() + "/browser");
        browser.m_Listening = true;
        browser.begin(startBody(), [](QJsonObject) {});
        host.holdStatus = true;
        browser.poll();
        QVERIFY(bool(host.pendingStatus));
        host.failStop = true;
        browser.end();
        QVERIFY(browser.m_Poll.isActive());
        QVERIFY(!browser.m_Id.isEmpty());
        // A pre-cleanup response cannot resurrect polling state or ownership.
        host.pendingStatus({{"status", true}, {"state", "connected"}});
        host.pendingStatus = {};
        host.failStop = false;
        QTRY_VERIFY_WITH_TIMEOUT(browser.m_Id.isEmpty(), 2200);
        QVERIFY(!browser.m_Poll.isActive());
    }
    void peerWatchdogCoversEveryController() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TimerTestHost host(directory.path());
        PeerManager manager(&host, {}, {}, directory.path() + "/binding", 0, QHostAddress::LocalHost);
        QVERIFY(!manager.m_Watchdog.isActive());
        auto session = link(manager), display = link(manager), clipboard = link(manager);
        manager.m_SessionLink = session;
        manager.m_DisplayLink = display;
        manager.m_ClipboardLink = clipboard;
        emit manager.changed();
        QVERIFY(manager.m_Watchdog.isActive());
        session->lastDisplayRequest -= 21000;
        clipboard->lastClipboardRequest -= 151000;
        QVERIFY(QMetaObject::invokeMethod(&manager.m_Watchdog, "timeout", Qt::DirectConnection));
        QVERIFY(session->ended && display->ended && clipboard->ended);
        QCOMPARE(host.releases, 1);
        QVERIFY(!manager.m_Watchdog.isActive());
        QVERIFY(!manager.m_EndpointRefresh.isActive());
        // Reconnection replaces the transport before retiring the old link.
        auto old = link(manager), replacement = link(manager);
        manager.m_SessionLink = old;
        emit manager.changed();
        manager.m_SessionLink = replacement;
        manager.fail(old, "Synthetic transport replacement");
        QVERIFY(manager.m_Watchdog.isActive());
        QCOMPARE(manager.m_SessionLink, replacement);
        manager.fail(replacement, "Synthetic disconnect");
        QVERIFY(!manager.m_Watchdog.isActive());
    }
    void peerDiscoverySurvivesIdleSessions() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TimerTestHost host(directory.path());
        PeerManager manager(&host, {}, {}, directory.path() + "/binding", 0, QHostAddress::LocalHost);
        // Enable scheduling only; the ten-second discovery callback never runs.
        manager.m_Persistent = manager.m_Healthy = true;
        QJsonObject peer{{"address", "127.0.0.1"}, {"ready", true}, {"granted", true}, {"role", "client"}};
        manager.m_Peers.insert("synthetic", peer);
        emit manager.changed();
        QVERIFY(!manager.m_EndpointRefresh.isActive());
        peer["role"] = "host";
        manager.m_Peers.insert("synthetic", peer);
        emit manager.changed();
        QVERIFY(manager.m_EndpointRefresh.isActive());
        QVERIFY(!manager.m_Watchdog.isActive());
        peer["ready"] = false;
        manager.m_Peers.insert("synthetic", peer);
        emit manager.changed();
        QVERIFY(!manager.m_EndpointRefresh.isActive());
        peer["ready"] = true;
        manager.m_Peers.insert("synthetic", peer);
        emit manager.changed();
        QVERIFY(manager.m_EndpointRefresh.isActive());
        manager.m_Peers = {};
        emit manager.changed();
        QVERIFY(!manager.m_EndpointRefresh.isActive());
    }
    void parkedDisplayExpiryAndClaim() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TimerTestHost host(directory.path());
        QVERIFY(!host.m_ParkedDisplayTimer.isActive());
        QVERIFY(!host.m_ParkedInputMonitor.isActive());
        host.adaptive = true;
        host.m_DisplayWidth = 1332;
        host.m_DisplayHeight = 1600;
        host.m_ParkedDisplayTimeoutMs = 120;
        host.parkDisplay("peer-a", 0);
        QVERIFY(host.displayParked());
        QVERIFY(host.m_ParkedDisplayTimer.isActive());
        QVERIFY(!host.m_ParkedInputMonitor.isActive());
        host.m_ParkedInputMonitor.start(500);
        host.claimParkedDisplay("peer-a");
        QVERIFY(!host.m_ParkedInputMonitor.isActive());
        QVERIFY(host.m_ParkedDisplayTimer.isActive());
        QTRY_VERIFY_WITH_TIMEOUT(!host.displayParked(), 1000);
        QVERIFY(!host.m_ParkedDisplayTimer.isActive());
        QTRY_COMPARE_WITH_TIMEOUT(host.restores, 1, 1000);
    }
    void unattendedExternalMarkers() {
#ifndef Q_OS_MACOS
        QSKIP("macOS unattended recovery contract");
#else
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        HostManager host(nullptr, directory.path() + "/not-created/host", false);
        QVERIFY(!host.m_UnattendedMonitor.isActive());
        // Explicitly opt this isolated manager into watches of synthetic files.
        host.m_UnattendedMonitoring = true;
        host.refreshUnattended();
        QVERIFY(!host.m_UnattendedMonitor.isActive());
        QVERIFY(!host.m_UnattendedWatcher.directories().isEmpty());
        const auto path = host.unattendedDirectory();
        QVERIFY(QDir().mkpath(path));
        QVERIFY(marker(path + "/enabled"));
        QTRY_VERIFY_WITH_TIMEOUT(host.m_UnattendedMonitor.isActive(), 1000);
        QVERIFY(marker(path + "/enabled")); // Atomic replacement must re-arm.
        QTRY_VERIFY_WITH_TIMEOUT(host.m_UnattendedWatcher.files().contains(path + "/enabled"), 1000);
        QVERIFY(marker(path + "/heartbeat"));
        QTRY_VERIFY_WITH_TIMEOUT(host.m_UnattendedWatcher.files().contains(path + "/heartbeat"), 1000);
        QVERIFY(QFile::remove(path + "/enabled"));
        QTRY_VERIFY_WITH_TIMEOUT(!host.m_UnattendedMonitor.isActive(), 1000);
        QVERIFY(QDir(path).removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(!host.m_UnattendedWatcher.directories().contains(path), 1000);
        QVERIFY(QDir().mkpath(path));
        QVERIFY(marker(path + "/enabled"));
        QTRY_VERIFY_WITH_TIMEOUT(host.m_UnattendedMonitor.isActive(), 1000);
        host.m_ShuttingDown = true;
        host.updateUnattendedMonitor();
        QVERIFY(!host.m_UnattendedMonitor.isActive());
        QVERIFY(host.m_UnattendedWatcher.files().isEmpty());
        QVERIFY(host.m_UnattendedWatcher.directories().isEmpty());
#endif
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
        qFatal("Backend timer tests require the offscreen platform");
    QCoreApplication::setOrganizationName("DeskPortTimerTests");
    QCoreApplication::setApplicationName("BackendTimers");
    QTemporaryDir settings;
    if (!settings.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    QSettings().setValue("diagnostics/enabled", false);
    BackendTimerTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "backend-timers.moc"
