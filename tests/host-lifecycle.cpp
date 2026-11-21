#include "diagnostics.h"
#include <QtTest>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "hostmanager.h"
#include "nvaddress.h"
#include "localhostfilter.h"
#include "peerstore.h"
#ifdef Q_OS_MACOS
#include "macdock.h"
#include <CoreFoundation/CoreFoundation.h>
#include <QSettings>
#include <QWindow>
#endif
#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef Q_OS_LINUX
namespace {
qint64 fixtureChildPid(const QString& path) {
    QFile pidFile(path);
    return pidFile.open(QIODevice::ReadOnly) ? pidFile.readAll().trimmed().toLongLong() : 0;
}
bool fixtureChildExited(const QString& path) {
    const auto pid = fixtureChildPid(path);
    if (pid <= 1) return false;
    QFile status(QString("/proc/%1/stat").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) return true;
    const auto bytes = status.readAll();
    const auto state = bytes.indexOf(") ") + 2;
    return state > 1 && state < bytes.size() && bytes.at(state) == 'Z';
}
class FixtureSubreaper {
public:
    explicit FixtureSubreaper(const QString& directory) : directory(directory) {
        ::prctl(PR_GET_CHILD_SUBREAPER, &previous);
        ::prctl(PR_SET_CHILD_SUBREAPER, 1);
    }
    ~FixtureSubreaper() {
        for (const auto& name : {"auth-pid", "host-pid", "display-pid"}) {
            QFile file(directory + "/" + name);
            if (!file.open(QIODevice::ReadOnly)) continue;
            const auto pid = pid_t(file.readAll().trimmed().toLongLong());
            if (pid <= 1) continue;
            // Clean up only a fixture child adopted by this test process.
            int status;
            if (::waitpid(pid, &status, WNOHANG) == 0) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, 0);
            }
        }
        ::prctl(PR_SET_CHILD_SUBREAPER, previous);
    }
private:
    QString directory;
    int previous = 0;
};
}
#endif

static QAction* trayAction(const QString& title) {
    for (auto widget : QApplication::topLevelWidgets())
        if (auto menu=qobject_cast<QMenu*>(widget))
            for (auto action : menu->actions()) if (action->isVisible() && action->text()==title) return action;
    return nullptr;
}

#ifdef Q_OS_MACOS
// Offscreen only: reproduce the resident window's close-to-hide veto without
// creating a status item, opening a menu or sending any native mouse events.
class CloseToHideWindow : public QWindow {
public:
    int closes = 0;
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Close) {
            ++closes;
            event->ignore();
            hide();
            return true;
        }
        return QWindow::event(event);
    }
};

static int explicitExitProbe(QApplication& app, bool restart) {
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QCoreApplication::setOrganizationName("DeskPortExitTest");
    QCoreApplication::setApplicationName("DeskPortExitTest");
    app.setQuitOnLastWindowClosed(false);
    CloseToHideWindow window;
    window.create();
    const bool closed = window.close();
    if (closed || window.closes != 1) {
        qWarning() << "Exit probe close-veto setup failed: closed=" << closed << "closes=" << window.closes;
        return 81;
    }
    window.show(); // Keep a visible close veto in place for the eventual exit.
    qputenv("DESKPORT_TEST_MODE", "stubborn");
    HostManager host(nullptr, dir.path(), false);
    host.setResident(true);
    int hides = 0, exits = 0, aboutToQuit = 0, ticksAfterExit = 0;
    bool requested = false, drained = false;
    QObject::connect(&host, &HostManager::hideRequested, &app, [&] { ++hides; });
    QObject::connect(&host, &HostManager::exitRequested, &app, [&] {
        ++exits;
        // Explicit authorization alone must not bypass the host drain.
        host.completeExit();
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        ++aboutToQuit;
    });
    QTimer::singleShot(0, &app, [&] {
        host.completeExit(); // Neither authorized nor running: must be a no-op.
        host.start(2560, 1440);
    });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &app, [&] {
        if (!requested && host.canPair() && QFile::exists(dir.filePath("host-started"))) {
            // Ordinary resident Quit still hides, and cannot authorize exit.
            QEvent quit(QEvent::Quit);
            QCoreApplication::sendEvent(&app, &quit);
            if (hides != 1 || exits) { app.exit(82); return; }
            host.completeExit();
            auto action = trayAction(restart ? "Restart DeskPort" : "Quit");
            if (!action) { app.exit(83); return; }
            requested = true;
            action->trigger();
            if (exits) app.exit(84); // Tray actions must return before dispatch.
        } else if (exits) {
            ++ticksAfterExit;
            if (!host.running()) {
                // Snapshot before aboutToQuit: HostManager's earlier handler
                // calls beginStop() again and temporarily sets m_Stopping.
                drained = exits == 1 && !host.canPair();
                host.completeExit();
            }
        }
    });
    poll.start(10);
    QTimer::singleShot(10000, &app, [&] { app.exit(85); });
    const int result = app.exec();
    const bool restartMarker = QSettings().value("ui/showAfterRestart", false).toBool();
    const bool displayClosed = QFile::exists(dir.filePath("display-closed"));
    const auto fail = [&](int code) {
        qWarning() << "Exit probe failed: code=" << code << "restart=" << restart
                   << "requested=" << requested << "drained=" << drained
                   << "hides=" << hides << "exits=" << exits << "aboutToQuit=" << aboutToQuit
                   << "closes=" << window.closes << "windowVisible=" << window.isVisible()
                   << "ticksAfterExit=" << ticksAfterExit << "running=" << host.running()
                   << "canPair=" << host.canPair() << "restarting=" << host.restarting()
                   << "restartMarker=" << restartMarker << "displayClosed=" << displayClosed;
        return code;
    };
    if (result) return fail(result);
    if (!drained || aboutToQuit != 1 || window.closes != 1 || ticksAfterExit < 10) return fail(86);
    if (host.restarting() != restart || restartMarker != restart) return fail(87);
    if (!displayClosed) return fail(88);
    return 0;
}
#endif

class HostLifecycle : public QObject {
    Q_OBJECT
private slots:
    void init() {
        qputenv("DESKPORT_TEST_MODE", "normal");
        qunsetenv("DESKPORT_TEST_HYPR_POLICY");
#ifdef Q_OS_LINUX
        qputenv("WAYLAND_DISPLAY", "deskport-isolated-fake");
        qputenv("XDG_SESSION_TYPE", "wayland");
        qunsetenv("DESKPORT_X11_OUTPUT");
#endif
    }
    void explicitX11SelectionNeverFallsBack_data() {
        QTest::addColumn<bool>("wayland");
        QTest::newRow("missing-display") << false;
        QTest::newRow("wayland-session") << true;
    }
    void explicitX11SelectionNeverFallsBack() {
#ifndef Q_OS_LINUX
        QSKIP("Linux X11 adapter");
#else
        QFETCH(bool, wayland);
        qputenv("DESKPORT_TEST_MODE", "x11-unavailable");
        if (!wayland) { qunsetenv("WAYLAND_DISPLAY"); qputenv("XDG_SESSION_TYPE", "x11"); }
        qunsetenv("DISPLAY"); qputenv("DESKPORT_X11_OUTPUT", "DUMMY1");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
        QVERIFY(!host.running()); QVERIFY(!host.adaptiveDisplayAvailable());
        QVERIFY(QFile::exists(dir.path() + "/display-pid"));
        QVERIFY(!QFile::exists(dir.path() + "/auth-pid"));
        QVERIFY(!QFile::exists(dir.path() + "/host-pid"));
        QVERIFY(host.status().contains("unavailable"));
#endif
    }
    void waylandSessionWithoutDisplayVariableKeepsPortalCapture() {
#ifndef Q_OS_LINUX
        QSKIP("Linux session detection");
#else
        qunsetenv("WAYLAND_DISPLAY"); qputenv("DISPLAY", ":99999");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720); QTRY_VERIFY_WITH_TIMEOUT(host.running(), 5000);
        QVERIFY(!host.adaptiveDisplayAvailable());
        QVERIFY(!QFile::exists(dir.path() + "/display-pid"));
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("capture = portal\n"));
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
#endif
    }
    void x11UnselectedRetainsExistingCaptureWithoutAdaptiveLease() {
#ifndef Q_OS_LINUX
        QSKIP("Linux X11 adapter");
#else
        qunsetenv("WAYLAND_DISPLAY"); qputenv("XDG_SESSION_TYPE", "x11"); qputenv("DISPLAY", ":99999");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720); QTRY_VERIFY_WITH_TIMEOUT(host.running(), 5000);
        QVERIFY(!host.adaptiveDisplayAvailable());
        QVERIFY(!QFile::exists(dir.path() + "/display-pid"));
        QVERIFY(!QFile::exists(dir.path() + "/virtual-display.json"));
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("capture = x11\n"));
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
#endif
    }
    void x11LeaseUsesNamedCaptureWithoutTopologyPolicies() {
#ifndef Q_OS_LINUX
        QSKIP("Linux X11 adapter");
#else
        qputenv("DESKPORT_TEST_MODE", "x11-idle");
        qunsetenv("WAYLAND_DISPLAY"); qputenv("XDG_SESSION_TYPE", "x11"); qputenv("DISPLAY", ":99999");
        qputenv("DESKPORT_X11_OUTPUT", "DUMMY1");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QVERIFY(!host.displayPoliciesAvailable());
        QVERIFY(!host.resizeDisplay(1280, 720, 1, 6, 2));
        const auto path = dir.path() + "/virtual-display.json";
        QVERIFY(!QFile::exists(path));
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        const auto text = config.readAll();
        QVERIFY(text.contains("capture = x11\n")); QVERIFY(text.contains("output_name = DUMMY1\n"));
        QSignalSpy results(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1668, 2388, 1, 7));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
        const auto state = PeerStore::read(path);
        QCOMPARE(state["output"].toString(), QString("DUMMY1"));
        QCOMPARE(state["leaseToken"].toString(), QString("DeskPort-00000000-0000-0000-0000-000000000000"));
        QCOMPARE(state["width"].toInt(), 1668);
        host.restoreDisplay(); QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 5000);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
#endif
    }
    void parentCrashReleasesOwnedSunshineChildren_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::addColumn<QString>("pidFile");
        QTest::newRow("authentication") << QByteArray("auth-slow") << QString("auth-pid");
        QTest::newRow("server-ignores-sigterm") << QByteArray("stubborn") << QString("host-pid");
    }
    void parentCrashReleasesOwnedSunshineChildren() {
#ifndef Q_OS_LINUX
        QSKIP("Linux owned-child parent-death signal");
#else
        QFETCH(QByteArray, mode); QFETCH(QString, pidFile);
        QTemporaryDir state; FixtureSubreaper children(state.path());
        QProcess owner;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DESKPORT_TEST_MODE", QString::fromUtf8(mode));
        environment.insert("DESKPORT_TEST_CRASH_OWNER", state.path());
        owner.setProcessEnvironment(environment);
        owner.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(owner.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.filePath(pidFile.section('-', 0, 0) + "-started")), 5000);
        owner.kill(); QVERIFY(owner.waitForFinished(3000));
        QTRY_VERIFY_WITH_TIMEOUT(fixtureChildExited(state.filePath(pidFile)), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.filePath("display-closed")), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(fixtureChildExited(state.filePath("display-pid")), 3000);
#endif
    }
    void parentCrashDuringStartupLeavesNoFixtureChildren() {
#ifndef Q_OS_LINUX
        QSKIP("Linux startup race");
#else
        for (const int delay : {0, 1, 5, 15, 50, 100}) {
            QTemporaryDir state; FixtureSubreaper children(state.path());
            QProcess owner;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("DESKPORT_TEST_CRASH_OWNER", state.path());
            environment.insert("DESKPORT_TEST_MODE", "stubborn");
            owner.setProcessEnvironment(environment);
            owner.start(QCoreApplication::applicationFilePath(), {});
            QVERIFY(owner.waitForStarted(3000));
            QTest::qWait(delay);
            owner.kill(); QVERIFY(owner.waitForFinished(3000));
            QTest::qWait(100);
            for (const auto& name : {"auth-pid", "host-pid", "display-pid"})
                if (fixtureChildPid(state.filePath(name)) > 1)
                    QTRY_VERIFY_WITH_TIMEOUT(fixtureChildExited(state.filePath(name)), 3000);
        }
#endif
    }
    void trayActionsReturnBeforeDispatch() {
        QTemporaryDir dir; HostManager host(nullptr,dir.path());
        QSignalSpy opened(&host,&HostManager::showDevicesRequested);
        QSignalSpy selected(&host,&HostManager::sessionSelected);
        QSignalSpy disconnected(&host,&HostManager::sessionDisconnectRequested);
        QSignalSpy reconnected(&host,&HostManager::sessionReconnectRequested);
        auto row=[](QString id, QString name, QString state, bool visible=false, bool selected=false) {
            return QVariantMap{{"id",id},{"name",name},{"state",state},{"visible",visible},{"selected",selected}}; };
        QVERIFY(trayAction("Open device list"));
        QVERIFY(!trayAction("Disconnect “Alpha”"));
        host.updateSessionMenu({row("a","Alpha","connected",true,true),row("b","Beta & Co","starting"),row("c","Gone","disconnected")});
        auto open=trayAction("Open device list"), alpha=trayAction("Alpha"), beta=trayAction("Beta && Co · Connecting…");
        auto disconnect=trayAction("Disconnect “Alpha”"), reconnect=trayAction("Reconnect “Alpha”");
        QVERIFY(open); QVERIFY(alpha); QVERIFY(beta); QVERIFY(disconnect); QVERIFY(reconnect);
        QVERIFY(!trayAction("Gone")); // Ended sessions are not listed.
        QVERIFY(alpha->isChecked()); QVERIFY(!beta->isChecked());
        alpha->trigger(); QVERIFY(alpha->isChecked()); // Only the viewer changes the checkmark.
        beta->trigger(); open->trigger(); disconnect->trigger(); reconnect->trigger();
        QCOMPARE(opened.size(),0); QCOMPARE(selected.size(),0);
        QCOMPARE(disconnected.size(),0); QCOMPARE(reconnected.size(),0);
        QTRY_COMPARE(opened.size(),1);
        QTRY_COMPARE(selected.size(),2);
        QCOMPARE(selected.at(1).at(0).toString(),QString("b"));
        QTRY_COMPARE(disconnected.size(),1); QCOMPARE(disconnected.first().at(0).toString(),QString("a"));
        QTRY_COMPARE(reconnected.size(),1); QCOMPARE(reconnected.first().at(0).toString(),QString("a"));
        // No viewer shown: actions address the last viewed desktop; the list is capped at seven.
        QVariantList many;
        for (int i=0;i<9;++i) many.append(row(QString::number(i),QString("Desk %1").arg(i),"connected",false,i==8));
        host.updateSessionMenu(many);
        QVERIFY(trayAction("Desk 6")); QVERIFY(!trayAction("Desk 7")); QVERIFY(!trayAction("Desk 8"));
        QVERIFY(trayAction("Disconnect “Desk 8”"));
        auto all=trayAction("All connections (9)…"); QVERIFY(all);
        all->trigger(); QTRY_COMPARE(opened.size(),2);
        host.updateSessionMenu({});
        QVERIFY(!trayAction("Desk 0")); QVERIFY(!trayAction("Disconnect “Desk 8”")); QVERIFY(!trayAction("Reconnect “Desk 8”"));
        QVERIFY(trayAction("Open device list"));
    }
#ifdef Q_OS_MACOS
    void nativeMenuWaitsForStatusButtonTracking() {
        // Exercise the production scheduler in the mode AppKit uses while a
        // status button waits for mouseUp. No synthetic input or personal UI.
        const auto loop = CFRunLoopGetMain();
        const auto tracking = CFSTR("NSEventTrackingRunLoopMode");
        CFRunLoopSourceContext sourceContext = {};
        sourceContext.perform = [](void*) {};
        const auto source = CFRunLoopSourceCreate(nullptr, 0, &sourceContext);
        CFRunLoopAddSource(loop, source, tracking);
        CFRunLoopAddSource(loop, source, kCFRunLoopDefaultMode);
        QObject owner;
        int delivered = 0;
        bool canceledDelivered = false;
        deskPortAfterStatusItemTracking(&owner, [&] { ++delivered; });
        auto canceled = new QObject;
        deskPortAfterStatusItemTracking(canceled, [&] { canceledDelivered = true; });
        delete canceled;
        const bool synchronous = delivered != 0;
        CFRunLoopRunInMode(tracking, 0.02, false);
        const bool ranDuringTracking = delivered != 0;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, false);
        CFRunLoopRemoveSource(loop, source, tracking);
        CFRunLoopRemoveSource(loop, source, kCFRunLoopDefaultMode);
        CFRelease(source);
        QVERIFY(!synchronous);
        QVERIFY(!ranDuringTracking);
        QCOMPARE(delivered, 1);
        QVERIFY(!canceledDelivered);
    }
    void explicitExitBypassesCloseToHideAfterDrain_data() {
        QTest::addColumn<bool>("restart");
        QTest::newRow("quit") << false;
        QTest::newRow("restart") << true;
    }
    void explicitExitBypassesCloseToHideAfterDrain() {
        QFETCH(bool, restart);
        QProcess probe;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DESKPORT_TEST_EXPLICIT_EXIT", restart ? "restart" : "quit");
        probe.setProcessEnvironment(environment);
        probe.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(probe.waitForStarted(3000));
        const bool finished = probe.waitForFinished(15000);
        if (!finished) { probe.kill(); probe.waitForFinished(3000); }
        QVERIFY2(finished, probe.readAllStandardError().constData());
        QCOMPARE(probe.exitStatus(), QProcess::NormalExit);
        QVERIFY2(probe.exitCode() == 0,
            qPrintable(QString("Exit probe failed (%1): %2").arg(probe.exitCode()).arg(QString::fromUtf8(probe.readAllStandardError()))));
    }
#endif
    void exitKeepsEventLoopAliveWhileHelperStops() {
        qputenv("DESKPORT_TEST_MODE","stubborn");
        QTemporaryDir dir; HostManager host(nullptr,dir.path());
        host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair() && QFile::exists(dir.filePath("host-started")),5000);
        QSignalSpy exited(&host,&HostManager::exitRequested);
        int heartbeats=0;
        QTimer heartbeat;
        connect(&heartbeat,&QTimer::timeout,this,[&] { ++heartbeats; });
        heartbeat.start(10);
        QElapsedTimer elapsed; elapsed.start();
        host.requestExit();
        QVERIFY(elapsed.elapsed()<100);
        QCOMPARE(exited.size(),1);
        QVERIFY(host.running());
        QSignalSpy opened(&host,&HostManager::showDevicesRequested);
        auto open=trayAction("Open device list"); QVERIFY(open);
        open->trigger();
        QCOMPARE(opened.size(),0);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(),1,500);
        QVERIFY(host.running());
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(),5000);
        QVERIFY(heartbeats>20);
    }
    void diagnosticFilesDisabledForAllChildren() {
        Diagnostics::instance().setEnabled(false);
        QTemporaryDir dir; HostManager host(nullptr,dir.path()); host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.running(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath("sunshine.conf")),5000);
        QFile config(dir.filePath("sunshine.conf")); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains(("log_path = "+QProcess::nullDevice()).toUtf8()));
        for (auto name : {"host.log","display.log","sunshine.log"}) QVERIFY(!QFile::exists(dir.filePath(name)));
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(),5000);
        Diagnostics::instance().setEnabled(true);
        QVERIFY(host.resizeDisplay(1920,1080,1,5));
        QTest::qWait(100);
        Diagnostics::instance().setEnabled(false);
        QVERIFY(host.running());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(),5000);
        for (auto name : {"host.log","display.log","sunshine.log"}) QVERIFY(!QFile::exists(dir.filePath(name)));
    }
    void displayRejectionReasons() {
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        QCOMPARE(host.displayResizeRejection(1920, 1080, 1, 1), QString("unavailable"));
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QCOMPARE(host.displayResizeRejection(1920, 1080, 1, 1, 3), QString("policy-unsupported"));
        QCOMPARE(host.displayResizeRejection(1921, 1080, 1, 1), QString("topology-invalid"));
        QCOMPARE(host.displayResizeRejection(1920, 1080, 3, 1), QString("topology-invalid"));
        QCOMPARE(host.displayResizeRejection(1920, 1080, 1, 0), QString("topology-invalid"));
        QVERIFY(host.displayResizeRejection(1920, 1080, 1, 1).isEmpty());
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 1));
        QCOMPARE(host.displayResizeRejection(1920, 1080, 1, 2), QString("busy"));
        QVERIFY(!host.resizeDisplay(1920, 1080, 1, 2));
        // Invalid input must not be diagnosed as a transient busy condition.
        QCOMPARE(host.displayResizeRejection(1921, 1080, 1, 2), QString("topology-invalid"));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 3000);
        QVERIFY(host.displayResizeRejection(1920, 1080, 1, 2).isEmpty());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void reconnectWaitsForPendingRestore() {
        qputenv("DESKPORT_TEST_MODE", "restore-slow");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1280, 720, 1, -1));
        QVERIFY(host.displayResizeRejection(1920, 1080, 2, 7, 1).isEmpty());
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7, 1));
        QCOMPARE(host.displayResizeRejection(1920, 1080, 2, 8, 1), QString("busy"));
        QVERIFY(!host.resizeDisplay(1920, 1080, 2, 8, 1));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 4000);
        QCOMPARE(result[0][0].toInt(), -1);
        QCOMPARE(result[1][0].toInt(), 7);
        QVERIFY(result[1][3].toString().isEmpty());
        QCOMPARE(result[1][1].toInt(), 1920);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void reconnectAfterFailedRestore() {
        qputenv("DESKPORT_TEST_MODE", "restore-reject");
        QTemporaryDir dir; HostManager host(nullptr, dir.path()); host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(),5000);
        QSignalSpy result(&host,&HostManager::displayResized);
        QVERIFY(host.resizeDisplay(2560,1440,1,-1));
        QVERIFY(host.resizeDisplay(1920,1888,2,7));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),2,4000);
        QVERIFY(!result[0][3].toString().isEmpty());
        QCOMPARE(result[1][0].toInt(),7);
        QVERIFY(result[1][3].toString().isEmpty());
        QCOMPARE(result[1][1].toInt(),1920); QCOMPARE(result[1][2].toInt(),1888);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(),5000);
    }
    void disconnectedQueuedControllerNeverTakesOver() {
        qputenv("DESKPORT_TEST_MODE", "restore-slow");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1280, 720, 1, -1));
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7, 1));
        host.restoreDisplay();
        QTest::qWait(2000);
        for (const auto& reply : result) QVERIFY(reply[0].toInt() < 0);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void resizeFailuresDoNotStopSharing_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("rejected") << QByteArray("resize-reject");
        QTest::newRow("timed-out") << QByteArray("resize-timeout");
    }
    void resizeFailuresDoNotStopSharing() {
        QFETCH(QByteArray, mode);
        qputenv("DESKPORT_TEST_MODE", mode);
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy result(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1920, 1080, 2, 7));
        QVERIFY(!host.resizeDisplay(1600, 1000, 2, 8));
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 6500);
        QCOMPARE(result.first().first().toInt(), 7);
        QVERIFY(!result.first().last().toString().isEmpty());
        QVERIFY(host.running()); QVERIFY(!host.changing());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void hyprlandCapabilityAndHostDeath() {
#ifndef Q_OS_LINUX
        QSKIP("Linux Hyprland capability adapter");
#else
        for (const auto& policyMode : {QByteArray("legacy"), QByteArray("0"), QByteArray("1")}) {
            const bool policies = policyMode == "1";
            qputenv("DESKPORT_TEST_HYPR_POLICY", policyMode);
            QTemporaryDir state; FixtureSubreaper children(state.path());
            HostManager host(nullptr, state.path(), false);
            host.start(2560, 1440);
            QTRY_VERIFY_WITH_TIMEOUT(host.running() && QFile::exists(state.filePath("host-started")), 5000);
            QVERIFY(host.adaptiveDisplayAvailable());
            QCOMPARE(host.displayPoliciesAvailable(), policies);
            QSignalSpy results(&host, &HostManager::displayResized);
            QVERIFY(host.resizeDisplay(1668, 2388, 2, 7, 0));
            QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
            QVERIFY2(results.at(0).at(3).toString().isEmpty(), qPrintable(QString::fromLatin1(policyMode) + ": " + results.at(0).at(3).toString()));
            QCOMPARE(host.displayWarning().isEmpty(), policies);
            QCOMPARE(PeerStore::read(state.filePath("last-display-request.json"))["displayPolicy"].toInt(), policyMode == "0" ? 2 : 0);
            QCOMPARE(PeerStore::read(state.filePath("virtual-display.json"))["output"].toString(), QString("DeskPort-Hyprland"));
            QFile config(state.filePath("sunshine.conf")); QVERIFY(config.open(QIODevice::ReadOnly));
            QVERIFY(config.readAll().contains("capture = wlr\n"));
            const auto pid = fixtureChildPid(state.filePath("host-pid")); QVERIFY(pid > 1);
            QCOMPARE(::kill(pid_t(pid), SIGKILL), 0);
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.filePath("display-closed")), 5000);
            host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(state.filePath("virtual-display.json")), 5000);
        }
        qunsetenv("DESKPORT_TEST_HYPR_POLICY");
#endif
    }
    void ownedLinuxCaptureTracksModeAndCleansUp() {
#ifndef Q_OS_LINUX
        QSKIP("Linux capture descriptor");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-display");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        const auto path = dir.path() + "/virtual-display.json";
        const auto initial = PeerStore::read(path);
        QCOMPARE(initial["node"].toInt(), 42);
        QCOMPARE(initial["serial"].toString(), QString("142"));
        QCOMPARE(initial["output"].toString(), QString("Meta-1"));
        QSignalSpy results(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1668, 2388, 2, 7));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
        const auto resized = PeerStore::read(path);
        QCOMPARE(resized["node"].toInt(), 43);
        QCOMPARE(resized["serial"].toString(), QString("143"));
        QCOMPARE(resized["output"].toString(), QString("Meta-2"));
        QCOMPARE(resized["width"].toInt(), 1668);
        QCOMPARE(resized["height"].toInt(), 2388);
        QCOMPARE(resized["scale"].toInt(), 2);
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        const auto text = config.readAll();
        QVERIFY(text.contains("capture = portal\n"));
        QVERIFY(text.contains("output_name = Meta-1\n")); // GNOME capture reads the live descriptor.
        host.restoreDisplay();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 4500);
        QCOMPARE(host.displayWidth(), 0);
        QVERIFY(host.adaptiveDisplayAvailable());
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 8));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 5000);
        QCOMPARE(PeerStore::read(path)["node"].toInt(), 43);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(!QFile::exists(path));
#endif
    }
    void idleLinuxStartupWaitsForAdmission() {
#ifndef Q_OS_LINUX
        QSKIP("Linux on-demand lifecycle");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-idle");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.running(), 5000);
        const auto path = dir.path() + "/virtual-display.json";
        QVERIFY(!QFile::exists(path));
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QCOMPARE(host.displayWidth(), 0);
        QVERIFY(!host.displayPoliciesAvailable()); // Older GNOME helpers stay unadvertised.
        QFile config(dir.path() + "/sunshine.conf"); QVERIFY(config.open(QIODevice::ReadOnly));
        QVERIFY(config.readAll().contains("capture = portal\n"));
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 7));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 5000);
        QCOMPARE(PeerStore::read(path)["output"].toString(), QString("Meta-2"));
        host.restoreDisplay();
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(path), 5000);
        QCOMPARE(host.displayWidth(), 0);
        QVERIFY(host.running()); QVERIFY(host.adaptiveDisplayAvailable());
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
#endif
    }
    void gnomePoliciesRequireNativeHelperOptIn() {
#ifndef Q_OS_LINUX
        QSKIP("Linux Mutter policy capability");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-policies");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QVERIFY(host.displayPoliciesAvailable());
        QSignalSpy results(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1668, 2388, 2, 7, 1));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 5000);
        QCOMPARE(host.displayWidth(), 1668);
        QCOMPARE(host.displayScale(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(!host.displayWarning().isEmpty(), 5000);
        QVERIFY(host.resizeDisplay(1920, 1080, 1, 8, 1));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(host.displayWarning().isEmpty(), 5000);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(!host.displayPoliciesAvailable());
#endif
    }
    void gnomeRecoveryMayExceedOrdinaryResizeDeadline() {
#ifndef Q_OS_LINUX
        QSKIP("Linux Mutter recovery deadline");
#else
        qputenv("DESKPORT_TEST_MODE", "gnome-slow");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy results(&host, &HostManager::displayResized);
        QVERIFY(host.resizeDisplay(1668, 2388, 2, 7, 2));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 9000);
        QVERIFY2(results.at(0).at(3).toString().isEmpty(), qPrintable(results.at(0).at(3).toString()));
        QCOMPARE(host.displayWidth(), 1668);
        bool settled = false, ok = false;
        host.settleSessionDisplay(this, [&](bool success) { ok = success; settled = true; });
        QTRY_VERIFY_WITH_TIMEOUT(settled, 9000);
        QVERIFY(ok);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
#endif
    }
    void localHostsAreFilteredWithoutMatchingNamesOrSubnets() {
        const QList<QHostAddress> local{QHostAddress("192.0.2.10"), QHostAddress("2001:db8::10")};
        for (const QString& value : {"127.0.0.1", "127.0.0.2", "::1", "192.0.2.10", "::ffff:192.0.2.10", "2001:db8::10"})
            QVERIFY(DeskPortNetwork::isLocalHostAddress(value, local));
        for (const QString& value : {"", "0.0.0.0", "::", "DeskPort", "192.0.2.11", "2001:db8::11"})
            QVERIFY(!DeskPortNetwork::isLocalHostAddress(value, local));
    }
    void manualAddressesKeepServicesSeparate() {
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10").port(), quint16(48989));
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10:47989").port(), quint16(47989));
        QCOMPARE(NvAddress::fromUserInput("203.0.113.10:49089").port(), quint16(49089));
        QCOMPARE(NvAddress::fromUserInput("[2001:db8::1]:49089").port(), quint16(49089));
        QVERIFY(NvAddress::fromUserInput("203.0.113.10:0").isNull());
    }
    void separateFromNativePortFamily() {
        QVERIFY(!DeskPortNetwork::isPrivateBase(47989));
        QVERIFY(!DeskPortNetwork::isPrivateBase(65535));
        QVERIFY(!DeskPortNetwork::isPrivateBase(DeskPortNetwork::DefaultBasePort + DeskPortNetwork::PortChoices * DeskPortNetwork::PortStep));
        for (int i = 0; i < DeskPortNetwork::PortChoices; ++i) {
            const int base = DeskPortNetwork::DefaultBasePort + i * DeskPortNetwork::PortStep;
            QVERIFY(DeskPortNetwork::isPrivateBase(base));
            QVERIFY(!DeskPortNetwork::isPrivateBase(base - 5));
            QVERIFY(!DeskPortNetwork::isPrivateBase(base + 1));
            QVERIFY(base - 5 > 48010);
            QVERIFY(base + 21 <= 65535);
        }
    }
    void occupiedFamilyMemberIsNotReused_data() {
        QTest::addColumn<bool>("tcp");
        QTest::addColumn<int>("offset");
        for (int offset : DeskPortNetwork::tcpOffsets())
            QTest::newRow(qPrintable(QString("tcp-%1").arg(offset))) << true << offset;
        for (int offset : DeskPortNetwork::udpOffsets())
            QTest::newRow(qPrintable(QString("udp-%1").arg(offset))) << false << offset;
    }
    void occupiedFamilyMemberIsNotReused() {
        QFETCH(bool, tcp);
        QFETCH(int, offset);
        HostPortReservation ports;
        const int original = ports.reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost);
        QVERIFY(original != 0);
        ports.release();
        QTcpServer foreignTcp;
        QUdpSocket foreignUdp;
        if (tcp) QVERIFY(foreignTcp.listen(QHostAddress::LocalHost, original + offset));
        else QVERIFY(foreignUdp.bind(QHostAddress::LocalHost, original + offset, QAbstractSocket::DontShareAddress));
        const int selected = ports.reserve(original, QHostAddress::LocalHost);
        QVERIFY(selected != 0);
        QVERIFY(selected != original);
        if (tcp) QVERIFY(foreignTcp.isListening());
        else QCOMPARE(foreignUdp.localPort(), quint16(original + offset));
        ports.release();
        foreignTcp.close(); foreignUdp.close();
        QCOMPARE(ports.reserve(original, QHostAddress::LocalHost), original);
    }
    void exhaustedPortsDoNotLaunchComponents() {
        std::vector<std::unique_ptr<HostPortReservation>> occupied;
        for (int i = 0; i < DeskPortNetwork::PortChoices; ++i) {
            std::unique_ptr<HostPortReservation> ports(new HostPortReservation);
            if (!ports->reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost)) break;
            occupied.push_back(std::move(ports));
        }
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 1000);
        QVERIFY(host.status().contains("No free DeskPort port group"));
        QVERIFY(!QFile::exists(state.path() + "/sunshine.conf"));
        QVERIFY(!QFile::exists(state.path() + "/display.log"));
        occupied.clear();
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void anotherInstanceCannotChangeActiveHost() {
        QTemporaryDir state;
        HostManager first(nullptr, state.path());
        first.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(first.canPair(), 5000);
        QFile lock(state.path() + "/instance.lock");
        QVERIFY(lock.open(QIODevice::ReadWrite));
        QVERIFY(lock.setFileTime(QDateTime::currentDateTime().addSecs(-120), QFileDevice::FileModificationTime));
        lock.close();
        HostManager second(nullptr, state.path());
        second.start(3840, 2160);
        QVERIFY(!second.running());
        QVERIFY(second.status().contains("Another DeskPort instance"));
        QVERIFY(first.canPair());
        first.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!first.running(), 5000);
        second.start(3840, 2160);
        QTRY_VERIFY_WITH_TIMEOUT(second.canPair(), 5000);
        second.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!second.running(), 5000);
    }
    void chosenPortReachesHostConfiguration() {
        HostPortReservation foreign;
        const int occupied = foreign.reserve(DeskPortNetwork::DefaultBasePort, QHostAddress::LocalHost);
        QVERIFY(occupied != 0);
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QVERIFY(host.basePort() != occupied);
        QFile config(state.path() + "/sunshine.conf");
        QVERIFY(config.open(QIODevice::ReadOnly));
        const auto contents = config.readAll();
        QVERIFY(contents.contains(QString("port = %1\n").arg(host.basePort()).toUtf8()));
        // Audio availability is host-wide; each client selects whether to receive it.
        QVERIFY(contents.contains("stream_audio = enabled\n"));
        QVERIFY(contents.contains("upnp = disabled\n"));
        QVERIFY(contents.contains("system_tray = disabled\n"));
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void startupAndStopStayResponsive_data() {
        QTest::addColumn<int>("heartbeatInterval");
        QTest::newRow("normal-cadence") << 10;
        QTest::newRow("slow-cadence") << 75;
    }
    void startupAndStopStayResponsive() {
        QFETCH(int, heartbeatInterval);
        qputenv("DESKPORT_TEST_MODE", "auth-gated");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        QVERIFY(host.available());
        QElapsedTimer elapsed; elapsed.start();
        host.start(2560, 1440);
        QVERIFY(elapsed.elapsed() < 200);
        QVERIFY(host.running());
        QVERIFY(!host.canPair());
        int ticks = 0;
        QTimer heartbeat;
        bool releasedAuthentication = false;
        connect(&heartbeat, &QTimer::timeout, [&] {
            if (!QFile::exists(state.filePath("auth-started")) || ticks >= 10) return;
            ++ticks;
            if (ticks == 10) {
                // Authentication cannot finish until the UI event loop proves
                // progress. Timer coalescing may delay this, but cannot pass it.
                QFile release(state.filePath("auth-release"));
                releasedAuthentication = release.open(QIODevice::WriteOnly);
            }
        });
        heartbeat.start(heartbeatInterval);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QVERIFY(!QFile::exists(state.filePath("auth-gate-timed-out")));
        QVERIFY(releasedAuthentication);
        QCOMPARE(ticks, 10);
        elapsed.restart(); host.stop();
        QVERIFY(elapsed.elapsed() < 200);
        QVERIFY(!host.canPair());
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
    void authenticationFailureCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "auth-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("authentication"));
        QVERIFY(!host.canPair());
    }
    void hostFailureAllowsRetry() {
        qputenv("DESKPORT_TEST_MODE", "host-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Host stopped (7)"));
        qputenv("DESKPORT_TEST_MODE", "normal");
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
    }
    void earlyDisplayExitCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "display-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Virtual display stopped"));
    }
    void displayFailureStopsRunningHost() {
        qputenv("DESKPORT_TEST_MODE", "display-late-fail");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QVERIFY(host.status().contains("Virtual display stopped"));
        QVERIFY(!host.canPair());
    }
    void authenticationTimeoutCleansUp() {
        qputenv("DESKPORT_TEST_MODE", "auth-slow");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 9000);
        QVERIFY(host.status().contains("authentication timed out"));
        QVERIFY(!QFile::exists(state.path() + "/host-started"));
    }
    void stopDuringAuthenticationNeverStartsHost() {
        qputenv("DESKPORT_TEST_MODE", "auth-slow");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.path() + "/auth-started"), 3000);
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(600);
        QVERIFY(!QFile::exists(state.path() + "/host-started"));
        QVERIFY(!host.canPair());
    }
    void unresponsiveHostIsKilledWithoutBlockingUi() {
        qputenv("DESKPORT_TEST_MODE", "stubborn");
        QTemporaryDir state;
        HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(state.path() + "/host-started"), 5000);
        int ticks = 0;
        qint64 longestGap = 0;
        QElapsedTimer pulse; pulse.start();
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat, &QTimer::timeout, [&] {
            ++ticks; longestGap = qMax(longestGap, pulse.restart());
        });
        heartbeat.start(10);
        QElapsedTimer elapsed; elapsed.start(); host.stop();
        QVERIFY(elapsed.elapsed() < 200);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 6000);
        // Hosted runners may coalesce timers. Measure responsiveness, not the
        // number of 10 ms callbacks delivered by a particular machine.
        QVERIFY(elapsed.elapsed() >= 2000); // Exercise the forced-stop grace period.
        QVERIFY(ticks >= 3);
        QVERIFY2(longestGap < 1000, qPrintable(QString::number(longestGap)));
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
    void slowDisplayRecoveryGetsItsOwnShutdownGrace() {
        qputenv("DESKPORT_TEST_MODE", "stubborn-slow-display");
        QTemporaryDir state; HostManager host(nullptr, state.path());
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair() && QFile::exists(state.filePath("host-started")), 5000);
        QElapsedTimer elapsed; elapsed.start();
        host.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 8000);
        // The host needs its 2.5 s forced-stop grace, then the display fixture
        // needs another 1.5 s to restore. A global 3.5 s deadline kills it early.
        QVERIFY(elapsed.elapsed() >= 3500);
        QVERIFY(QFile::exists(state.filePath("display-closed")));
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_MACOS
    const auto exitProbe = qEnvironmentVariable("DESKPORT_TEST_EXPLICIT_EXIT");
    if (!exitProbe.isEmpty()) return explicitExitProbe(app, exitProbe == "restart");
#endif
#ifdef Q_OS_LINUX
    const auto crashOwner = qEnvironmentVariable("DESKPORT_TEST_CRASH_OWNER");
    if (!crashOwner.isEmpty()) {
        HostManager host(nullptr, crashOwner, false);
        host.start(2560, 1440);
        return app.exec();
    }
#endif
    HostLifecycle test;
    return QTest::qExec(&test, argc, argv);
}
#include "host-lifecycle.moc"
