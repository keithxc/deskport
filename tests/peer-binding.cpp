#include <QtTest>
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
