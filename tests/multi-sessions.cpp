#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>
#include <QSettings>
#include <QQuickWindow>
#include <QLockFile>
#include "multisessions.h"
#include "peermanager.h"
#include "peerstore.h"
#include "sessiongraph.h"
#include "windowframe.h"
static QByteArray credential(const char* name) { QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }

// Keep JSON/host fixtures in the managed test run even when Unix sockets need
// a shorter TMPDIR. QTemporaryDir's default template otherwise follows TMPDIR.
static QString fixtureTemplate() {
    return QDir(qEnvironmentVariable("DESKPORT_TEST_FIXTURE_ROOT", QDir::tempPath()))
        .filePath("multi-sessions-XXXXXX");
}

// Real child processes and local sockets; synthetic media never contacts a host
// or touches physical input. Slow hide acknowledgements test ownership ordering.
static int worker(QApplication& app) {
    QLocalSocket socket;
    QByteArray buffer;
    int fullscreenRequests = 0, raisedShows = 0, quietShows = 0;
    int shortcutShows = 0, tokenShows = 0;
    int frameReleases = 0, frameHides = 0;
    bool presenting = false;
    std::unique_ptr<QLockFile> inputLease;
    if (!qEnvironmentVariableIsEmpty("TEST_SESSION_INPUT_LEASE"))
        inputLease.reset(new QLockFile(qEnvironmentVariable("TEST_SESSION_INPUT_LEASE")));
    const QString id=app.arguments().value(2);
    const auto mode=app.arguments().value(3);
    const bool connecting = mode.startsWith("connecting-");
    auto send=[&](QJsonObject o) { socket.write(QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n'); };
    QObject::connect(&socket,&QLocalSocket::connected,&app,[&] {
        send({{"token",qEnvironmentVariable("DESKPORT_SESSION_TOKEN")}});
        if (!connecting) { send({{"type","ready"}}); send({{"type","connected"}}); }
    });
    QObject::connect(&socket,&QLocalSocket::readyRead,&app,[&] {
        buffer+=socket.readAll();
        while (buffer.contains('\n')) {
            auto end=buffer.indexOf('\n'); auto o=QJsonDocument::fromJson(buffer.left(end)).object(); buffer.remove(0,end+1);
            const auto action=o.value("command").toString();
            if (mode == "frame-refresh" && action == "canvas-state") {
                const auto state = o.value("state").toObject();
                if (state.contains("visible") && !state["visible"].toBool() && !state["focused"].toBool()) {
                    ++frameReleases;
                    const auto path = qEnvironmentVariable("DESKPORT_TEST_CANVAS_RELEASE_FILE");
                    if (!path.isEmpty()) { QFile marker(path); if (marker.open(QIODevice::WriteOnly)) marker.write(QByteArray::number(frameReleases)); }
                }
                send({{"type", "traffic"}, {"received", frameReleases}, {"sent", frameHides}});
            }
            if (connecting && action == "initialize")
                send({{"type","traffic"},{"received",1},{"sent",0}});
            if(mode=="shortcut-config" && (action=="initialize" || action=="shortcut")) {
                const auto sequence = o.value(action=="initialize" ? "shortcutSequence" : "sequence").toString();
                const bool enabled = o.value(action=="initialize" ? "shortcutEnabled" : "enabled").toBool();
                send({{"type","traffic"},{"received",sequence=="Ctrl+Alt+K" ? 1 : sequence=="Meta+F8" ? 2 : 0},{"sent",enabled ? 1 : 0}});
            }
            if(action=="show") {
                if (!o.value("raise").toBool(true) &&
                    (o.value("shortcut").toBool() || !o.value("activationToken").toString().isEmpty()))
                    QCoreApplication::exit(8); // Recovery must not inherit an old user action.
                shortcutShows += o.value("shortcut").toBool();
                tokenShows += !o.value("activationToken").toString().isEmpty();
                if (!presenting && inputLease && !inputLease->tryLock()) {
                    send({{"type","error"},{"message","fixture input ownership overlapped"}});
                    QCoreApplication::exit(9); return;
                }
                presenting=true; ++(o.value("raise").toBool(true) ? raisedShows : quietShows);
                send({{"type","shown"}});
                if (mode.startsWith("activation")) send({{"type","traffic"},{"received",shortcutShows},{"sent",tokenShows}});
                if (mode=="recover") send({{"type","traffic"},{"received",quietShows},{"sent",raisedShows}});
                if (connecting) send({{"type","traffic"},{"received",1},{"sent",raisedShows+quietShows}});
                // Models a transport restart under a retained window: an
                // unsolicited hide acknowledgement, then a new ready viewer.
                if (mode=="recover" && raisedShows==1 && quietShows==0)
                    QTimer::singleShot(300,&app,[&] { send({{"type","hidden"},{"epoch",0}}); send({{"type","ready"}}); });
            }
            else if(action=="fullscreen") { ++fullscreenRequests;
                send({{"type","traffic"},{"received",0},{"sent",fullscreenRequests},{"tuningLimited",fullscreenRequests==1}}); }
            else if(action=="hide") {
                if (mode == "frame-refresh") { ++frameHides; send({{"type", "traffic"}, {"received", frameReleases}, {"sent", frameHides}}); }
                if (mode=="hung") continue;
                // Models a busy background viewer: it only answers while presenting.
                if (mode=="lazy" && !presenting) continue;
                presenting=false;
                QTimer::singleShot(mode.endsWith("slow") ? 300 : 0,&app,[&,o] { if (inputLease) inputLease->unlock(); send({{"type","hidden"},{"epoch",o.value("epoch")}}); });
            } else if(action=="disconnect") {
                if (mode == "error-on-disconnect") {
                    send({{"type","connected"}});
                    send({{"type","error"},{"message","Late worker error"}});
                    socket.flush();
                }
                QTimer::singleShot(0,&app,&QCoreApplication::quit);
            }
            else if(action=="tuning") // Echo the value; a tuning change never asks for a reconnect.
                send({{"type","traffic"},{"received",qRound(o.value("value").toDouble()*100)},{"sent",fullscreenRequests}});
            else if(action=="reconnect" && mode=="crash") QCoreApplication::exit(7);
            else if(action=="reconnect" && mode=="next") send({{"type","next"}}); // The viewer's chord.
            else if(action=="reconnect" && connecting) {
                // Finish a deliberately slow connection only when requested by
                // the test, including after the shell was hidden by the user.
                if (mode == "connecting-crash-slow") QCoreApplication::exit(7);
                else { send({{"type","ready"}}); send({{"type","connected"}}); }
            }
        }
    });
    QObject::connect(&socket,&QLocalSocket::disconnected,&app,&QCoreApplication::quit);
    if (connecting) QTimer::singleShot(500,&app,[&] { socket.connectToServer(qEnvironmentVariable("DESKPORT_SESSION_ENDPOINT")); });
    else socket.connectToServer(qEnvironmentVariable("DESKPORT_SESSION_ENDPOINT"));
    QTimer::singleShot(15000,&app,&QCoreApplication::quit);
    return app.exec();
}
class MultiSessionsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY2(QSslSocket::supportsSsl(), "The fixture requires a working Qt SSL backend");
        QVERIFY2(!QSslCertificate(credential("TEST_CERT_A")).isNull(), "Local fixture certificate is unavailable");
        QVERIFY2(!QSslCertificate(credential("TEST_CERT_B")).isNull(), "Remote fixture certificate is unavailable");
        QTemporaryDir dir(fixtureTemplate());
        QVERIFY2(dir.isValid(), qPrintable("Fixture directory is not writable: " + fixtureTemplate()));
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        const auto name = "deskport-sessions-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY2(server.listen(name), qPrintable(QString("Private session socket unavailable (%1 bytes): %2")
            .arg(QFile::encodeName(QDir::tempPath() + '/' + name).size()).arg(server.errorString())));
    }
    void launchRejectsIncompleteBinding_data() {
        QTest::addColumn<QString>("missing");
        for (auto field : {"ready", "granted", "clientCert", "bindingPort", "address"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void launchRejectsIncompleteBinding() {
        QFETCH(QString, missing);
        QTemporaryDir dir(fixtureTemplate());
        const auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        QJsonObject record{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
                           {"bindingPort",48991},{"ready",true},{"granted",true}};
        if (missing != "address") record.remove(missing);
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",
            {{"version",1},{"peers",QJsonObject{{"a",record}}}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture",missing == "address" ? QString() : QString("127.0.0.1"));
        QTRY_COMPARE(manager.states().value("a").toString(),QString("error"));
        QVERIFY(!manager.busy());
        QVERIFY(manager.sessions().first().toMap().value("error").toString().contains("binding"));
    }
    void blockedRecallDiscardsActivation() {
        QTemporaryDir dir(fixtureTemplate());
        const auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",QJsonObject{
            {"a",QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
                             {"bindingPort",48991},{"ready",true},{"granted",true}}}}}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture","127.0.0.1","activation");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        QTRY_VERIFY(manager.sessions().first().toMap().value("visible").toBool());
        for (bool sleeping : {false, true}) {
            manager.setLifecycleBlocked(sleeping,!sleeping);
            QTRY_VERIFY(!manager.sessions().first().toMap().value("visible").toBool());
            manager.setActivationToken("blocked-token",true);
            QVERIFY(!manager.recall());
            manager.setLifecycleBlocked(false,false);
            QSignalSpy traffic(&manager,&MultiSessions::trafficChanged);
            manager.select("a");
            QTRY_VERIFY(manager.sessions().first().toMap().value("visible").toBool());
            QTRY_VERIFY(!traffic.isEmpty());
            QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),0);
            QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),0);
        }
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void presentationFailureSurvivesWorkerShutdown() {
        QTemporaryDir dir(fixtureTemplate());
        const auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A"), remote=credential("TEST_CERT_B");
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",QJsonObject{
            {"a",QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(remote)},
                             {"bindingPort",48991},{"ready",true},{"granted",true}}}}}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture","127.0.0.1","error-on-disconnect");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        QVERIFY(manager.presentation());
        QVERIFY(QMetaObject::invokeMethod(manager.presentation(), "failed", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("Canvas fixture failed"))));
        // A disconnect while that shutdown is in progress keeps its reason.
        manager.disconnectSession("a");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("error"));
        QCOMPARE(manager.sessions().first().toMap().value("error").toString(),QString("Canvas fixture failed"));
        manager.open("a","Fixture","127.0.0.1","error-on-disconnect");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        manager.disconnectSession("a");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("disconnected"));
        QVERIFY(manager.sessions().first().toMap().value("error").toString().isEmpty());
    }
    void connectingCanvasAndTrayStayResponsive() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records;
        for (auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        QWindow window; window.resize(900,600);
        MultiSessions manager(&peers,cert,key); manager.setCanvasWindow(&window);
        connect(&manager,&MultiSessions::changed,&host,[&] { host.updateSessionMenu(manager.sessions()); });
        connect(&host,&HostManager::trayMenuAboutToShow,&manager,[&] { host.updateSessionMenu(manager.sessions()); });
        connect(&host,&HostManager::showDevicesRequested,&manager,[&] { manager.showDevices(); window.show(); });
        connect(&host,&HostManager::sessionSelected,&manager,&MultiSessions::select);
        const auto visible = [&](QString id) {
            for (auto item : manager.sessions()) if (item.toMap().value("id") == id) return item.toMap().value("visible").toBool();
            return false;
        };
        int ticks=0; qint64 longestTick=0;
        QElapsedTimer tick; tick.start();
        QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat,&QTimer::timeout,this,[&] { ++ticks; longestTick=qMax(longestTick,tick.restart()); });
        heartbeat.start();

        manager.open("a","Connecting fixture","127.0.0.1","connecting-slow");
        // No event loop turn, process or IPC handshake is needed to expose the
        // connection UI, and it already counts as a busy session for recall.
        QVERIFY(manager.busy());
        QVERIFY(manager.canvasVisible());
        QVERIFY(window.isVisible());
        for (int i=0;i<3;++i) {
            window.hide();
            QVERIFY(!manager.canvasVisible());
            QVERIFY(manager.recall());
            QVERIFY(manager.canvasVisible());
            QVERIFY(window.isVisible());
        }
        QTest::qWait(100);
        QCOMPARE(manager.states().value("a").toString(),QString("starting"));
        QVERIFY(manager.selectedTraffic().isEmpty()); // The worker has not authenticated yet.

        QMenu* menu=nullptr; QAction* devices=nullptr;
        for (auto widget : QApplication::topLevelWidgets()) if (auto candidate=qobject_cast<QMenu*>(widget)) {
            for (auto action : candidate->actions()) if (action->text()==HostManager::tr("Open device list")) {
                menu=candidate; devices=action;
            }
        }
        QVERIFY(menu); QVERIFY(devices);
        QSignalSpy menuShown(menu,&QMenu::aboutToShow);
        menu->popup(QPoint(20,20));
        QCOMPARE(menuShown.count(),1);
        QVERIFY(menu->isVisible());
        QTest::qWait(60);
        QVERIFY(ticks>=5);
        menu->hide(); devices->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.canvasVisible(),150);
        QVERIFY(window.isVisible());
        QCOMPARE(manager.states().value("a").toString(),QString("starting"));

        // An authenticated worker that never sends readiness/focus still must
        // not be needed for the shell's show/hide decisions.
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),1);
        QVERIFY(!manager.canvasVisible());
        QVERIFY(manager.recall());
        QVERIFY(window.isVisible());
        QVERIFY(manager.canvasVisible());
        QTRY_VERIFY(visible("a"));
        QCOMPARE(manager.states().value("a").toString(),QString("starting"));
        QVERIFY(!manager.viewerInFront());
        window.hide();
        QVERIFY(manager.recall());
        QVERIFY(window.isVisible()); // A's hidden acknowledgement is still pending.
        QVERIFY(manager.canvasVisible());
        manager.showDevices(); window.hide();
        QTest::qWait(400);
        QVERIFY(!window.isVisible());
        QVERIFY(!manager.canvasVisible());
        manager.reconnect("a"); // A late ready/connected result stays in the background.
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        QVERIFY(!window.isVisible());
        QVERIFY(!manager.canvasVisible());

        QVERIFY(manager.recall()); QTRY_VERIFY(visible("a"));
        manager.open("b","Next connecting fixture","127.0.0.1","connecting-crash-slow");
        // B is selected locally immediately; A retains input ownership until
        // its delayed hide acknowledgement, and B has no IPC socket yet.
        QCOMPARE(manager.selectedId(),QString("b"));
        QVERIFY(window.isVisible()); QVERIFY(manager.canvasVisible());
        QVERIFY(!visible("b"));
        window.hide();
        QVERIFY(manager.recall());
        QVERIFY(window.isVisible()); QVERIFY(manager.canvasVisible());
        QVERIFY(!visible("b"));
        QTRY_VERIFY(visible("b"));
        QVERIFY(!visible("a"));
        QCOMPARE(manager.states().value("b").toString(),QString("starting"));
        QVERIFY2(longestTick<150,qPrintable(QString("UI heartbeat gap while connecting: %1 ms").arg(longestTick)));
        qInfo("UI heartbeat longest gap while connecting: %lld ms",static_cast<long long>(longestTick));
        QSignalSpy devicesRequested(&manager,&MultiSessions::devicesRequested);
        window.hide();
        manager.reconnect("b"); // Failure before the delayed hidden acknowledgement.
        QTRY_COMPARE(manager.states().value("b").toString(),QString("error"));
        QCOMPARE(devicesRequested.count(),0);
        QVERIFY(!window.isVisible()); QVERIFY(!manager.canvasVisible());
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void presentingCanvasPreservesWindowState() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        const QJsonObject records{{"a", QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}}}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        QWindow window; window.resize(900,600); window.showMaximized();
        MultiSessions manager(&peers,cert,key); manager.setCanvasWindow(&window);
        const auto root=window.winId();
        manager.open("a","Fixture","127.0.0.1","recover");
        QTRY_VERIFY(manager.selectedTraffic().value("sent").toInt() >= 1);
        const auto presentation = manager.presentation(); QVERIFY(presentation);
        QCOMPARE(window.windowState(),Qt::WindowMaximized);
        for (auto state : {Qt::WindowFullScreen,Qt::WindowMaximized,Qt::WindowNoState}) {
            window.setWindowState(state);
            manager.showDevices();
            QCOMPARE(manager.presentation(), presentation);
            QVERIFY(!manager.canvasVisible());
            QTest::qWait(100);
            QVERIFY(manager.recall());
            QCOMPARE(manager.presentation(), presentation);
            QTest::qWait(100);
            QCOMPARE(window.windowState(),state);
            QCOMPARE(window.winId(),root);
            QVERIFY(window.isVisible());
        }
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
        QVERIFY(!manager.presentation());
    }
    void explicitFrameRefreshRetainsCanvasAndReleasesInput() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr, dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        const QJsonObject records{{"a", QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}}}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        QQuickWindow window; window.resize(900,600);
        MultiSessions manager(&peers,cert,key); manager.setCanvasWindow(&window);
        const auto releaseFile = dir.path()+"/input-released";
        qputenv("DESKPORT_TEST_CANVAS_RELEASE_FILE", releaseFile.toUtf8());
        manager.open("a","Fixture","127.0.0.1","frame-refresh");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        qunsetenv("DESKPORT_TEST_CANVAS_RELEASE_FILE");
        const auto presentation=manager.presentation(); QVERIFY(presentation);
        QJsonObject state{{"width",900},{"height",600},{"scale",1.0},{"visible",true},{"focused",true}};
        manager.updateCanvas(state);
        QTRY_VERIFY(manager.selectedTraffic().contains("received"));
        const int initialReleases=manager.selectedTraffic().value("received").toInt();
        for (int i=0; i<10; ++i) {
            QFile::remove(releaseFile);
            manager.setCanvasWindowFrameless(i % 2 == 0);
            // Deliberately stop servicing the shell event loop, as AppKit does
            // during a native drag. The independent worker must see release.
            QElapsedTimer nativeLoop; nativeLoop.start();
            while (!QFile::exists(releaseFile) && nativeLoop.elapsed() < 500) QThread::msleep(10);
            QVERIFY(QFile::exists(releaseFile));
            QCOMPARE(window.size(),QSize(900,600));
            QVERIFY(window.isVisible()); QVERIFY(manager.canvasVisible());
            QCOMPARE(manager.presentation(),presentation);
            QCOMPARE(manager.selectedId(),QString("a"));
            manager.updateCanvas(state); // The real canvas publishes this after re-exposure.
            QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),initialReleases+i+1);
            QCOMPARE(manager.selectedTraffic().value("sent").toInt(),0); // No worker hide/restart.
        }
        QTest::qWait(200); // Finish the last native surface configure.
        for (auto visibility : {QWindow::FullScreen,QWindow::Maximized,QWindow::Minimized}) {
            window.showNormal();
            QTest::qWait(200);
            window.setVisibility(visibility);
            QTest::qWait(100);
            const auto settledVisibility = window.visibility();
            // This isolated Qt/KWin fixture does not always report minimized
            // visibility; preserve the Qt state observed before the change.
            if (!WindowFrame::nativeWayland() || visibility != QWindow::Minimized)
                QCOMPARE(settledVisibility, visibility);
            const bool keepsSurface = !WindowFrame::requiresSurfaceRefresh(&window);
            if (WindowFrame::nativeWayland() && visibility != QWindow::Minimized)
                QVERIFY(keepsSurface);
            manager.updateCanvas(state);
            QTest::qWait(80); // Drain the preceding state publication.
            const int releases = manager.selectedTraffic().value("received").toInt();
            QFile::remove(releaseFile);
            manager.setCanvasWindowFrameless(!window.flags().testFlag(Qt::FramelessWindowHint));
            QCOMPARE(window.visibility(),settledVisibility);
            QCOMPARE(manager.presentation(),presentation);
            if (keepsSurface) {
                // A real canvas suppresses an unchanged viewport. No later
                // publication is available to undo a spurious focus release.
                QTest::qWait(150);
                QVERIFY(!QFile::exists(releaseFile));
                QCOMPARE(manager.selectedTraffic().value("received").toInt(), releases);
            }
        }
        window.hide();
        QVERIFY(!manager.canvasVisible());
        manager.setCanvasWindowFrameless(!window.flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!window.isVisible()); QVERIFY(!manager.canvasVisible());
        QTest::qWait(150); // A frame refresh must not have a deferred re-show.
        QVERIFY(!window.isVisible());
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void shortcutConfigurationReachesNewAndExistingWorkers() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        const QJsonObject records{{"a", QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}}}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.setShortcutEnabled(true,"Ctrl+Alt+K");
        manager.open("a","Fixture","127.0.0.1","shortcut-config");
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),1);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        manager.setShortcutEnabled(false,"Meta+F8");
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),2);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),0);
        manager.setShortcutEnabled(true,"Meta+F8");
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void concurrentWorkersAndInputHandoff() {
        QTemporaryDir dir(fixtureTemplate());
        auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        auto remote=credential("TEST_CERT_B");
        QJsonObject records;
        for(auto id : {"a","b","c"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(remote)},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        const auto identity=SessionGraph::identity(QSslCertificate(cert));
        auto row=[&](QString id) { for(const auto& item:manager.sessions()) if(item.toMap().value("id")==id)return item.toMap(); return QVariantMap{}; };
        QSignalSpy shown(&manager,&MultiSessions::viewerShown);
        manager.open("a","First","127.0.0.1","slow");
        QTRY_VERIFY2(row("a").value("state").toString()=="connected", qPrintable(row("a").value("error").toString()));
        QTRY_VERIFY(row("a").value("visible").toBool());
        manager.open("b","Second","127.0.0.1","crash");
        int ticks=0; QTimer heartbeat; heartbeat.setInterval(10); connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;}); heartbeat.start();
        QTest::qWait(100);
        QVERIFY(!row("b").value("visible").toBool()); // A has not released input yet.
        QVERIFY(ticks>=5);
        QTRY_VERIFY(row("b").value("visible").toBool());
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(SessionGraph::branches(identity).first.size(),2);
        manager.fullscreen("a");
        QTRY_VERIFY(row("a").value("visible").toBool());
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        // The worker's host-capped enlargement reaches the device settings.
        QTRY_COMPARE(manager.tuningLimited(),QStringList{"a"});
        // Tuning goes to the running stream as a value, not as a reconnect.
        manager.setDesktopAdjustment("A",1.5);
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),150);
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        // Rapid switching supersedes older hide acknowledgements.
        for(int i=0;i<12;++i) { manager.select(i%2 ? "a":"b"); QTest::qWait(5); }
        manager.select("b"); QTRY_VERIFY(row("b").value("visible").toBool());
        QVERIFY(!row("a").value("visible").toBool());
        manager.showDevices();
        QTRY_VERIFY(!row("b").value("visible").toBool());
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(row("b").value("state").toString(),QString("connected"));
        // One crash cannot tear down the other connection or its graph edge.
        manager.reconnect("b");
        QTRY_COMPARE(row("b").value("state").toString(),QString("error"));
        QCOMPARE(row("a").value("state").toString(),QString("connected"));
        QCOMPARE(SessionGraph::branches(identity).first.size(),1);
        manager.select("a"); QTRY_VERIFY(row("a").value("visible").toBool());
        manager.open("c","Third","127.0.0.1");
        QTRY_COMPARE(row("c").value("state").toString(),QString("connected"));
        QTRY_VERIFY(row("c").value("visible").toBool());
        QSignalSpy devices(&manager,&MultiSessions::devicesRequested);
        manager.disconnectSession("a");
        QTRY_COMPARE(row("a").value("state").toString(),QString("disconnected"));
        QVERIFY(manager.tuningLimited().isEmpty());
        QCOMPARE(devices.count(),0);
        QVERIFY(row("c").value("visible").toBool());
        QCOMPARE(row("c").value("state").toString(),QString("connected"));
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
        QCOMPARE(SessionGraph::branches(identity).first.size(),0);
    }
    void unresponsiveWorkerDoesNotBlockShellOrNextViewer() {
        QTemporaryDir dir(fixtureTemplate());
        auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        auto remote=credential("TEST_CERT_B");
        QJsonObject records;
        for(auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(remote)},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        auto visible=[&](QString id) { for (auto item:manager.sessions()) if(item.toMap().value("id")==id)return item.toMap().value("visible").toBool(); return false; };
        manager.open("a","Hung viewer","127.0.0.1","hung");
        QTRY_VERIFY(visible("a"));
        manager.open("b","Healthy viewer","127.0.0.1");
        int ticks=0; qint64 longestTick=0;
        QElapsedTimer elapsed; elapsed.start();
        QTimer heartbeat; heartbeat.setInterval(10);
        connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks; longestTick=qMax(longestTick,elapsed.restart());}); heartbeat.start();
        for (int i=0;i<15;++i) { manager.select("b"); QTest::qWait(100); }
        QVERIFY(ticks>=75);
        QVERIFY2(longestTick<100,qPrintable(QString("UI heartbeat gap: %1 ms").arg(longestTick)));
        qInfo("UI heartbeat longest gap during stalled handoff: %lld ms", static_cast<long long>(longestTick));
        QVERIFY(!visible("b"));
        QTRY_COMPARE_WITH_TIMEOUT(manager.states().value("a").toString(),QString("error"),1500);
        QTRY_VERIFY(visible("b"));
        QCOMPARE(manager.states().value("b").toString(),QString("connected"));
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void hiddenViewersDoNotDelaySwitchingAndNextCycles() {
        QTemporaryDir dir(fixtureTemplate());
        auto cert=credential("TEST_CERT_A"), key=credential("TEST_KEY_A");
        auto remote=credential("TEST_CERT_B");
        QJsonObject records;
        for(auto id : {"a","b","c"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(remote)},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        auto visible=[&](QString id) { for (auto item:manager.sessions()) if(item.toMap().value("id")==id)return item.toMap().value("visible").toBool(); return false; };
        manager.open("a","Lazy background","127.0.0.1","lazy");
        QTRY_VERIFY(visible("a"));
        manager.open("b","Second","127.0.0.1","next");
        QTRY_VERIFY(visible("b"));
        // A is hidden and ignores further hides; switching must not wait for or kill it.
        QElapsedTimer handoff; handoff.start();
        manager.open("c","Third","127.0.0.1","next");
        QTRY_VERIFY_WITH_TIMEOUT(visible("c"),1000);
        QVERIFY2(handoff.elapsed()<1000,qPrintable(QString("handoff took %1 ms").arg(handoff.elapsed())));
        manager.showDevices(); QTRY_VERIFY(!visible("c"));
        manager.showDevices(); // Idempotent: nothing is presenting.
        manager.select("c"); QTRY_VERIFY(visible("c"));
        QTest::qWait(2300);
        QCOMPARE(manager.states().value("a").toString(),QString("connected"));
        // Background viewers cannot switch presentation.
        manager.reconnect("b"); QTest::qWait(200);
        QVERIFY(visible("c"));
        // The presenting viewer's chord cycles in connection order.
        manager.reconnect("c"); QTRY_VERIFY(visible("a"));
        manager.selectNext(); QTRY_VERIFY(visible("b"));
        QCOMPARE(manager.states().value("a").toString(),QString("connected"));
        // Rows, next and recall follow the device list's saved order.
        QSettings().setValue("ui/hostLayout",QVariantList{QVariantMap{{"type","device"},{"id","c"}},
            QVariantMap{{"type","group"},{"id","g"},{"name","Group"},{"devices",QStringList{"b","a"}}}});
        QStringList order; for (auto item:manager.sessions()) order << item.toMap().value("id").toString();
        QCOMPARE(order,QStringList({"c","b","a"}));
        manager.selectNext(); QTRY_VERIFY(visible("a")); // b → a
        manager.selectNext(); QTRY_VERIFY(visible("c")); // wraps to the first
        QSettings().remove("ui/hostLayout");
        manager.select("b"); QTRY_VERIFY(visible("b"));
        manager.disconnectSession("c"); QTRY_COMPARE(manager.states().value("c").toString(),QString("disconnected"));
        manager.selectNext(); QTRY_VERIFY(visible("a")); // Skips the ended session.
        // Recall prefers the last viewed desktop, then the first connected one.
        manager.showDevices(); QTRY_VERIFY(!visible("a"));
        QVERIFY(manager.recall()); QTRY_VERIFY(visible("a"));
        manager.disconnectSession("a"); QTRY_COMPARE(manager.states().value("a").toString(),QString("disconnected"));
        QVERIFY(manager.recall()); QTRY_VERIFY(visible("b"));
        manager.disconnectSession("b"); QTRY_COMPARE(manager.states().value("b").toString(),QString("disconnected"));
        QVERIFY(!manager.recall());
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void backgroundRecoveryNeverRaisesTheViewer() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records{{"a",QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}}}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture","127.0.0.1","recover");
        // Opening raises; the later restart's hide and ready only restore state.
        QTRY_VERIFY(manager.selectedTraffic().value("sent").toInt() >= 1);
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),2);
        const int opened = manager.selectedTraffic().value("sent").toInt();
        QTest::qWait(200);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),opened);
        // A user's explicit switch still raises.
        manager.showDevices(); manager.select("a");
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),opened+1);
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),2);
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void repeatedHideRecallKeepsOneInputOwnerAndAllConnections() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records;
        for (auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        const auto lease=dir.path()+"/synthetic-input-owner";
        qputenv("TEST_SESSION_INPUT_LEASE",lease.toUtf8());
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        auto visible=[&](QString id) { for(auto item:manager.sessions()) if(item.toMap().value("id")==id) return item.toMap().value("visible").toBool(); return false; };
        manager.open("a","Fixture A","127.0.0.1","slow"); QTRY_VERIFY(visible("a"));
        manager.open("b","Fixture B","127.0.0.1","slow"); QTRY_VERIFY(visible("b"));
        for (int i=0;i<10;++i) {
            const QString target=i%2 ? "b":"a";
            manager.showDevices(); manager.showDevices();
            manager.select(target); manager.select(target);
            QTRY_VERIFY(visible(target));
            QCOMPARE(manager.states().value("a").toString(),QString("connected"));
            QCOMPARE(manager.states().value("b").toString(),QString("connected"));
        }
        manager.showDevices(); QTRY_VERIFY(!visible("a") && !visible("b"));
        QVERIFY(manager.busy()); // Closing the last viewer retains both workers.
        QVERIFY(manager.recall()); QTRY_VERIFY(visible("b"));
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
        QVERIFY(!QFile::exists(lease)); // All fixture input leases released on Quit.
        qunsetenv("TEST_SESSION_INPUT_LEASE");
    }
    void suspensionParksWorkerHideDeadline() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records{{"a",QJsonObject{{"hostId","a"},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}}}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Frozen fixture","127.0.0.1","hung");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        QTRY_VERIFY(manager.sessions().first().toMap().value("visible").toBool());
        QSignalSpy devices(&manager,&MultiSessions::devicesRequested);
        manager.setLifecycleBlocked(true,false);
        QTest::qWait(2300);
        QCOMPARE(manager.states().value("a").toString(),QString("connected"));
        manager.setLifecycleBlocked(false,false);
        QTest::qWait(200);
        QCOMPARE(manager.states().value("a").toString(),QString("connected"));
        // A genuinely unresponsive worker still has a bounded deadline after wake.
        QTRY_COMPARE_WITH_TIMEOUT(manager.states().value("a").toString(),QString("error"),3000);
        QCOMPARE(devices.count(),0); // Failure on wake also stays in the background.
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
    void powerAndLockKeepConnectionIntentWithoutPresentation() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records;
        for (auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        auto visible=[&] { for(auto item:manager.sessions()) if(item.toMap().value("visible").toBool())return true; return false; };
        manager.open("a","First","127.0.0.1");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("connected"));
        manager.open("b","Second","127.0.0.1");
        QTRY_COMPARE(manager.states().value("b").toString(),QString("connected"));
        QTRY_VERIFY(visible());
        QSignalSpy shown(&manager,&MultiSessions::viewerShown);
        for (int i=0;i<3;++i) manager.setLifecycleBlocked(true,true);
        QTRY_VERIFY(!visible());
        manager.select("b"); manager.open("c","Blocked","127.0.0.1");
        QVERIFY(!manager.states().contains("c"));
        manager.disconnectSession("a");
        QTRY_COMPARE(manager.states().value("a").toString(),QString("disconnected"));
        manager.setLifecycleBlocked(false,true); // Wake still locked.
        manager.select("b"); QTest::qWait(100); QVERIFY(!visible());
        manager.setLifecycleBlocked(false,false);
        QTest::qWait(100); QVERIFY(!visible()); QCOMPARE(shown.count(),0);
        QCOMPARE(manager.states().value("b").toString(),QString("connected"));
        QCOMPARE(manager.states().value("a").toString(),QString("disconnected"));
        manager.select("b"); QTRY_VERIFY(visible()); // Explicit recall only.
        manager.setLifecycleBlocked(true,false);
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
        manager.setLifecycleBlocked(false,false); QTest::qWait(100);
        QVERIFY(!manager.busy()); QVERIFY(!visible());
    }
    void cancellationBeforeLaunch() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        manager.open("a","Fixture","127.0.0.1"); manager.disconnectSession("a");
        QTest::qWait(100); QVERIFY(!manager.busy());
        QCOMPARE(manager.sessions().first().toMap().value("state").toString(),QString("disconnected"));
    }
    void shortcutActivationIsOneShotAndCancelledByDevices() {
        QTemporaryDir dir(fixtureTemplate()); HostManager host(nullptr,dir.path()+"/host");
        const auto cert=credential("TEST_CERT_A"),key=credential("TEST_KEY_A");
        QJsonObject records;
        for (auto id : {"a","b"}) records[id]=QJsonObject{{"hostId",id},{"clientCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"bindingPort",48991},{"ready",true},{"granted",true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json",{{"version",1},{"peers",records}}));
        PeerManager peers(&host,cert,key,dir.path()+"/binding",0,QHostAddress::LocalHost);
        MultiSessions manager(&peers,cert,key);
        QSignalSpy shown(&manager,&MultiSessions::viewerShown);
        manager.open("a","Fixture","127.0.0.1","activation");
        QTRY_VERIFY(!shown.isEmpty());
        manager.setActivationToken("fixture-token",true);
        QVERIFY(manager.recall());
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),1);
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        int count=shown.count();
        QVERIFY(manager.recall()); QTRY_VERIFY(shown.count()>count);
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),1);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        manager.setActivationToken({},true); // KDE's tokenless portal gesture.
        QVERIFY(manager.recall());
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),2);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        manager.setActivationToken("cancelled-token",true);
        manager.showDevices();
        count=shown.count();
        QVERIFY(manager.recall()); QTRY_VERIFY(shown.count()>count);
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),2);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),1);
        manager.setActivationToken("tray-token");
        QVERIFY(manager.recall());
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),2);
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),2);
        const auto visible = [&](QString id) {
            for (auto item : manager.sessions()) if (item.toMap().value("id") == id) return item.toMap().value("visible").toBool();
            return false;
        };
        manager.open("b","Slow fixture","127.0.0.1","activation-slow");
        QTRY_VERIFY(visible("b"));
        manager.select("a"); // The user action waits for B to release input.
        manager.setActivationToken("delayed-token",true);
        QVERIFY(manager.recall());
        QTRY_VERIFY(visible("a"));
        QTRY_COMPARE(manager.selectedTraffic().value("received").toInt(),3);
        QTRY_COMPARE(manager.selectedTraffic().value("sent").toInt(),3);
        manager.select("b"); QTRY_VERIFY(visible("b"));
        manager.select("a");
        manager.setActivationToken("superseded-token",true);
        QVERIFY(manager.recall());
        manager.select("b"); // A later selection cannot inherit A's action.
        QTest::qWait(400);
        QTRY_VERIFY(visible("b"));
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),0);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),0);
        manager.select("a");
        manager.setActivationToken("disconnected-token",true);
        QVERIFY(manager.recall());
        manager.disconnectSession("a");
        manager.select("b"); QTest::qWait(400);
        QTRY_VERIFY(visible("b"));
        QCOMPARE(manager.selectedTraffic().value("received").toInt(),0);
        QCOMPARE(manager.selectedTraffic().value("sent").toInt(),0);
        manager.shutdown(); QTRY_VERIFY(!manager.busy());
    }
};
int main(int argc,char**argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("DeskPortTests"); QCoreApplication::setApplicationName("MultiSessions");
    if(app.arguments().value(1)=="--session-worker")return worker(app);
    MultiSessionsTest test; return QTest::qExec(&test,argc,argv);
}
#include "multi-sessions.moc"
