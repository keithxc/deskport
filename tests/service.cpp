#include <QtTest>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QApplication>
#include "hostmanager.h"
#include "serviceconfig.h"

class ServiceTests : public QObject {
    Q_OBJECT
private slots:
    void startupSupervisesTheRealProcess() {
        const auto plist = DeskPortService::launchAgent();
        QXmlStreamReader reader(plist); while (!reader.atEnd()) reader.readNext();
        QVERIFY(!reader.hasError());
        QVERIFY(plist.contains("/Applications/DeskPort.app/Contents/MacOS/DeskPort"));
        QVERIFY(!plist.contains("/usr/bin/open"));
        QVERIFY(plist.contains("SuccessfulExit</key><false/>"));
        QVERIFY(plist.contains("--background"));
        const auto unit = DeskPortService::systemdUnit("/tmp/path with spaces/percent%/deskport");
        QVERIFY(unit.contains("ExecStart=\"/tmp/path with spaces/percent%%/deskport\" --background"));
        QVERIFY(unit.contains("Restart=on-failure"));
        QVERIFY(unit.contains("KillMode=control-group"));
        QVERIFY(DeskPortService::desktopEntry().contains("systemctl --user start"));
    }
    void declarativeConfigurationKeepsOwnershipOfLoginStartup() {
        QTemporaryDir dir;
        const QString linked = dir.path() + "/linked.desktop", plain = dir.path() + "/plain.desktop";
        // 悬空也算数: home-manager 的链接在 store 回收后仍是它的地盘。
        QVERIFY(QFile::link("/nix/store/abc-deskport/share/applications/x.desktop", linked));
        QFile file(plain); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        QVERIFY(DeskPortService::storeManaged(linked));
        QVERIFY(!DeskPortService::storeManaged(plain));
        QVERIFY(!DeskPortService::storeManaged(dir.path() + "/missing.desktop"));
        QVERIFY(QFile::link(dir.path() + "/elsewhere", dir.path() + "/other.desktop"));
        QVERIFY(!DeskPortService::storeManaged(dir.path() + "/other.desktop"));
    }
    void ordinaryQuitHidesUntilExplicitExit() {
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.setResident(true);
        QSignalSpy hide(&host, &HostManager::hideRequested), exit(&host, &HostManager::exitRequested);
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(qApp, &quit);
        QCOMPARE(hide.size(), 1); QCOMPARE(exit.size(), 0);
        host.requestExit(); QCOMPARE(exit.size(), 1);
    }
    void crashedChildRecoversWithoutChangingIdentity() {
        qputenv("DESKPORT_TEST_MODE", "host-crash-once");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.path()+"/crashed-once"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(host.status().contains("Host stopped (7)"), 5000);
        QFile secret(dir.path()+"/control-secret"); QVERIFY(secret.open(QIODevice::ReadOnly));
        const auto before = secret.readAll(); secret.close();
        QTRY_VERIFY_WITH_TIMEOUT(host.canPair(), 12000);
        QVERIFY(secret.open(QIODevice::ReadOnly)); QCOMPARE(secret.readAll(), before);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(5500); QVERIFY(!host.running());
    }
    void stopCancelsPendingRecovery() {
        qputenv("DESKPORT_TEST_MODE", "host-fail");
        QTemporaryDir dir; HostManager host(nullptr, dir.path());
        host.start(1280, 720);
        QTRY_VERIFY_WITH_TIMEOUT(host.status().contains("Host stopped (7)"), 5000);
        host.stop(); qputenv("DESKPORT_TEST_MODE", "normal");
        QTRY_VERIFY_WITH_TIMEOUT(!host.running(), 5000);
        QTest::qWait(5500); QVERIFY(!host.running());
    }
};
QTEST_MAIN(ServiceTests)
#include "service.moc"
