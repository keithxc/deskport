#include <QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "hostmanager.h"

class HostLifecycle : public QObject {
    Q_OBJECT
private slots:
    void init() { qputenv("DESKPORT_TEST_MODE", "normal"); }
    void startupAndStopStayResponsive() {
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
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start(10);
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 5000);
        QVERIFY(ticks >= 10); // Authentication runs for 400 ms in the fake host.
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
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
        heartbeat.start(10);
        QElapsedTimer elapsed; elapsed.start(); host.stop();
        QVERIFY(elapsed.elapsed() < 200);
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 6000);
        QVERIFY(ticks >= 100);
        QCOMPARE(host.status(), QString("Sharing is off"));
    }
};
QTEST_MAIN(HostLifecycle)
#include "host-lifecycle.moc"
