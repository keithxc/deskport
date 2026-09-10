#include <QtTest>
#include <future>
#include "adaptivedisplay.h"
#include "workspaceresolution.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSslSocket>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include "peermanager.h"
#include "peerstore.h"
#include "qmlcachekey.h"

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
class PeerBinding : public QObject {
    Q_OBJECT
private slots:
    void fractionalOutputScale() {
        QCOMPARE(DeskPortDisplay::scaleForOutput({2880, 1620}, {1920, 1080}, 2.0), 1.5);
        QCOMPARE(DeskPortDisplay::scaleForOutput({3840, 2160}, {1920, 1080}, 1.0), 2.0);
        QCOMPARE(DeskPortDisplay::scaleForOutput({1920, 1080}, {1920, 1080}, 2.0), 1.0);
        QCOMPARE(DeskPortDisplay::scaleForOutput({}, {1920, 1080}, 1.5), 1.5);
        QCOMPARE(DeskPortDisplay::scaleForOutput({1080, 1920}, {1920, 1080}, 1.5), 1.5);
    }

    void adaptiveDisplayRequiresPinnedApprovedExclusiveController() {
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B"), cCert = credential("TEST_CERT_C");
        const auto fp = QString::fromLatin1(QSslCertificate(aCert).digest(QCryptographicHash::Sha256).toHex());
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {fp, QJsonObject{{"ready", true}, {"granted", true}}}}}}));
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager server(&host, bCert, credential("TEST_KEY_B"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        host.start(2560, 1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(), 5000);
        QSignalSpy resized(&host, &HostManager::displayResized), approval(&server, &PeerManager::incomingRequest);
        auto resize = [this](AdaptiveDisplay& channel, QSize size) {
            std::atomic<int> frames {0};
            auto result = std::async(std::launch::async, [&] { return channel.resize(size, 2, [&] { ++frames; }); });
            QElapsedTimer timer; timer.start();
            while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && timer.elapsed() < 11000) QTest::qWait(10);
            const bool connected = result.get();
            if (connected && frames == 0) return false; // Waiting must keep the loading UI alive.
            return connected;
        };
        {
            AdaptiveDisplay wrongPin("127.0.0.1", server.port(), QSslCertificate(cCert), aCert, credential("TEST_KEY_A"));
            QVERIFY(!resize(wrongPin, QSize(1920, 1080)));
        }
        QTRY_VERIFY(!server.busy());
        {
            AdaptiveDisplay unknown("127.0.0.1", server.port(), QSslCertificate(bCert), cCert, credential("TEST_KEY_C"));
            QVERIFY(!resize(unknown, QSize(1920, 1080)));
        }
        QTRY_VERIFY(!server.busy());
        QCOMPARE(resized.size(), 0); QCOMPARE(approval.size(), 0);
        {
            AdaptiveDisplay channel("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
            QVERIFY(resize(channel, QSize(1920, 1080)));
            QCOMPARE(resized.size(), 1); QVERIFY(host.running()); QVERIFY(!server.busy());
            {
                AdaptiveDisplay competing("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
                QVERIFY(!resize(competing, QSize(2560, 1440)));
            }
            QTRY_VERIFY(!server.busy());
            QVERIFY(resize(channel, QSize(1600, 1000)));
            QCOMPARE(resized.size(), 2);
            QVERIFY(!host.resizeDisplay(99999, 1000, 2, 99));
            QVERIFY(!host.resizeDisplay(1600, 1000, 9, 99));
            QCOMPARE(approval.size(), 0);
        }
        // A released controller cannot prevent the next connection taking over.
        QTest::qWait(100);
        AdaptiveDisplay next("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
        QVERIFY(resize(next, QSize(2560, 1440)));
        QCOMPARE(resized.size(), 3);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void workspaceUsesClientSystemScale() {
        const auto fractional = DeskPortDisplay::forClient(QSize(2880, 1620), 1.5);
        QCOMPARE(fractional.pixels, QSize(3840, 2160)); QCOMPARE(fractional.scale, 2);
        const auto retina = DeskPortDisplay::forClient(QSize(2880, 1800), 2.0);
        QCOMPARE(retina.pixels, QSize(2880, 1800)); QCOMPARE(retina.scale, 2);
        const auto standard = DeskPortDisplay::forClient(QSize(1920, 1080), 1.0);
        QCOMPARE(standard.pixels, QSize(1920, 1080)); QCOMPARE(standard.scale, 1);
        QCOMPARE(DeskPortDisplay::forClient(QSize(3840, 2160), 1.5).pixels, QSize(5120, 2880));
        QCOMPARE(DeskPortDisplay::forClient(QSize(2560, 1440), 1.5).pixels, QSize(3416, 1920));
        QCOMPARE(DeskPortDisplay::forClient(QSize(800, 450), 2.0).pixels, QSize(1920, 1080));
        QVERIFY(!DeskPortDisplay::forClient(QSize(), 1.5).pixels.isValid());
        QVERIFY(!DeskPortDisplay::forClient(QSize(1920,1080), 0).pixels.isValid());
    }
    void adaptiveSizeBounds() {
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(15360, 8640)), QSize(7680, 4320));
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(1001, 777)), QSize(1000, 776));
        QCOMPARE(AdaptiveDisplay::boundedSize(QSize(100, 100)), QSize(640, 360));
        QVERIFY(!AdaptiveDisplay::boundedSize(QSize(0, 0)).isValid());
    }
    void qmlCacheChangesWithContentEvenWhenTimestampsMatch() {
        QTemporaryDir dir;
        QFile file(dir.path()+"/main.qml");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("old UI"); file.close();
        const auto timestamp = QFileInfo(file).lastModified();
        const auto before = qmlCacheKey(dir.path());
        QCOMPARE(before,qmlCacheKey(dir.path()));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("new UI");
        QVERIFY(file.setFileTime(timestamp,QFileDevice::FileModificationTime)); file.close();
        QVERIFY(before != qmlCacheKey(dir.path()));
        const auto after = qmlCacheKey(dir.path());
        QFile child(dir.path()+"/Approval.qml");
        QVERIFY(child.open(QIODevice::WriteOnly)); child.write("Dialog {}"); child.close();
        QVERIFY(after != qmlCacheKey(dir.path()));
        QVERIFY(child.remove()); QCOMPARE(after,qmlCacheKey(dir.path()));
    }
    void actualApprovalDialogOpensAndClosesWithRequest() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        PeerManager a(&ah,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        QQmlEngine engine;
        QQuickWindow window;
        QQmlComponent component(&engine,QUrl::fromLocalFile(qEnvironmentVariable("TEST_BINDING_QML")));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> dialog(component.createWithInitialProperties({
            {"manager",QVariant::fromValue(&b)}, {"appWindow",QVariant::fromValue(&window)},
            {"parent",QVariant::fromValue(window.contentItem())}}));
        QVERIFY2(dialog,qPrintable(component.errorString()));
        a.request(QString("127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(),5000);
        QCOMPARE(dialog->property("transaction").toString(),b.requestId());
        QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
        a.cancel();
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }
    void trustPreservesExistingClientsAndRejectsBrokenState() {
        QTemporaryDir dir; const QString file = dir.path()+"/state.json";
        const QSslCertificate cert(credential("TEST_CERT_A"));
        QVERIFY(!cert.isNull());
        QVERIFY(PeerStore::write(file, {{"extra", 42}, {"root", QJsonObject{{"uniqueid", "stable"},
            {"named_devices", QJsonArray{QJsonObject{{"uuid", "other"}, {"name", "existing"}}}}}}}));
        QVERIFY(PeerStore::trust(file, "new", "new device", cert));
        auto state = PeerStore::read(file);
        QCOMPARE(state["extra"].toInt(), 42);
        QCOMPARE(state["root"].toObject()["uniqueid"].toString(), QString("stable"));
        QCOMPARE(state["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "legacy-replacement", "same certificate", cert));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "new", "renamed", cert));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 2);
        QVERIFY(PeerStore::trust(file, "new", "", QSslCertificate(), true));
        QCOMPARE(PeerStore::read(file)["root"].toObject()["named_devices"].toArray().size(), 1);
        QFile broken(file); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("{broken"); broken.close();
        QVERIFY(!PeerStore::trust(file, "new", "new", cert));
        QVERIFY(broken.open(QIODevice::ReadOnly)); QCOMPARE(broken.readAll(), QByteArray("{broken"));
    }
    void mutualBindingWaitsForOneApprovalAndSurvivesRestart() {
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B");
        const auto aKey = credential("TEST_KEY_A"), bKey = credential("TEST_KEY_B");
        QString aId, bId;
        {
            HostManager ah(nullptr, dir.path()+"/ah"), bh(nullptr, dir.path()+"/bh");
            PeerManager a(&ah,aCert,aKey,dir.path()+"/ab",0,QHostAddress::LocalHost);
            PeerManager b(&bh,bCert,bKey,dir.path()+"/bb",0,QHostAddress::LocalHost);
            QVERIFY(a.port() > 0); QVERIFY(b.port() > 0);
            aId=ah.identity()["hostId"].toString(); bId=bh.identity()["hostId"].toString();
            QSignalSpy incoming(&b,&PeerManager::incomingRequest), aDone(&a,&PeerManager::peerBound), bDone(&b,&PeerManager::peerBound);
            a.request(QString("127.0.0.1:%1").arg(b.port()));
            QTRY_COMPARE_WITH_TIMEOUT(incoming.size(), 1, 5000);
            QTRY_VERIFY(a.status().contains("Request received"));
            QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
            b.approve("wrong-transaction"); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            b.approve(b.requestId());
            QTRY_COMPARE_WITH_TIMEOUT(aDone.size(),1,7000);
            QTRY_COMPARE_WITH_TIMEOUT(bDone.size(),1,7000);
            QVERIFY(a.peers().first().toMap()["ready"].toBool()); QVERIFY(b.peers().first().toMap()["ready"].toBool());
            auto clients=PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray();
            QCOMPARE(clients.size(),1); QCOMPARE(QSslCertificate(clients[0].toObject()["cert"].toString().toUtf8()),QSslCertificate(bCert));
            QFile metadata(dir.path()+"/ab/peers.json"); QVERIFY(metadata.open(QIODevice::ReadOnly)); QVERIFY(!metadata.readAll().contains("PRIVATE KEY"));
        }
        {
            HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
            PeerManager a(&ah,aCert,aKey,dir.path()+"/ab",0,QHostAddress::LocalHost);
            PeerManager b(&bh,bCert,bKey,dir.path()+"/bb",0,QHostAddress::LocalHost);
            QCOMPARE(ah.identity()["hostId"].toString(),aId); QCOMPARE(bh.identity()["hostId"].toString(),bId);
            QSignalSpy restored(&a,&PeerManager::peerBound); a.restoreHosts(); QCOMPARE(restored.size(),1);
            HostManager ch(nullptr,dir.path()+"/ch");
            PeerManager c(&ch,credential("TEST_CERT_C"),credential("TEST_KEY_C"),dir.path()+"/cb",quint16(a.peers().first().toMap()["bindingPort"].toInt()),QHostAddress::LocalHost);
            QSignalSpy substituted(&c,&PeerManager::incomingRequest);
            a.request(QString("127.0.0.1:%1").arg(c.port()));
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000);
            QCOMPARE(substituted.size(),0); QVERIFY(c.peers().isEmpty());
            QVERIFY(a.status().contains("different device key"));
            a.revoke(a.peers().first().toMap()["fingerprint"].toString());
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000); QVERIFY(a.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        }
    }
    void invalidOversizedAndReplayedMessagesCannotGrant() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        QVERIFY(ah.prepareIdentity(credential("TEST_CERT_A"),credential("TEST_KEY_A")));
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        for (int mode = 0; mode < 3; ++mode) {
            QSslSocket socket;
            socket.setLocalCertificate(QSslCertificate(credential("TEST_CERT_A")));
            socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"), QSsl::Rsa));
            connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&socket](const QList<QSslError>& e){ socket.ignoreSslErrors(e); });
            socket.connectToHostEncrypted("127.0.0.1",quint16(b.port()));
            QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
            if (mode == 0) socket.write("{not-json}\n");
            if (mode == 1) socket.write(QByteArray(40000,'x'));
            if (mode == 2) {
                auto meta=ah.identity(); meta["version"]=1; meta["bindingPort"]=48991; meta["name"]="Test A";
                const auto request=QJsonDocument(QJsonObject{{"type","request"},{"tx",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"meta",meta}}).toJson(QJsonDocument::Compact)+ '\n';
                socket.write(request); QTRY_VERIFY(!b.requestId().isEmpty());
                socket.write(request);
            }
            QTRY_VERIFY_WITH_TIMEOUT(!b.busy(),5000);
            QVERIFY(b.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/bh/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        }
    }
    void declinedOrDisconnectedRequestsNeverGrantAccess() {
        QTemporaryDir dir;
        HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
        PeerManager a(&ah,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        a.request(QString("127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        b.reject(b.requestId());
        QTRY_VERIFY(!a.busy()); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
        a.request(QString("127.0.0.1:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        const QString stale=b.requestId(); a.cancel(); QTRY_VERIFY(!b.busy());
        b.approve(stale); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
    }
};
QTEST_MAIN(PeerBinding)
#include "peer-binding.moc"
