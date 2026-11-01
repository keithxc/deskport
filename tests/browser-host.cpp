#include "browserhost.h"
#include "hostmanager.h"
#include "workspaceresolution.h"
#include <QApplication>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>

class FakeBrowserHost : public HostManager {
public:
    using Completion = std::function<void(QJsonObject)>;
    struct Operation { QJsonObject body; QPointer<QObject> context; Completion done; };
    explicit FakeBrowserHost(const QString& directory) : HostManager(nullptr, directory, false) {}
    bool available() const override { return true; }
    bool running() const override { return online; }
    bool canPair() const override { return online && sharing; }
    bool adaptiveDisplayAvailable() const override { return adaptive; }
    bool resizeDisplay(int width, int height, int scale, int sequence, int policy) override {
        events.append("resize"); lastSequence = sequence; lastSize = QSize(width, height); lastScale = scale; lastPolicy = policy;
        return acceptResize;
    }
    void settleSessionDisplay(QObject* context, std::function<void(bool)> completion) override {
        events.append("restore"); restoreContext = context; restoreDone = std::move(completion);
    }
    void browserControl(const QJsonObject& body, QObject* context, Completion completion) override {
        events.append(body.value("action").toString());
        operations.append({body, context, std::move(completion)});
    }
    void resized(const QString& error = {}) { emit displayResized(lastSequence, lastSize.width(), lastSize.height(), error); }
    void restored(bool ok = true) {
        auto callback = std::move(restoreDone); restoreDone = {};
        if (restoreContext && callback) callback(ok);
    }
    bool complete(const QString& action, bool ok = true, const QString& code = {}) {
        for (int i = 0; i < operations.size(); ++i) {
            if (operations[i].body.value("action").toString() != action) continue;
            const auto operation = operations.takeAt(i);
            QJsonObject result{{"status", ok}, {"version", 1}};
            if (action == "reserve" && nativeOwner) { result["status"] = false; result["code"] = "busy"; }
            if (!code.isEmpty()) result["code"] = code;
            if (action == "reserve" && result.value("status").toBool()) owner = operation.body.value("id").toString();
            if (action == "release" && result.value("status").toBool()) owner.clear();
            if (action == "start" && ok) { result["type"] = "offer"; result["sdp"] = "v=0\r\n"; }
            if (action == "status" && ok) result["state"] = "connected";
            if (operation.context) operation.done(result);
            return true;
        }
        return false;
    }
    void crashed() { online = false; owner.clear(); emit changed(); }
    bool online = true, sharing = true, adaptive = true, acceptResize = true, nativeOwner = false;
    QString owner;
    QStringList events;
    QVector<Operation> operations;
    int lastSequence = 0, lastScale = 0, lastPolicy = -1;
    QSize lastSize;
    QPointer<QObject> restoreContext;
    std::function<void(bool)> restoreDone;
};

class BrowserHostTests : public QObject {
    Q_OBJECT
    const QString id = "browser-isolated-session-00000001";
    QJsonObject startBody() const { return {{"action", "start"}, {"id", id}, {"width", 1280}, {"height", 720}, {"fps", 30}, {"bitrateKbps", 6000}}; }
    void send(BrowserHost& browser, QJsonObject body, QJsonObject& result, int* count = nullptr) {
        browser.request(body, &browser, [&result, count](QJsonObject value) { result = value; if (count) ++*count; });
    }
    void establish(BrowserHost& browser, FakeBrowserHost& host, QJsonObject& result) {
        send(browser, startBody(), result);
        QVERIFY(host.complete("reserve"));
        if (host.adaptive) host.resized();
        QVERIFY(host.complete("start"));
        QVERIFY(result.value("status").toBool());
    }
    void finishEnd(FakeBrowserHost& host) {
        QVERIFY(host.complete("stop"));
        if (host.restoreDone) host.restored();
        QVERIFY(host.complete("release"));
    }
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("DeskPortTest");
        QCoreApplication::setApplicationName("BrowserHostIsolated");
    }
    void reserveResizeStartAndRestoreReleaseBarriers() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser"));
        QJsonObject started, ended, competing;
        send(browser, startBody(), started);
        QCOMPARE(host.events, QStringList{"reserve"}); QVERIFY(browser.busy()); QVERIFY(started.isEmpty());
        QVERIFY(host.complete("reserve"));
        QCOMPARE(host.events, (QStringList{"reserve", "resize"})); QVERIFY(started.isEmpty());
        QCOMPARE(host.lastSize, QSize(1280, 720)); QCOMPARE(host.lastScale, 1);
        host.resized(); QCOMPARE(host.events.last(), QString("start")); QVERIFY(started.isEmpty());
        QVERIFY(host.complete("start")); QVERIFY(started.value("status").toBool());
        send(browser, {{"action", "stop"}, {"id", id}}, ended);
        QCOMPARE(host.events.last(), QString("stop"));
        QVERIFY(host.operations.last().body.value("keepReservation").toBool());
        QVERIFY(ended.isEmpty()); QCOMPARE(host.owner, id);
        QVERIFY(host.complete("stop")); QCOMPARE(host.events.last(), QString("restore"));
        QVERIFY(!host.events.contains("release")); QVERIFY(ended.isEmpty());
        send(browser, startBody(), competing); QCOMPARE(competing.value("code").toString(), QString("busy"));
        host.restored(); QCOMPARE(host.events.last(), QString("release")); QVERIFY(ended.isEmpty()); QCOMPARE(host.owner, id);
        QVERIFY(host.complete("release")); QVERIFY(ended.value("status").toBool()); QVERIFY(!browser.busy()); QVERIFY(host.owner.isEmpty());
    }
    void nativeTakeoverEndsTheSessionAndTellsThePage() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser"));
        QJsonObject result, ended, status;
        QVERIFY(!host.browserSessionBusy());
        establish(browser, host, result);
        QVERIFY(host.browserSessionBusy());
        browser.endForTakeover([&ended](QJsonObject value) { ended = value; });
        finishEnd(host);
        QVERIFY(ended.value("status").toBool()); QVERIFY(!browser.busy());
        send(browser, {{"action", "heartbeat"}, {"id", id}}, status);
        QCOMPARE(status.value("code").toString(), QString("taken-over"));
    }
    void nativeOwnerIsNeverEvictedOrResized() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); host.nativeOwner = true;
        BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        send(browser, startBody(), result); QVERIFY(host.complete("reserve"));
        QCOMPARE(result.value("code").toString(), QString("busy"));
        QCOMPARE(host.events, QStringList{"reserve"}); QVERIFY(!browser.busy()); QVERIFY(host.nativeOwner);
    }
    void duplicateDisplayAcknowledgementStartsExactlyOnce() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        send(browser, startBody(), result); QVERIFY(host.complete("reserve"));
        host.resized(); host.resized();
        QCOMPARE(host.events.count("start"), 1);
        QVERIFY(host.complete("start")); QVERIFY(result.value("status").toBool());
        send(browser, {{"action", "stop"}, {"id", id}}, result); finishEnd(host);
    }
    void viewportStartUsesSharedWorkspacePolicy() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        auto body = startBody();
        body["viewport"] = QJsonObject{{"width", 1440}, {"height", 820}, {"ratio", 2}};
        body["zoom"] = 1.2; body["displayPolicy"] = 2;
        send(browser, body, result); QVERIFY(host.complete("reserve"));
        const auto expected = DeskPortDisplay::adjusted(DeskPortDisplay::forClient(QSize(2880, 1640), 2), 1.2);
        QVERIFY(!expected.pixels.isEmpty());
        QCOMPARE(host.lastSize, expected.pixels); QCOMPARE(host.lastScale, expected.scale); QCOMPARE(host.lastPolicy, 2);
        host.resized();
        const auto media = host.operations.last().body;
        QCOMPARE(media.value("action").toString(), QString("start"));
        QCOMPARE(QSize(media.value("width").toInt(), media.value("height").toInt()), expected.pixels);
        QVERIFY(!media.contains("viewport")); QVERIFY(!media.contains("displayPolicy"));
        QVERIFY(host.complete("start")); QVERIFY(result.value("status").toBool());
        send(browser, {{"action", "stop"}, {"id", id}}, result); finishEnd(host);
        body["displayPolicy"] = 1;
        send(browser, body, result); QCOMPARE(result.value("code").toString(), QString("invalid-session"));
    }
    void liveResizeOrdersDisplayBeforeMediaAndCoalesces() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        establish(browser, host, result);
        const auto view = [this](int width, int height) {
            return QJsonObject{{"action", "resize"}, {"id", id}, {"zoom", 1.0},
                               {"viewport", QJsonObject{{"width", width}, {"height", height}, {"ratio", 1}}}};
        };
        QJsonObject first, second, third; int secondCount = 0;
        send(browser, view(1600, 1000), first);
        QCOMPARE(host.events.last(), QString("resize")); QCOMPARE(host.lastPolicy, 0);
        send(browser, view(1200, 900), second, &secondCount);
        send(browser, view(1920, 1080), third);
        QCOMPARE(secondCount, 1); QVERIFY(second.value("superseded").toBool());
        QCOMPARE(host.events.count("resize"), 2);
        host.resized();
        QCOMPARE(host.operations.last().body.value("action").toString(), QString("resize"));
        QVERIFY(first.isEmpty());
        QVERIFY(host.complete("resize")); QVERIFY(first.value("status").toBool());
        QCOMPARE(host.events.count("resize"), 4); QVERIFY(third.isEmpty());
        host.resized(); QVERIFY(host.complete("resize"));
        QVERIFY(third.value("status").toBool());
        QCOMPARE(QSize(third.value("width").toInt(), third.value("height").toInt()), host.lastSize);
        send(browser, {{"action", "resize"}, {"id", "someone-else-session-0001"}}, result);
        QCOMPARE(result.value("code").toString(), QString("not-owner"));
        send(browser, {{"action", "stop"}, {"id", id}}, result); finishEnd(host);
    }
    void invalidStartAndSharingOffDoNotReserve() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        auto body = startBody(); body["width"] = 1919;
        send(browser, body, result); QCOMPARE(result.value("code").toString(), QString("invalid-session")); QVERIFY(host.events.isEmpty());
        host.sharing = false; send(browser, startBody(), result);
        QVERIFY(!result.value("status").toBool()); QVERIFY(host.events.isEmpty());
    }
    void lostReserveReplyCleansPotentiallyAcquiredLease() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        send(browser, startBody(), result);
        host.owner = id; // The server applied the reserve; only its reply was lost.
        QVERIFY(host.complete("reserve", false, "unavailable"));
        QCOMPARE(host.events, (QStringList{"reserve", "stop"})); QVERIFY(result.isEmpty()); QVERIFY(browser.busy());
        finishEnd(host); QCOMPARE(result.value("code").toString(), QString("unavailable"));
        QVERIFY(host.owner.isEmpty()); QVERIFY(!browser.busy()); QVERIFY(!host.events.contains("resize"));
    }
    void failedDisplayAcquisitionReleasesReservedOwner() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); host.acceptResize = false;
        BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        send(browser, startBody(), result); QVERIFY(host.complete("reserve"));
        QCOMPARE(host.events, (QStringList{"reserve", "resize", "stop"})); QVERIFY(result.isEmpty());
        finishEnd(host); QCOMPARE(result.value("code").toString(), QString("display-unavailable"));
        QVERIFY(!host.events.contains("start")); QVERIFY(!browser.busy()); QVERIFY(host.owner.isEmpty());
    }
    void displayErrorUsesSameReleaseBarrier() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject result;
        send(browser, startBody(), result); QVERIFY(host.complete("reserve")); host.resized("test display rejected");
        QVERIFY(result.isEmpty()); QCOMPARE(host.events.last(), QString("stop"));
        finishEnd(host); QCOMPARE(result.value("code").toString(), QString("display-unavailable")); QVERIFY(!browser.busy());
    }
    void cancelDuringReserveNeverStartsOrResizes() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        send(browser, startBody(), started); send(browser, {{"action", "stop"}, {"id", id}}, ended);
        QCOMPARE(host.events, QStringList{"reserve"}); QVERIFY(started.isEmpty()); QVERIFY(ended.isEmpty());
        QVERIFY(host.complete("reserve")); QCOMPARE(host.events, (QStringList{"reserve", "stop"}));
        finishEnd(host); QCOMPARE(started.value("code").toString(), QString("cancelled"));
        QVERIFY(ended.value("status").toBool()); QVERIFY(!browser.busy());
    }
    void cancelDuringResizeWaitsForDisplayThenReleases() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        send(browser, startBody(), started); QVERIFY(host.complete("reserve"));
        send(browser, {{"action", "stop"}, {"id", id}}, ended); QCOMPARE(host.events.last(), QString("resize"));
        host.resized(); QCOMPARE(host.events.last(), QString("stop")); QVERIFY(!host.events.contains("start"));
        finishEnd(host); QCOMPARE(started.value("code").toString(), QString("cancelled")); QVERIFY(ended.value("status").toBool());
    }
    void cancelledLateMediaStartCannotPublishOffer() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        int starts = 0, stops = 0;
        send(browser, startBody(), started, &starts); QVERIFY(host.complete("reserve")); host.resized();
        send(browser, {{"action", "stop"}, {"id", id}}, ended, &stops);
        QVERIFY(ended.isEmpty()); QVERIFY(host.complete("start"));
        QVERIFY(started.isEmpty()); QCOMPARE(host.events.last(), QString("stop"));
        finishEnd(host); QCOMPARE(started.value("code").toString(), QString("cancelled"));
        QVERIFY(!started.contains("sdp")); QCOMPARE(starts, 1); QCOMPARE(stops, 1); QVERIFY(!browser.busy());
    }
    void shutdownWhileMediaStartsUsesSameCancellationPath() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started;
        send(browser, startBody(), started); QVERIFY(host.complete("reserve")); host.resized(); browser.stop();
        QVERIFY(host.complete("start")); finishEnd(host);
        QCOMPARE(started.value("code").toString(), QString("cancelled")); QVERIFY(!browser.busy());
    }
    void stopFailureRetainsOwnerAndDoesNotRestoreDisplay() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended, competitor;
        establish(browser, host, started); send(browser, {{"action", "stop"}, {"id", id}}, ended);
        QVERIFY(host.complete("stop", false, "unavailable")); QVERIFY(!ended.value("status").toBool());
        QVERIFY(!host.events.contains("restore")); QVERIFY(!host.events.contains("release")); QVERIFY(browser.busy());
        send(browser, startBody(), competitor); QCOMPARE(competitor.value("code").toString(), QString("busy"));
        send(browser, {{"action", "stop"}, {"id", id}}, ended); finishEnd(host); QVERIFY(!browser.busy());
    }
    void wrongOwnerCannotStopOrInjectInput() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, result;
        establish(browser, host, started); const int count = host.events.size();
        for (const auto& action : {"stop", "answer", "input", "heartbeat", "status"}) {
            send(browser, {{"action", action}, {"id", "browser-another-client-0002"}}, result);
            QCOMPARE(result.value("code").toString(), QString("not-owner"));
        }
        QCOMPARE(host.events.size(), count); QCOMPARE(host.owner, id);
        send(browser, {{"action", "stop"}, {"id", id}}, result); finishEnd(host);
    }
    void authoritativeOwnerLossDoesNotRestoreAnotherDisplay() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        establish(browser, host, started); host.owner = "browser-another-authorized-owner";
        send(browser, {{"action", "stop"}, {"id", id}}, ended);
        QVERIFY(host.complete("stop", false, "unauthorized")); QVERIFY(ended.value("status").toBool());
        QVERIFY(!browser.busy()); QVERIFY(!host.events.contains("restore")); QVERIFY(!host.events.contains("release"));
        QCOMPARE(host.owner, QString("browser-another-authorized-owner"));
    }
    void releaseFailureRetainsOwnerAndRetriesAfterListenerStops() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, result, competitor;
        establish(browser, host, started); browser.stop();
        QVERIFY(host.complete("stop")); host.restored();
        QVERIFY(host.complete("release", false, "unavailable"));
        QVERIFY(browser.busy()); QCOMPARE(host.owner, id);
        send(browser, startBody(), competitor); QCOMPARE(competitor.value("code").toString(), QString("busy"));
        QTRY_COMPARE_WITH_TIMEOUT(host.events.count("stop"), 2, 2500);
        QVERIFY(host.complete("stop")); QCOMPARE(host.events.last(), QString("release"));
        QCOMPARE(host.events.count("restore"), 1); // A successful restore need not be repeated.
        QVERIFY(host.complete("release")); QVERIFY(!browser.busy()); QVERIFY(host.owner.isEmpty());
    }
    void physicalDisplayDoesNotTriggerVirtualDisplayMutations() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); host.adaptive = false;
        BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        establish(browser, host, started); QCOMPARE(host.events, (QStringList{"reserve", "start"}));
        send(browser, {{"action", "stop"}, {"id", id}}, ended); finishEnd(host);
        QCOMPARE(host.events, (QStringList{"reserve", "start", "stop", "release"}));
    }
    void hostExitFencesLateStartCompletion() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        send(browser, startBody(), started); QVERIFY(host.complete("reserve")); host.resized();
        send(browser, {{"action", "stop"}, {"id", id}}, ended); host.crashed();
        QVERIFY(ended.value("status").toBool()); QVERIFY(!browser.busy());
        QVERIFY(host.complete("start")); QCOMPARE(started.value("code").toString(), QString("cancelled")); QVERIFY(!started.contains("sdp"));
    }
    void stoppingHostRetainsOwnerUntilProcessExit() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, ended;
        establish(browser, host, started); host.sharing = false;
        send(browser, {{"action", "stop"}, {"id", id}}, ended);
        QVERIFY(ended.isEmpty()); QVERIFY(browser.busy()); QCOMPARE(host.owner, id);
        QVERIFY(!host.events.contains("release"));
        host.crashed(); QVERIFY(ended.value("status").toBool()); QVERIFY(!browser.busy());
    }
    void explicitHeartbeatAndInternalPollRemainDifferentActions() {
        QTemporaryDir directory; FakeBrowserHost host(directory.path()); BrowserHost browser(&host, nullptr, directory.filePath("browser")); QJsonObject started, result;
        establish(browser, host, started);
        browser.poll(); QCOMPARE(host.events.last(), QString("status")); QVERIFY(host.complete("status"));
        send(browser, {{"action", "heartbeat"}, {"id", id}}, result);
        QCOMPARE(host.events.last(), QString("heartbeat")); QVERIFY(host.complete("heartbeat"));
        send(browser, {{"action", "stop"}, {"id", id}}, result); finishEnd(host);
    }
};
QTEST_MAIN(BrowserHostTests)
#include "browser-host.moc"
