#include <QtTest>
#include "sleepmonitor.h"
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QProcess>
int runLogindFixture(QCoreApplication&);
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
void injectNativeSleep(bool);
void injectNativeLock();
#endif
class ClientLifecycleTest : public QObject {
    Q_OBJECT
private slots:
#ifdef Q_OS_LINUX
    void nativeLogindEvents() {
        QVERIFY(qEnvironmentVariable("DESKPORT_LIFECYCLE_PRIVATE_BUS") == "1");
        struct OwnedFixture : QProcess {
            ~OwnedFixture() { if (state()!=NotRunning) { terminate(); if(!waitForFinished(2000)) { kill(); waitForFinished(); } } }
        } fixture;
        fixture.start(QCoreApplication::applicationFilePath(), {"--logind-fixture"});
        QVERIFY(fixture.waitForStarted());
        auto bus = QDBusConnection::systemBus();
        QTRY_VERIFY(bus.interface()->isServiceRegistered("org.freedesktop.login1").value());
        SleepMonitor power; ScreenLockMonitor screen;
        auto call = [&](QString method, QVariantList args = {}) {
            auto message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", method);
            message.setArguments(args); return bus.call(message, QDBus::Block, 2000);
        };
        QCOMPARE(call("InhibitCount").arguments().value(0).toInt(),1);
        QSignalSpy sleep(&power,&SleepMonitor::sleeping), wake(&power,&SleepMonitor::resumed);
        QCOMPARE(call("SetLock",{true}).type(),QDBusMessage::ReplyMessage);
        QTRY_VERIFY(ScreenLockMonitor::locked());
        QCOMPARE(call("SetSleep",{true}).type(),QDBusMessage::ReplyMessage);
        QTRY_VERIFY(SleepMonitor::asleep());
        call("SetSleep",{true}); QTest::qWait(20); QCOMPARE(sleep.count(),1);
        power.release();
        call("SetSleep",{false}); QTRY_VERIFY(!SleepMonitor::asleep());
        QCOMPARE(wake.count(),1); QVERIFY(ScreenLockMonitor::locked());
        QCOMPARE(call("InhibitCount").arguments().value(0).toInt(),2);
        call("SetLock",{false}); QTRY_VERIFY(!ScreenLockMonitor::locked());
        fixture.terminate(); QVERIFY(fixture.waitForFinished(2000));
        QCOMPARE(fixture.state(),QProcess::NotRunning);
    }
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    void nativeAdapterEvents() {
        SleepMonitor power;
        ScreenLockMonitor screen;
        QSignalSpy sleep(&power, &SleepMonitor::sleeping), wake(&power, &SleepMonitor::resumed);
        injectNativeSleep(true);
        QTRY_VERIFY(SleepMonitor::asleep()); QCOMPARE(sleep.count(), 1);
        injectNativeSleep(true); QCOMPARE(sleep.count(), 1);
        injectNativeSleep(false);
        QTRY_VERIFY(!SleepMonitor::asleep()); QCOMPARE(wake.count(), 1);
        injectNativeLock(); QTRY_VERIFY(ScreenLockMonitor::locked());
        screen.setLocked(false);
    }
#endif
    void duplicatePowerEvents() {
        SleepMonitor power(nullptr, false);
        QSignalSpy sleep(&power, &SleepMonitor::sleeping), wake(&power, &SleepMonitor::resumed);
        power.prepareForSleep(false);
        QCOMPARE(wake.count(), 0);
        for (int i=0; i<3; ++i) power.prepareForSleep(true);
        QVERIFY(SleepMonitor::asleep()); QCOMPARE(sleep.count(), 1);
        for (int i=0; i<3; ++i) power.prepareForSleep(false);
        QVERIFY(!SleepMonitor::asleep()); QCOMPARE(wake.count(), 1);
        power.prepareForSleep(true); power.prepareForSleep(false);
        QCOMPARE(sleep.count(), 2); QCOMPARE(wake.count(), 2);
    }
    void wakingDoesNotUnlock() {
        SleepMonitor power(nullptr, false);
        ScreenLockMonitor screen(nullptr, false);
        QSignalSpy change(&screen, &ScreenLockMonitor::changed);
        screen.setLocked(true); screen.setLocked(true);
        power.prepareForSleep(true); power.prepareForSleep(false);
        QVERIFY(ScreenLockMonitor::locked()); QCOMPARE(change.count(), 1);
        screen.setLocked(false); screen.setLocked(false);
        QCOMPARE(change.count(), 2); QVERIFY(!ScreenLockMonitor::locked());
        // Unlocking while asleep also cannot authorize a transport restart.
        power.prepareForSleep(true); screen.setLocked(true); screen.setLocked(false);
        QVERIFY(SleepMonitor::asleep()); power.prepareForSleep(false);
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc,argv);
#ifdef Q_OS_LINUX
    if (app.arguments().contains("--logind-fixture")) return runLogindFixture(app);
#endif
    ClientLifecycleTest test; return QTest::qExec(&test,argc,argv);
}
#include "client-lifecycle.moc"
