#include <QSysInfo>
#include <QtTest>
#include <future>
#include "adaptivedisplay.h"
#include "workspaceresolution.h"
#include <QTemporaryDir>
#include <QHostInfo>
#include <QSignalSpy>
#include <QPointer>
#include <QSslSocket>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include "peermanager.h"
#include "sessiongraph.h"
#include "peerstore.h"
#include "qmlcachekey.h"

static QByteArray credential(const char* name) {
    QFile f(qEnvironmentVariable(name)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
// Simulate Windows without a host while exercising production PeerManager on Linux/macOS.
class UnavailableHost : public HostManager {
public:
    using HostManager::HostManager;
    bool available() const override { return false; }
};
class ScriptedBindingHost : public QTcpServer {
public:
    ScriptedBindingHost(QByteArray cert, QByteArray key) : certificate(cert), privateKey(key, QSsl::Rsa) {}
    QPointer<QSslSocket> socket;
    QList<QJsonObject> messages;
    void send(const QJsonObject& frame) { socket->write(QJsonDocument(frame).toJson(QJsonDocument::Compact)+'\n'); }
protected:
    void incomingConnection(qintptr fd) override {
        socket = new QSslSocket(this);
        socket->setSocketDescriptor(fd);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(privateKey);
        socket->setProtocol(QSsl::TlsV1_2OrLater); socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
        connect(socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), socket, [this](const QList<QSslError>& errors) {
            for (const auto& error : errors) if (error.error() != QSslError::SelfSignedCertificate &&
                error.error() != QSslError::HostNameMismatch) return;
            socket->ignoreSslErrors(errors);
        });
        connect(socket, &QSslSocket::readyRead, this, [this] {
            while (socket->canReadLine()) messages.append(QJsonDocument::fromJson(socket->readLine()).object());
        });
        socket->startServerEncryption();
    }
private:
    QSslCertificate certificate;
    QSslKey privateKey;
};
class PeerBinding : public QObject {
    Q_OBJECT
private slots:
    void sessionGraph_data() {
        QTest::addColumn<QString>("scenario");
        for (const char* item : {"chain", "reciprocal", "three-cycle", "simultaneous", "takeover-cycle", "unreachable", "old-desktop", "old-mobile", "reservation-lifetime"})
            QTest::newRow(item) << QString(item);
    }
    void sessionGraph() {
        QFETCH(QString, scenario);
        QTemporaryDir dir;
        const QList<QByteArray> certs{credential("TEST_CERT_A"),credential("TEST_CERT_B"),credential("TEST_CERT_C")};
        const QList<QByteArray> keys{credential("TEST_KEY_A"),credential("TEST_KEY_B"),credential("TEST_KEY_C")};
        QStringList ids;
        for (const auto& cert : certs) ids << SessionGraph::identity(QSslCertificate(cert));
        std::vector<std::unique_ptr<HostManager>> hosts;
        std::vector<std::unique_ptr<PeerManager>> peers;
        QStringList tokens{"edge-a", "edge-b", "edge-c"};
        struct Cleanup {
            QStringList ids, tokens;
            ~Cleanup() { for (int i=0;i<ids.size();++i) SessionGraph::release(ids[i],tokens[i]); }
        } cleanup{ids,tokens};
        for (int i=0;i<3;++i) {
            const auto base = dir.path()+QString("/%1").arg(i);
            QDir().mkpath(base+"/binding"); QJsonObject records;
            for (int j=0;j<3;++j) if (i!=j) {
                QJsonObject record{{"ready",true},{"granted",true}};
                if (scenario=="old-mobile" && j==0) record["role"]="client";
                records[ids[j]]=record;
            }
            QVERIFY(PeerStore::write(base+"/binding/peers.json",{{"version",1},{"peers",records}}));
            hosts.emplace_back(new HostManager(nullptr,base+"/host"));
            peers.emplace_back(new PeerManager(hosts.back().get(),certs[i],keys[i],base+"/binding",0,QHostAddress::LocalHost));
            hosts.back()->start(2560,1440);
            QTRY_VERIFY_WITH_TIMEOUT(hosts.back()->canPair() && QFile::exists(base+"/host/test-sessions.json"),5000);
        }
        auto edge = [&](int from,int to) {
            return SessionGraph::Edge{tokens[from],"127.0.0.1",quint16(peers[to]->port()),QSslCertificate(certs[to]),certs[from],keys[from]};
        };
        auto reserve = [&](int from,int to) { return SessionGraph::reserve(ids[from],edge(from,to)); };
        auto receive = [&](QSslSocket& socket) {
            QElapsedTimer timer; timer.start();
            while (!socket.canReadLine() && timer.elapsed()<7000) QTest::qWait(5);
            return QJsonDocument::fromJson(socket.readLine()).object();
        };
        auto connectPeer = [&](QSslSocket& socket,int from,int to) {
            socket.setLocalCertificate(QSslCertificate(certs[from])); socket.setPrivateKey(QSslKey(keys[from],QSsl::Rsa));
            connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&socket](const QList<QSslError>& errors){socket.ignoreSslErrors(errors);});
            socket.connectToHostEncrypted("127.0.0.1",peers[to]->port());
            QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
            QCOMPARE(receive(socket)["meta"].toObject()["sessionTopology"].toInt(),1);
        };
        auto send = [](QSslSocket& socket,QJsonObject message) { socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n'); };
        const QJsonObject query{{"type","session-status"},{"sessionTakeover",1},{"sessionTopology",1}};
        auto state = [&](int i) { return PeerStore::read(dir.path()+QString("/%1/host/test-sessions.json").arg(i)); };
        if (scenario=="reservation-lifetime") {
            QVERIFY(!reserve(0,0)); QVERIFY(reserve(0,1)); QVERIFY(!reserve(0,2));
            SessionGraph::release(ids[0],"stale-token"); QCOMPARE(SessionGraph::snapshot(ids[0]).first.token,tokens[0]);
            SessionGraph::release(ids[0],tokens[0]); QVERIFY(reserve(0,2)); return;
        }
        QSslSocket a,b,c;
        if (scenario=="old-desktop" || scenario=="old-mobile") {
            connectPeer(a,0,1); auto legacy=query; legacy.remove("sessionTopology"); send(a,legacy);
            const auto result=receive(a);
            if(scenario=="old-mobile") QVERIFY(result["admitted"].toBool());
            else { QCOMPARE(result["code"].toString(),QString("topology-unsupported")); QVERIFY(state(1)["lease"].toString().isEmpty()); }
            return;
        }
        QVERIFY(reserve(0,1)); connectPeer(a,0,1);
        if(scenario=="simultaneous") { QVERIFY(reserve(1,2)); QVERIFY(reserve(2,0)); }
        send(a,query); auto result=receive(a);
        if(scenario=="simultaneous") {
            QCOMPARE(result["code"].toString(),QString("cycle"));
            connectPeer(b,1,2); connectPeer(c,2,0); send(b,query); send(c,query);
            QCOMPARE(receive(b)["code"].toString(),QString("cycle")); QCOMPARE(receive(c)["code"].toString(),QString("cycle"));
            for(int i=0;i<3;++i) QVERIFY(state(i)["lease"].toString().isEmpty()); return;
        }
        QVERIFY(result["admitted"].toBool()); const auto oldB=state(1);
        if(scenario=="takeover-cycle") {
            QVERIFY(reserve(2,1)); connectPeer(c,2,1); send(c,query); const auto occupied=receive(c);
            QVERIFY(occupied["busy"].toBool()); QVERIFY(!occupied["challenge"].toString().isEmpty());
            QVERIFY(reserve(1,2));
            send(c,{{"type","session-takeover"},{"challenge",occupied["challenge"]}});
            QCOMPARE(receive(c)["code"].toString(),QString("cycle")); QCOMPARE(state(1),oldB); return;
        }
        if(scenario=="reciprocal") {
            QVERIFY(reserve(1,0)); connectPeer(b,1,0); send(b,query);
            QCOMPARE(receive(b)["code"].toString(),QString("cycle")); QCOMPARE(state(1),oldB);
            SessionGraph::release(ids[1],tokens[1]); SessionGraph::release(ids[0],tokens[0]);
            QVERIFY(reserve(1,0)); send(b,query); QVERIFY(receive(b)["admitted"].toBool()); return;
        }
        if(scenario=="unreachable") {
            auto broken=edge(1,2); broken.port=1; QVERIFY(SessionGraph::reserve(ids[1],broken));
            QVERIFY(reserve(2,0)); connectPeer(c,2,0); send(c,query);
            QCOMPARE(receive(c)["code"].toString(),QString("topology-unavailable")); QCOMPARE(state(1),oldB); return;
        }
        QVERIFY(reserve(1,2)); connectPeer(b,1,2); send(b,query); QVERIFY(receive(b)["admitted"].toBool());
        if(scenario=="three-cycle") {
            QVERIFY(reserve(2,0)); connectPeer(c,2,0); send(c,query);
            QCOMPARE(receive(c)["code"].toString(),QString("cycle")); QCOMPARE(state(1),oldB);
            QVERIFY(state(0)["lease"].toString().isEmpty());
        }
    }
    void sessionAdmission_data() {
        QTest::addColumn<QString>("scenario");
        for (const char* name : {"recover-owner", "recover-before-eof", "recover-wrong-token", "recover-wrong-identity", "release-explicit", "client-window"}) QTest::newRow(name) << QString(name);
        QFile fixtures(qEnvironmentVariable("TEST_CORE_SESSION_CASES"));
        QVERIFY(fixtures.open(QIODevice::ReadOnly));
        for (const auto& entry : QJsonDocument::fromJson(fixtures.readAll()).object()["scenarios"].toArray()) {
            const auto name = entry.toObject()["name"].toString();
            QTest::newRow(qPrintable(name)) << name;
        }
    }
    void sessionAdmission() {
        QFETCH(QString, scenario);
        QTemporaryDir dir;
        auto fp = [](const QByteArray& cert) { return QString::fromLatin1(QSslCertificate(cert).digest(QCryptographicHash::Sha256).toHex()); };
        QJsonObject peers{{fp(credential("TEST_CERT_A")), QJsonObject{{"ready", true}, {"granted", true}}}};
        if (scenario != "unapproved") peers[fp(credential("TEST_CERT_C"))] = QJsonObject{{"ready", true}, {"granted", true}};
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version",1},{"peers",peers}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        host.start(2560,1440);
        const auto statePath = dir.path()+"/host/test-sessions.json";
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable() && QFile::exists(statePath),5000);
        auto state = [&] { QFile file(statePath); file.open(QIODevice::ReadOnly); return QJsonDocument::fromJson(file.readAll()).object(); };
        auto writeState = [&](QJsonObject object) { QFile file(statePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(object).toJson()); };
        auto connectPeer = [&](QSslSocket& socket, const char* cert, const char* key) {
            socket.setLocalCertificate(QSslCertificate(credential(cert)));
            socket.setPrivateKey(QSslKey(credential(key),QSsl::Rsa));
            connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&socket](const QList<QSslError>& errors){socket.ignoreSslErrors(errors);});
            socket.connectToHostEncrypted("127.0.0.1",quint16(manager.port()));
            QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            const auto hello = QJsonDocument::fromJson(socket.readLine()).object();
            QCOMPARE(hello["meta"].toObject()["sessionTakeover"].toInt(), 1);
        };
        auto send = [](QSslSocket& socket, QJsonObject object) { socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n'); };
        auto receive = [](QSslSocket& socket) {
            QElapsedTimer timer; timer.start();
            while (!socket.canReadLine() && timer.elapsed() < 6000) QTest::qWait(10);
            return QJsonDocument::fromJson(socket.readLine()).object();
        };
        const QJsonObject query{{"type","session-status"},{"sessionTakeover",1},{"sessionTopology",1}};
        const QJsonObject resize{{"type","display-resize"},{"seq",1},{"width",1920},{"height",1080},{"scale",1}};
        QSslSocket old, incoming, rival;
        if (scenario.startsWith("recover-") || scenario == "release-explicit") {
            auto lifecycleQuery = query; lifecycleQuery["sessionLifecycle"] = 1;
            connectPeer(old,"TEST_CERT_A","TEST_KEY_A"); send(old,lifecycleQuery);
            const auto admitted = receive(old); QVERIFY(admitted["admitted"].toBool());
            const auto token = admitted["resumeToken"].toString(); QVERIFY(!token.isEmpty());
            send(old,resize); QVERIFY(!receive(old).contains("error"));
            QSignalSpy restored(&host,&HostManager::displayResized);
            if (scenario == "release-explicit") {
                send(old,{{"type","session-release"}});
                QTRY_VERIFY(state()["lease"].toString().isEmpty());
                QTRY_VERIFY(!restored.isEmpty());
            } else {
                if (scenario != "recover-before-eof") old.abort();
                QTest::qWait(400);
                QCOMPARE(state()["lease"].toString(), token);
                QCOMPARE(restored.size(),0); // No display churn on transport loss.
                const bool same = scenario != "recover-wrong-identity";
                connectPeer(incoming,same ? "TEST_CERT_A" : "TEST_CERT_C",same ? "TEST_KEY_A" : "TEST_KEY_C");
                lifecycleQuery["resumeToken"] = scenario == "recover-wrong-token" ? "invalid" : token;
                send(incoming,lifecycleQuery); const auto recovered = receive(incoming);
                if (scenario == "recover-owner" || scenario == "recover-before-eof") {
                    QVERIFY(recovered["admitted"].toBool()); QCOMPARE(recovered["resumeToken"].toString(),token);
                    QCOMPARE(restored.size(),0);
                    send(incoming,{{"type","display-ping"}}); QCOMPARE(receive(incoming)["type"].toString(),QString("display-pong"));
                    send(incoming,{{"type","session-release"}}); QTRY_VERIFY(state()["lease"].toString().isEmpty());
                } else { QVERIFY(!recovered["admitted"].toBool()); QVERIFY(recovered["busy"].toBool()); }
                QCOMPARE(state()["takeovers"].toInt(),0);
            }
            incoming.abort(); host.stop(); return;
        }
        if (scenario == "client-window") {
            // The host offers "leave full screen" only while an opted-in client reports it.
            connectPeer(old,"TEST_CERT_A","TEST_KEY_A"); send(old,query); QVERIFY(receive(old)["admitted"].toBool());
            auto windowed = resize; windowed["clientWindow"] = 1; windowed["clientFullScreen"] = false;
            send(old,windowed); QVERIFY(!receive(old).contains("error"));
            QVERIFY(!manager.canReleaseClientFullscreen());
            auto full = windowed; full["seq"] = 2; full["width"] = 1280; full["height"] = 720; full["clientFullScreen"] = true;
            send(old,full); QVERIFY(!receive(old).contains("error"));
            QTRY_VERIFY(manager.canReleaseClientFullscreen());
            manager.releaseClientFullscreen();
            const auto request = receive(old);
            QCOMPARE(request["type"].toString(), QString("client-window"));
            QCOMPARE(request["action"].toString(), QString("leave-fullscreen"));
            // Clients that predate the state report keep the request available.
            auto legacy = resize; legacy["seq"] = 3; legacy["clientWindow"] = 1;
            send(old,legacy); QVERIFY(!receive(old).contains("error"));
            QVERIFY(manager.canReleaseClientFullscreen());
            old.abort(); host.stop(); return;
        }
        if (scenario == "unapproved") {
            connectPeer(incoming,"TEST_CERT_C","TEST_KEY_C"); send(incoming,query);
            QCOMPARE(receive(incoming)["code"].toString(), QString("unauthorized"));
            QVERIFY(state()["lease"].toString().isEmpty()); host.stop(); return;
        }
        if (scenario == "idle-repeat" || scenario == "same-identity" || scenario == "legacy-reservation") {
            connectPeer(incoming,"TEST_CERT_C","TEST_KEY_C"); send(incoming,query);
            const auto result=receive(incoming); QVERIFY(result["admitted"].toBool());
            QVERIFY(!state()["lease"].toString().isEmpty());
            if (scenario == "idle-repeat") {
                send(incoming,query); QVERIFY(receive(incoming)["admitted"].toBool());
            } else {
                connectPeer(rival,scenario == "same-identity" ? "TEST_CERT_C" : "TEST_CERT_A",
                            scenario == "same-identity" ? "TEST_KEY_C" : "TEST_KEY_A");
                send(rival,scenario == "same-identity" ? query : resize);
                if (scenario == "same-identity") QVERIFY(receive(rival)["busy"].toBool());
                else QTRY_COMPARE(rival.state(),QAbstractSocket::UnconnectedState);
            }
            incoming.abort(); rival.abort(); host.stop(); return;
        }
        connectPeer(old,"TEST_CERT_A","TEST_KEY_A"); send(old,resize);
        QVERIFY(!receive(old).contains("error"));
        auto initial=state(); initial["sessions"]=1; writeState(initial);
        connectPeer(incoming,"TEST_CERT_C","TEST_KEY_C"); send(incoming,query);
        const auto busy=receive(incoming); QVERIFY(busy["busy"].toBool()); QVERIFY(!busy["admitted"].toBool());
        const auto challenge=busy["challenge"].toString(); QVERIFY(!challenge.isEmpty());
        QCOMPARE(busy["expiresInMs"].toInt(),30000);
        if (scenario == "cancel") {
            incoming.abort(); QTest::qWait(100);
            QCOMPARE(state()["sessions"].toInt(),1);
            send(old,{{"type","display-ping"}}); QCOMPARE(receive(old)["type"].toString(),QString("display-pong"));
        } else {
            if (scenario == "changed-stream") { auto changed=state(); changed["generation"]=1; writeState(changed); }
            if (scenario == "backend-failure") { auto changed=state(); changed["fail"]=true; writeState(changed); }
            if (scenario == "expired") {
                for (int i=0;i<3;++i) { QTest::qWait(10200); send(old,{{"type","display-ping"}}); receive(old); }
            }
            QJsonObject takeover{{"type","session-takeover"},{"challenge",challenge}};
            if (scenario == "foreign-challenge" || scenario == "competing-confirmations") {
                connectPeer(rival,"TEST_CERT_A","TEST_KEY_A");
                if (scenario == "foreign-challenge") {
                    send(rival,takeover); QCOMPARE(receive(rival)["code"].toString(),QString("unauthorized"));
                    QCOMPARE(state()["sessions"].toInt(),1); incoming.abort(); rival.abort(); old.abort(); host.stop(); return;
                }
                send(rival,query); const auto rivalState=receive(rival); QVERIFY(rivalState["busy"].toBool());
                send(incoming,takeover); QVERIFY(receive(incoming)["admitted"].toBool());
                send(rival,{{"type","session-takeover"},{"challenge",rivalState["challenge"]}});
                QCOMPARE(receive(rival)["code"].toString(),QString("stale"));
                QCOMPARE(state()["takeovers"].toInt(),1); incoming.abort(); rival.abort(); old.abort(); host.stop(); return;
            }
            if (scenario == "invalid-confirmation") takeover["challenge"]="wrong";
            send(incoming,takeover); const auto result=receive(incoming);
            if (scenario == "takeover") {
                QVERIFY(result["admitted"].toBool()); QCOMPARE(state()["sessions"].toInt(),0);
                QCOMPARE(state()["takeovers"].toInt(),1);
                QTRY_COMPARE(old.state(),QAbstractSocket::UnconnectedState);
                send(incoming,resize); QVERIFY(!receive(incoming).contains("error"));
            } else {
                QCOMPARE(result["code"].toString(),scenario == "backend-failure" ? QString("unavailable") : QString("stale"));
                QVERIFY(!result["admitted"].toBool()); QCOMPARE(state()["sessions"].toInt(),1);
                send(old,{{"type","display-ping"}}); QCOMPARE(receive(old)["type"].toString(),QString("display-pong"));
            }
        }
        incoming.abort(); rival.abort(); old.abort(); host.stop();
    }
    void sharedDisplayContract_data() {
        QTest::addColumn<QJsonObject>("message");
        QTest::addColumn<bool>("accepted");
        QFile f(qEnvironmentVariable("TEST_CORE_DISPLAY_CASES"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto cases=QJsonDocument::fromJson(f.readAll()).object()["requests"].toArray();
        QVERIFY(!cases.isEmpty());
        for (const auto& value : cases) {
            const auto item=value.toObject();
            QTest::newRow(qPrintable(item["name"].toString())) << item["message"].toObject() << item["accepted"].toBool();
        }
    }
    void sharedDisplayContract() {
        QFETCH(QJsonObject, message); QFETCH(bool, accepted);
        QTemporaryDir dir;
        const auto cert=credential("TEST_CERT_A");
        const auto fp=QString::fromLatin1(QSslCertificate(cert).digest(QCryptographicHash::Sha256).toHex());
        QDir().mkpath(dir.path()+"/binding");
        QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version",1},{"peers",QJsonObject{
            {fp,QJsonObject{{"ready",true},{"granted",true}}}}}}));
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager server(&host,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        host.start(2560,1440);
        QTRY_VERIFY_WITH_TIMEOUT(host.adaptiveDisplayAvailable(),5000);
        QSslSocket socket;
        socket.setLocalCertificate(QSslCertificate(cert));
        socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"),QSsl::Rsa));
        connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&](const QList<QSslError>& errors){socket.ignoreSslErrors(errors);});
        socket.connectToHostEncrypted("127.0.0.1",quint16(server.port()));
        QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
        QCOMPARE(QJsonDocument::fromJson(socket.readLine()).object()["type"].toString(),QString("hello"));
        socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
        const auto reply=QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(reply["type"].toString(),QString("display-result"));
        QCOMPARE(reply["seq"].toInt(),message["seq"].toInt());
        if ((!reply.contains("error")) != accepted)
            qWarning() << "Display contract response" << reply << "policies" << host.displayPoliciesAvailable();
        QCOMPARE(!reply.contains("error"),accepted);
        if (accepted) {
            QCOMPARE(reply["width"].toInt(),message["width"].toInt());
            QCOMPARE(reply["height"].toInt(),message["height"].toInt());
            socket.write("{\"type\":\"display-ping\"}\n");
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QCOMPARE(QJsonDocument::fromJson(socket.readLine()).object()["type"].toString(),QString("display-pong"));
            auto changed=message;
            changed["seq"]=2;
            changed["displayPolicy"]=(message.value("displayPolicy").toInt()+1)%3;
            socket.write(QJsonDocument(changed).toJson(QJsonDocument::Compact)+'\n');
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QVERIFY(QJsonDocument::fromJson(socket.readLine()).object().contains("error"));
            message["seq"]=3;
            socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact)+'\n');
            QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(),5000);
            QVERIFY(!QJsonDocument::fromJson(socket.readLine()).object().contains("error"));
        }
        socket.abort(); host.stop();
    }
    void clientOnlyBinding_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("approved") << 0;
        QTest::newRow("declined") << 1;
        QTest::newRow("disconnect-before-approval") << 2;
        QTest::newRow("invalid-host-claim") << 3;
        QTest::newRow("ack-before-approval") << 4;
    }
    void clientOnlyBinding() {
        QFETCH(int, mode);
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        QSignalSpy imported(&manager,&PeerManager::peerBound);
        QSslSocket socket;
        socket.setLocalCertificate(QSslCertificate(credential("TEST_CERT_A")));
        socket.setPrivateKey(QSslKey(credential("TEST_KEY_A"),QSsl::Rsa));
        connect(&socket,qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),&socket,[&](const QList<QSslError>& e){socket.ignoreSslErrors(e);});
        socket.connectToHostEncrypted("127.0.0.1",quint16(manager.port()));
        QTRY_VERIFY_WITH_TIMEOUT(socket.isEncrypted(),5000);
        const auto tx=QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto send=[&](QJsonObject msg){socket.write(QJsonDocument(msg).toJson(QJsonDocument::Compact)+'\n');};
        QJsonObject meta{{"version",1},{"clientBinding",1},{"role","client"},{"name","Test tablet"},{"ready",true},{"granted",true}};
        if(mode==3) meta["hostPort"]=48989;
        send({{"type","request"},{"tx",tx},{"meta",meta}});
        if(mode!=3) {
            QTRY_COMPARE(manager.requestId(),tx);
            QVERIFY(manager.pendingClientOnly());
            QVERIFY(manager.peers().isEmpty());
            if(mode==1) manager.reject(tx);
            else if(mode==2) socket.abort();
            else if(mode==4) send({{"type","client-ready"},{"tx",tx}});
            else {
                manager.approve(tx);
                QTRY_VERIFY(!manager.peers().isEmpty() && manager.peers().first().toMap()["granted"].toBool());
                QVERIFY(!manager.peers().first().toMap()["ready"].toBool());
                send({{"type","client-ready"},{"tx",tx}});
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        QCOMPARE(imported.size(),0);
        const auto devices=PeerStore::read(dir.path()+"/host/state.json")["root"].toObject()["named_devices"].toArray();
        QCOMPARE(devices.size(),mode==0 ? 1 : 0);
        if(mode==0) {
            const auto peer=manager.peers().first().toMap();
            QVERIFY(peer["ready"].toBool()); QVERIFY(!peer.contains("hostPort"));
            manager.restoreHosts(); manager.refreshEndpoints();
            QCOMPARE(imported.size(),0); QVERIFY(!manager.busy());
            QVERIFY(!manager.editPeer(peer["fingerprint"].toString(),"Tablet","127.0.0.1",48989,48991));
            manager.revoke(peer["fingerprint"].toString());
            QTRY_VERIFY(!manager.busy()); QVERIFY(manager.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/host/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        } else QVERIFY(manager.peers().isEmpty());
        host.stop();
    }

    void outboundClientInitialization_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("valid-with-occupied-local-port") << 0;
        QTest::newRow("invalid-certificate") << 1;
        QTest::newRow("invalid-key") << 2;
        QTest::newRow("corrupt-store") << 3;
        QTest::newRow("unsupported-store") << 4;
    }
    void outboundClientInitialization() {
        QFETCH(int, failure);
        QTemporaryDir dir;
        UnavailableHost host(nullptr, dir.path()+"/host");
        QVERIFY(!host.available());
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost, 0));
        QDir().mkpath(dir.path()+"/binding");
        if (failure == 3) {
            QFile f(dir.path()+"/binding/peers.json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("{broken");
        } else if (failure == 4) {
            QVERIFY(PeerStore::write(dir.path()+"/binding/peers.json", {{"version",2},{"peers",QJsonObject()}}));
        }
        PeerManager client(&host, failure == 1 ? QByteArray("invalid") : credential("TEST_CERT_A"),
            failure == 2 ? QByteArray("invalid") : credential("TEST_KEY_A"), dir.path()+"/binding",
            occupied.serverPort(), QHostAddress::LocalHost, PeerManager::Mode::ClientOnly);
        QVERIFY(client.clientOnly()); QCOMPARE(client.port(), 0);
        for (auto listener : client.findChildren<QTcpServer*>()) QVERIFY(!listener->isListening());
        QVERIFY(!client.setConnectionPort(occupied.serverPort()));
        QVERIFY(!QFile::exists(dir.path()+"/host/state.json"));
        QVERIFY(!QFile::exists(dir.path()+"/host/credentials/cert.pem"));
        QVERIFY(!host.running());
        QSignalSpy restored(&client, &PeerManager::peerBound);
        client.restoreHosts(); QCOMPARE(restored.size(), 0);
        // An invalid local identity/store must not even attempt an outbound socket.
        if (failure) {
            client.request(QString("127.0.0.1:%1").arg(occupied.serverPort()));
            QVERIFY(!client.busy()); QVERIFY(client.findChildren<QSslSocket*>().isEmpty());
        } else QVERIFY(client.status().contains("Ready to request"));
    }
    void outboundClientBindsToProductionHost_data() {
        QTest::addColumn<int>("result");
        QTest::newRow("approved-persist-restore-control-forget") << 0;
        QTest::newRow("rejected") << 1;
        QTest::newRow("cancel-before-approval") << 2;
        QTest::newRow("host-grant-save-fails") << 3;
    }
    void outboundClientBindsToProductionHost() {
        QFETCH(int, result);
        QTemporaryDir dir;
        UnavailableHost local(nullptr, dir.path()+"/local");
        HostManager remote(nullptr, dir.path()+"/remote");
        PeerManager server(&remote, credential("TEST_CERT_B"), credential("TEST_KEY_B"),
                           dir.path()+"/server", 0, QHostAddress::LocalHost);
        auto client = std::make_unique<PeerManager>(&local, credential("TEST_CERT_A"), credential("TEST_KEY_A"),
            dir.path()+"/client", 0, QHostAddress::LocalHost, PeerManager::Mode::ClientOnly);
        QSignalSpy imported(client.get(), &PeerManager::peerBound), inbound(&server, &PeerManager::incomingRequest);
        QSignalSpy localTrust(&local, &HostManager::trustUpdated), remoteImported(&server, &PeerManager::peerBound);
        client->request(QString("127.0.0.1:%1").arg(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(inbound.size(), 1, 5000);
        QTRY_VERIFY(client->status().contains("Request received"));
        QVERIFY(server.pendingClientOnly());
        QVERIFY(client->peers().isEmpty()); QVERIFY(server.peers().isEmpty());
        server.approve("stale-approval"); QTest::qWait(30);
        QVERIFY(client->peers().isEmpty()); QCOMPARE(imported.size(), 0);
        if (result == 1) server.reject(server.requestId());
        else if (result == 2) client->cancel();
        else {
            if (result == 3) {
                QFile f(dir.path()+"/remote/state.json"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("{broken");
            }
            server.approve(server.requestId());
        }
        QTRY_VERIFY_WITH_TIMEOUT(!client->busy() && !server.busy(), 7000);
        QCOMPARE(imported.size(), result == 0 ? 1 : 0);
        QCOMPARE(localTrust.size(), 0); QCOMPARE(remoteImported.size(), 0);
        QVERIFY(!local.running()); QVERIFY(!QFile::exists(dir.path()+"/local/state.json"));
        QVERIFY(!QFile::exists(dir.path()+"/local/credentials/cert.pem"));
        if (result != 0) { QVERIFY(client->peers().isEmpty()); remote.stop(); return; }
        const auto peer = imported.first().first().toMap();
        QVERIFY(peer["ready"].toBool()); QVERIFY(peer["granted"].toBool()); QVERIFY(peer["outboundOnly"].toBool());
        QCOMPARE(peer["address"].toString(), QString("127.0.0.1"));
        QCOMPARE(peer["hostId"].toString(), remote.identity()["hostId"].toString());
        QCOMPARE(QSslCertificate(peer["hostCert"].toString().toUtf8()), QSslCertificate(credential("TEST_CERT_B")));
        QCOMPARE(QSslCertificate(peer["clientCert"].toString().toUtf8()), QSslCertificate(credential("TEST_CERT_B")));
        QCOMPARE(peer["hostPort"].toInt(), remote.basePort()); QCOMPARE(peer["bindingPort"].toInt(), server.port());
        const auto remotePeer = server.peers().first().toMap();
        QCOMPARE(remotePeer["role"].toString(), QString("client"));
        QVERIFY(remotePeer["ready"].toBool()); QVERIFY(!remotePeer.contains("hostPort"));
        auto trust = PeerStore::read(dir.path()+"/remote/state.json")["root"].toObject()["named_devices"].toArray();
        QCOMPARE(trust.size(), 1);
        QCOMPARE(QSslCertificate(trust.first().toObject()["cert"].toString().toUtf8()), QSslCertificate(credential("TEST_CERT_A")));
        client.reset();
        PeerManager restored(&local, credential("TEST_CERT_A"), credential("TEST_KEY_A"),
            dir.path()+"/client", 0, QHostAddress::LocalHost, PeerManager::Mode::ClientOnly);
        QSignalSpy reimported(&restored, &PeerManager::peerBound); restored.restoreHosts();
        QCOMPARE(reimported.size(), 1);
        const auto restoredPeer = reimported.first().first().toMap();
        QCOMPARE(restoredPeer.keys(), peer.keys());
        for (const auto& key : peer.keys())
            QVERIFY2(restoredPeer[key] == peer[key], qPrintable("Restored binding field differs: " + key));
        // Exercise the same pinned, certificate-authenticated control path used
        // before streaming, against fake host/display children only.
        QTRY_VERIFY_WITH_TIMEOUT(remote.adaptiveDisplayAvailable() &&
            QFile::exists(dir.path()+"/remote/test-sessions.json"), 5000);
        {
            AdaptiveDisplay control(peer["address"].toString(), quint16(peer["bindingPort"].toInt()),
                QSslCertificate(peer["clientCert"].toString().toUtf8()), credential("TEST_CERT_A"), credential("TEST_KEY_A"));
            auto operation = std::async(std::launch::async, [&] { return control.resize(QSize(1280,720),1); });
            QTRY_VERIFY_WITH_TIMEOUT(operation.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 11000);
            QVERIFY(operation.get()); QVERIFY(control.admissionRequired());
        }
        const auto fp = restored.peers().first().toMap()["fingerprint"].toString();
        restored.revoke(fp); QVERIFY(restored.peers().isEmpty());
        QCOMPARE(localTrust.size(), 0); QVERIFY(!local.running());
        QCOMPARE(PeerStore::read(dir.path()+"/remote/state.json")["root"].toObject()["named_devices"].toArray(), trust);
        remote.stop();
    }
    void outboundClientWireValidation_data() {
        QTest::addColumn<QString>("scenario");
        for (const auto name : {"success", "missing-capability", "invalid-host-cert", "client-host-claim", "expired-tls",
             "accept-before-pending", "ready-before-accept", "bound-before-ready", "wrong-pending-tx", "wrong-accept-tx",
             "wrong-ready-tx", "wrong-bound-tx", "duplicate-pending", "duplicate-ready", "invalid-port",
             "disconnect-before-bound", "pending-save-failure", "completed-save-failure", "known-key-change"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void outboundClientWireValidation() {
        QFETCH(QString, scenario);
        QTemporaryDir dir;
        UnavailableHost host(nullptr, dir.path()+"/host");
        PeerManager client(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"),
            dir.path()+"/binding", 0, QHostAddress::LocalHost, PeerManager::Mode::ClientOnly);
        auto remote = std::make_unique<ScriptedBindingHost>(
            credential(scenario == "expired-tls" ? "TEST_CERT_EXPIRED" : "TEST_CERT_B"), credential("TEST_KEY_B"));
        QVERIFY(remote->listen(QHostAddress::LocalHost, 0));
        const auto port = remote->serverPort();
        const auto endpoint = QString("127.0.0.1:%1").arg(port);
        QSignalSpy imported(&client, &PeerManager::peerBound);
        client.request(endpoint);
        if (scenario == "expired-tls") {
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
            QCOMPARE(imported.size(), 0); QVERIFY(client.peers().isEmpty()); return;
        }
        QTRY_VERIFY_WITH_TIMEOUT(remote->socket && remote->socket->isEncrypted(), 5000);
        // No request before the host hello/capability check.
        QTest::qWait(20); QCOMPARE(remote->messages.size(), 0);
        QJsonObject meta{{"version",1},{"clientBinding",1},{"name","Fixture host"},
            {"hostId", QUuid::createUuid().toString()}, {"hostCert",QString::fromUtf8(credential("TEST_CERT_B"))},
            {"hostPort",49089},{"bindingPort",int(port)}, {"ready",true},{"granted",true}};
        if (scenario == "missing-capability") meta.remove("clientBinding");
        if (scenario == "invalid-host-cert") meta["hostCert"] = "not a certificate";
        if (scenario == "client-host-claim") meta["role"] = "client";
        remote->send({{"type","hello"},{"meta",meta}});
        if (scenario == "missing-capability" || scenario == "invalid-host-cert" || scenario == "client-host-claim") {
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
            QCOMPARE(remote->messages.size(), 0); QVERIFY(client.peers().isEmpty()); return;
        }
        QTRY_COMPARE_WITH_TIMEOUT(remote->messages.size(), 1, 5000);
        const auto request = remote->messages.first();
        QCOMPARE(request["type"].toString(), QString("request"));
        const auto clientMeta = request["meta"].toObject();
        QCOMPARE(clientMeta.size(), 4); QCOMPARE(clientMeta["role"].toString(), QString("client"));
        QCOMPARE(clientMeta["clientBinding"].toInt(), 1); QCOMPARE(clientMeta["version"].toInt(), 1);
        QVERIFY(!clientMeta["name"].toString().isEmpty());
        const auto tx = request["tx"].toString(); QVERIFY(!QUuid(tx).isNull());
        auto send = [&](const char* type, bool wrong = false) {
            QJsonObject frame{{"type",QString::fromLatin1(type)},{"tx",wrong ? QString("wrong") : tx}};
            if (QString::fromLatin1(type) == "ready") frame["hostPort"] = scenario == "invalid-port" ? 1 : 49189;
            remote->send(frame);
        };
        if (scenario == "accept-before-pending") send("accept");
        else {
            send("pending", scenario == "wrong-pending-tx");
            if (scenario == "duplicate-pending") send("pending");
            else if (scenario == "ready-before-accept") send("ready");
            else if (scenario != "wrong-pending-tx") {
                send("accept", scenario == "wrong-accept-tx");
                if (scenario == "bound-before-ready") send("bound");
                else if (scenario != "wrong-accept-tx") {
                    if (scenario == "pending-save-failure") QVERIFY(QDir().mkdir(dir.path()+"/binding/peers.json"));
                    send("ready", scenario == "wrong-ready-tx");
                    if (scenario != "wrong-ready-tx" && scenario != "pending-save-failure" && scenario != "invalid-port") {
                        QTRY_COMPARE_WITH_TIMEOUT(remote->messages.size(), 2, 5000);
                        const auto commit = remote->messages.last();
                        QCOMPARE(commit["type"].toString(), QString("client-ready")); QCOMPARE(commit["tx"].toString(), tx);
                        QCOMPARE(imported.size(), 0);
                        const auto storedPeers = PeerStore::read(dir.path()+"/binding/peers.json")["peers"].toObject();
                        const auto saved = storedPeers.begin().value().toObject();
                        QVERIFY(!saved["ready"].toBool()); QVERIFY(saved["granted"].toBool());
                        QSignalSpy premature(&client, &PeerManager::peerBound); client.restoreHosts(); QCOMPARE(premature.size(), 0);
                        if (scenario == "duplicate-ready") send("ready");
                        else if (scenario == "disconnect-before-bound") remote->socket->disconnectFromHost();
                        else {
                            if (scenario == "completed-save-failure") {
                                QVERIFY(QFile::rename(dir.path()+"/binding/peers.json",dir.path()+"/binding/pending.json"));
                                QVERIFY(QDir().mkdir(dir.path()+"/binding/peers.json"));
                            }
                            send("bound", scenario == "wrong-bound-tx");
                        }
                    }
                }
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
        const bool success = scenario == "success" || scenario == "known-key-change";
        QCOMPARE(imported.size(), success ? 1 : 0);
        QVERIFY(!host.running()); QVERIFY(!QFile::exists(dir.path()+"/host/state.json"));
        if (success) {
            QCOMPARE(imported.first().first().toMap()["hostPort"].toInt(), 49189);
            QVERIFY(client.peers().first().toMap()["ready"].toBool());
        } else {
            for (const auto& p : client.peers()) QVERIFY(!p.toMap()["ready"].toBool());
            client.restoreHosts(); QCOMPARE(imported.size(), 0);
            UnavailableHost restartedHost(nullptr, dir.path()+"/restarted-host");
            PeerManager restarted(&restartedHost, credential("TEST_CERT_A"), credential("TEST_KEY_A"),
                dir.path()+"/binding", 0, QHostAddress::LocalHost, PeerManager::Mode::ClientOnly);
            QSignalSpy restored(&restarted, &PeerManager::peerBound);
            restarted.restoreHosts(); QCOMPARE(restored.size(), 0);
        }
        if (scenario == "known-key-change") {
            const auto saved = PeerStore::read(dir.path()+"/binding/peers.json");
            remote.reset();
            remote = std::make_unique<ScriptedBindingHost>(credential("TEST_CERT_C"), credential("TEST_KEY_C"));
            QVERIFY(remote->listen(QHostAddress::LocalHost, port));
            client.request(endpoint);
            QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 5000);
            QVERIFY(client.status().contains("different device key"));
            QCOMPARE(remote->messages.size(), 0); QCOMPARE(imported.size(), 1);
            QCOMPARE(PeerStore::read(dir.path()+"/binding/peers.json"), saved);
        }
    }

    void automaticEndpointRefresh_data() {
        QTest::addColumn<int>("failure");
        QTest::addColumn<bool>("outboundOnly");
        const QStringList scenarios {"changed-port", "wrong-stream-identity", "wrong-tls-pin", "revoked-at-server",
                                     "wrong-stream-certificate", "concurrent-local-edit", "retain-reachable-old-entry"};
        for (bool client : {false, true}) for (int i = 0; i < scenarios.size(); ++i)
            QTest::newRow(qPrintable((client ? QString("client-only-") : QString("mutual-")) + scenarios[i])) << i << client;
    }
    void automaticEndpointRefresh() {
        QFETCH(int, failure);
        QFETCH(bool, outboundOnly);
        QTemporaryDir dir;
        const auto aCert = credential("TEST_CERT_A"), bCert = credential("TEST_CERT_B");
        auto digest = [](const QByteArray& cert) {
            return QString::fromLatin1(QSslCertificate(cert).digest(QCryptographicHash::Sha256).toHex());
        };
        QDir().mkpath(dir.path()+"/bb"); QDir().mkpath(dir.path()+"/ab");
        QVERIFY(PeerStore::write(dir.path()+"/bb/peers.json", {{"version",1}, {"peers",QJsonObject{
            {digest(aCert),QJsonObject{{"ready",true},{"granted",failure != 3}}}}}}));
        HostManager bh(nullptr,dir.path()+"/bh");
        PeerManager b(&bh,bCert,credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        auto peer=bh.identity();
        const int actualPort=bh.basePort();
        peer["hostPort"]=actualPort+100;
        peer["bindingPort"]=b.port(); peer["address"]="127.0.0.1";
        peer["name"]="My saved desktop"; peer["customName"]=true;
        peer["ready"]=true; peer["granted"]=true;
        peer["requestedAddress"]=QString("127.0.0.1:%1").arg(b.port());
        peer["clientCert"]=QString::fromUtf8(bCert);
        if (failure==1) peer["hostId"]=QUuid::createUuid().toString();
        if (failure==4) peer["hostCert"]=QString::fromUtf8(credential("TEST_CERT_C"));
        const auto fp=digest(failure==2 ? credential("TEST_CERT_C") : bCert);
        const QJsonObject before{{"version",1},{"peers",QJsonObject{{fp,peer}}}};
        QVERIFY(PeerStore::write(dir.path()+"/ab/peers.json",before));
        HostManager ah(nullptr,dir.path()+"/ah");
        PeerManager a(&ah,aCert,credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost,
                      outboundOnly ? PeerManager::Mode::ClientOnly : PeerManager::Mode::PlatformDefault);
        QSignalSpy updated(&a,&PeerManager::peerBound), approval(&b,&PeerManager::incomingRequest);
        const auto status=a.status();
        if (failure==6) {
            QTcpServer reservation; QVERIFY(reservation.listen(QHostAddress::LocalHost,0));
            const int next=reservation.serverPort(); reservation.close();
            QVERIFY(b.setConnectionPort(next));
        }
        bool observedBusy = false;
        connect(&b, &PeerManager::changed, this, [&] { observedBusy = b.busy(); });
        a.refreshEndpoints();
        QVERIFY(!a.busy());
        QJsonObject edited;
        if (failure==5) {
            QVERIFY(a.editPeer(fp,"Updated locally","127.0.0.1",actualPort+200,b.port()));
            updated.clear();
            edited=PeerStore::read(dir.path()+"/ab/peers.json");
        }
        if (!failure || failure==6) {
            QTRY_COMPARE_WITH_TIMEOUT(updated.size(),1,5000);
            const auto after=PeerStore::read(dir.path()+"/ab/peers.json")["peers"].toObject()[fp].toObject();
            auto expected=peer; expected["hostPort"]=actualPort;
            expected["resolvedAddress"]="127.0.0.1";
            QCOMPARE(after,expected);
            QTest::qWait(100); a.refreshEndpoints(); QTest::qWait(200);
            QCOMPARE(updated.size(),1);
        } else {
            QTest::qWait(600);
            QCOMPARE(updated.size(),0);
            QCOMPARE(PeerStore::read(dir.path()+"/ab/peers.json"),failure==5 ? edited : before);
        }
        QCOMPARE(approval.size(),0);
        if (failure!=5) QCOMPARE(a.status(),status);
        QTRY_VERIFY(!b.busy());
        QVERIFY(!observedBusy); // QML must observe the idle transition too.
        QVERIFY(!ah.running()); QVERIFY(!bh.running());
    }

    void connectionPortKeepsOldListenerAndRejectsOccupiedPort() {
        QTemporaryDir dir;
        HostManager host(nullptr,dir.path()+"/host");
        PeerManager manager(&host,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/binding",0,QHostAddress::LocalHost);
        const int original=manager.port();
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost,0));
        QVERIFY(!manager.setConnectionPort(occupied.serverPort()));
        QVERIFY(!manager.setConnectionPort(0)); QCOMPARE(manager.port(),original);
        const int next=occupied.serverPort(); occupied.close();
        QVERIFY(manager.setConnectionPort(next)); QCOMPARE(manager.port(),next);
        QTcpServer oldProbe; QVERIFY(!oldProbe.listen(QHostAddress::LocalHost,original));
        QVERIFY(manager.setConnectionPort(original)); QCOMPARE(manager.port(),original);
        QTcpServer newProbe; QVERIFY(!newProbe.listen(QHostAddress::LocalHost,next));
    }

    void editedEndpointSurvivesRestartAndRejectsInvalidInput() {
        QTemporaryDir dir;
        const QString path = dir.path()+"/binding";
        QVERIFY(QDir().mkpath(path));
        const auto cert = credential("TEST_CERT_A"), key = credential("TEST_KEY_A");
        QVERIFY(PeerStore::write(path+"/peers.json", {{"version", 1}, {"peers", QJsonObject{
            {"saved", QJsonObject{{"address", "192.0.2.1"}, {"name", "old"}, {"ready", true},
                {"hostCert", "pinned"}, {"clientCert", "client"}, {"hostId", "stable"}}}}}}));
        {
            HostManager host(nullptr, dir.path()+"/host");
            PeerManager manager(&host,cert,key,path,0,QHostAddress::LocalHost);
            QSignalSpy updated(&manager,&PeerManager::peerBound);
            QVERIFY(manager.editPeer("saved", "My desktop", "desktop.example", 48989, 48991));
            QCOMPARE(updated.size(), 1);
            const auto saved = PeerStore::read(path+"/peers.json");
            QVERIFY(!manager.editPeer("saved", "bad", "https://host/path", 48989, 48991));
            QVERIFY(!manager.editPeer("saved", "bad", "host", 65535, 48991));
            QVERIFY(!manager.editPeer("missing", "bad", "host", 48989, 48991));
            QCOMPARE(PeerStore::read(path+"/peers.json"), saved);
        }
        HostManager host(nullptr, dir.path()+"/host");
        PeerManager manager(&host,cert,key,path,0,QHostAddress::LocalHost);
        const auto peer = manager.peers().first().toMap();
        QCOMPARE(peer["address"].toString(), QString("desktop.example"));
        QCOMPARE(peer["name"].toString(), QString("My desktop"));
        QCOMPARE(peer["hostCert"].toString(), QString("pinned"));
        QVERIFY(manager.editPeer("saved", "IPv6 desktop", "2001:db8::1", 48989, 48991));
        QCOMPARE(manager.peers().first().toMap()["requestedAddress"].toString(), QString("[2001:db8::1]:48991"));
    }
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
        auto resize = [this](AdaptiveDisplay& channel, QSize size, bool takeover = false) {
            std::atomic<int> frames {0};
            auto result = std::async(std::launch::async, [&] { return channel.resize(size, 2, [&] { ++frames; }, [takeover] { return takeover; }); });
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
            QVERIFY(!server.canReleaseClientFullscreen()); // a windowed client has nothing to leave
            channel.setFullScreen(true);
            QVERIFY(resize(channel, QSize(1600, 900)));
            QVERIFY(server.canReleaseClientFullscreen());
            server.releaseClientFullscreen();
            bool receivedWindowCommand = false;
            QTRY_VERIFY_WITH_TIMEOUT((receivedWindowCommand = receivedWindowCommand || channel.takeLeaveFullscreen()), 1500);
            QVERIFY(!channel.failed());
            QVERIFY(!channel.resumeToken().isEmpty());
            // Let the worker enter its long condition wait before submitting again.
            QTest::qWait(150);
            QElapsedTimer wakeLatency; wakeLatency.start();
            QTcpServer newEntry; QVERIFY(newEntry.listen(QHostAddress::LocalHost,0));
            const int nextEntry=newEntry.serverPort(); newEntry.close();
            QVERIFY(server.setConnectionPort(nextEntry));
            // The existing authenticated display-control channel survives a port change.
            QVERIFY(resize(channel, QSize(2560, 1440)));
            QCOMPARE(resized.size(), 3); QVERIFY(host.running());
            QVERIFY(wakeLatency.elapsed() < 2000); // Must wake on work, not the 5 s heartbeat.
            // Geometry is opt-in. Legacy desktop leases must still receive a
            // display-pong as their next message, never unsolicited caret data.
            emit host.caretChanged({{"valid",true},{"x",0.25},{"y",0.75}});
            QTest::qWait(5200); // Idle heartbeats must preserve the display lease.

            {
                AdaptiveDisplay competing("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
                QVERIFY(!resize(competing, QSize(2560, 1440)));
            }
            QTRY_VERIFY(!server.busy());
            QVERIFY(resize(channel, QSize(1600, 1000)));
            QCOMPARE(resized.size(), 4);
            QVERIFY(!host.resizeDisplay(99999, 1000, 2, 99));
            QVERIFY(!host.resizeDisplay(1600, 1000, 9, 99));
            QCOMPARE(approval.size(), 0);
            // A real admitted client must consume unsolicited takeover before
            // its five-second heartbeat, even when the host closes immediately.
            AdaptiveDisplay incoming("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
            QVERIFY(resize(incoming, QSize(1920,1080), true));
            QTRY_VERIFY_WITH_TIMEOUT(channel.failed(), 1500);
            QVERIFY(channel.wasTakenOver());
            QVERIFY(!incoming.wasTakenOver());
        }
        // A released controller cannot prevent the next connection taking over.
        QTest::qWait(100);
        AdaptiveDisplay next("127.0.0.1", server.port(), QSslCertificate(bCert), aCert, credential("TEST_KEY_A"));
        QVERIFY(resize(next, QSize(2560, 1440)));
        int clientReplies = 0;
        for (const auto& reply : resized) if (reply[0].toInt() > 0) ++clientReplies;
        QCOMPARE(clientReplies, 6);
        host.stop(); QTRY_VERIFY_WITH_TIMEOUT(!host.changing(), 5000);
    }
    void adaptiveDisplayNegotiatesFiniteModes() {
        const auto aCert=credential("TEST_CERT_A"),bCert=credential("TEST_CERT_B");
        ScriptedBindingHost server(bCert,credential("TEST_KEY_B"));
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        AdaptiveDisplay channel("127.0.0.1",server.serverPort(),QSslCertificate(bCert),aCert,credential("TEST_KEY_A"));
        auto result=std::async(std::launch::async,[&]{return channel.resize(QSize(1400,800),2);});
        QTRY_VERIFY_WITH_TIMEOUT(server.socket && server.socket->isEncrypted(),5000);
        server.send({{"type","hello"},{"meta",QJsonObject{{"adaptiveDisplay",1},{"sessionTakeover",1},{"sessionTopology",1},{"displayModes",QJsonArray{
            QJsonObject{{"width",1280},{"height",720}},QJsonObject{{"width",1920},{"height",1080}}}}}}});
        QTRY_VERIFY_WITH_TIMEOUT(!server.messages.isEmpty(),5000);
        QCOMPARE(server.messages.takeFirst()["type"].toString(),QString("session-status"));
        server.send({{"type","session-state"},{"busy",false},{"admitted",true}});
        QTRY_VERIFY_WITH_TIMEOUT(!server.messages.isEmpty(),5000);
        const auto request=server.messages.takeFirst();
        QCOMPARE(request["width"].toInt(),1280);QCOMPARE(request["height"].toInt(),720);QCOMPARE(request["scale"].toInt(),1);
        server.send({{"type","display-result"},{"seq",request["seq"]},{"width",1280},{"height",720},{"scale",1}});
        QTRY_VERIFY_WITH_TIMEOUT(result.wait_for(std::chrono::milliseconds(0))==std::future_status::ready,5000);
        QVERIFY(result.get());QCOMPARE(channel.negotiatedSize(),QSize(1280,720));
        QCOMPARE(channel.selectedSize(QSize(1400,800)),QSize(1280,720));
        QCOMPARE(channel.selectedSize(QSize(1920,1080)),QSize(1920,1080));
        QCOMPARE(channel.selectedScale(2),1);
    }
    void workspaceUsesClientSystemScale() {
        const auto fractional = DeskPortDisplay::forClient(QSize(2880, 1620), 1.5);
        QCOMPARE(fractional.pixels, QSize(3840, 2160)); QCOMPARE(fractional.scale, 2);
        QCOMPARE(DeskPortDisplay::forClient(QSize(2868, 1500), 1.5).pixels, QSize(3824, 2000));
        QCOMPARE(DeskPortDisplay::forClient(QSize(3828, 2040), 1.5).pixels, QSize(5104, 2720));
        const auto retina = DeskPortDisplay::forClient(QSize(2880, 1800), 2.0);
        QCOMPARE(retina.pixels, QSize(2880, 1800)); QCOMPARE(retina.scale, 2);
        const auto standard = DeskPortDisplay::forClient(QSize(1920, 1080), 1.0);
        QCOMPARE(standard.pixels, QSize(1920, 1080)); QCOMPARE(standard.scale, 1);
        const auto slight = DeskPortDisplay::forClient(QSize(2400, 1350), 1.25);
        QCOMPARE(slight.pixels, QSize(3840, 2160)); QCOMPARE(slight.scale, 2);
        // The 2x logical desktop keeps the 960x540 minimum.
        QCOMPARE(DeskPortDisplay::forClient(QSize(800, 450), 2.0).pixels, QSize(1920, 1080));
        QCOMPARE(DeskPortDisplay::forClient(QSize(8000, 4500), 2.0).pixels, QSize(7680, 4320));
        QVERIFY(!DeskPortDisplay::forClient(QSize(), 1.5).pixels.isValid());
        QVERIFY(!DeskPortDisplay::forClient(QSize(1920,1080), 0).pixels.isValid());
    }
    void fractionalWorkspacePreservesDetailAndUiSize() {
        // Compare the visible UI scale after fitting the stream to the window.
        // Merely asserting a chosen resolution would miss the old oversized UI.
        for (double scale : {1.0, 1.25, 1.5, 1.75, 2.0}) {
            for (QSize drawable : {QSize(2868, 1500), QSize(2400, 1600), QSize(1920, 2400)}) {
                const auto workspace = DeskPortDisplay::forClient(drawable, scale);
                QVERIFY(workspace.pixels.width() >= drawable.width());
                QVERIFY(workspace.pixels.height() >= drawable.height());
                const double visibleScale = double(workspace.scale) * drawable.width() / workspace.pixels.width();
                QVERIFY(std::abs(visibleScale - scale) < 0.005);
                QCOMPARE(workspace.pixels.width() % 4, 0);
                QCOMPARE(workspace.pixels.height() % 4, 0);
            }
        }
        const auto dense = DeskPortDisplay::forClient(QSize(3840, 2160), 3.0);
        QCOMPARE(dense.pixels, QSize(3840, 2160)); // No low-resolution upscaling above 2x.
        const auto cappedPortrait = DeskPortDisplay::forClient(QSize(1620, 2880), 1.25);
        QCOMPARE(cappedPortrait.pixels, QSize(2432, 4320));
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
    void unavailableHostReportsMissingBundledComponent() {
        QTemporaryDir dir;
        UnavailableHost host(nullptr, dir.path()+"/host");
        host.start(2560, 1440);
        QVERIFY(host.status().contains("bundled DeskPort host is missing"));
        QVERIFY(!host.running());
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
            bool completedBeforeHostReady = false;
            connect(&b, &PeerManager::peerBound, &b, [&] { completedBeforeHostReady = !bh.canPair(); });
            a.request(QString("localhost:%1").arg(b.port()));
            QTRY_COMPARE_WITH_TIMEOUT(incoming.size(), 1, 5000);
            QTRY_VERIFY(a.status().contains("Request received"));
            QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
            b.approve("wrong-transaction"); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
            b.approve(b.requestId());
            QTRY_COMPARE_WITH_TIMEOUT(aDone.size(),1,7000);
            QTRY_COMPARE_WITH_TIMEOUT(bDone.size(),1,7000);
            // Check at the peerBound emission itself, before any later event
            // loop turn can make the host ready.
            QVERIFY(!completedBeforeHostReady);
            QCOMPARE(a.peers().first().toMap()["os"].toString(), QSysInfo::prettyProductName());
            QCOMPARE(a.peers().first().toMap()["address"].toString(), QString("localhost"));
            QCOMPARE(b.peers().first().toMap()["address"].toString(), QHostInfo::localHostName());
            QVERIFY(a.peers().first().toMap()["ready"].toBool()); QVERIFY(b.peers().first().toMap()["ready"].toBool());
            auto clients=PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray();
            QCOMPARE(clients.size(),1); QCOMPARE(QSslCertificate(clients[0].toObject()["cert"].toString().toUtf8()),QSslCertificate(bCert));
            QFile metadata(dir.path()+"/ab/peers.json"); QVERIFY(metadata.open(QIODevice::ReadOnly)); QVERIFY(!metadata.readAll().contains("PRIVATE KEY"));
            auto legacy = PeerStore::read(dir.path()+"/ab/peers.json");
            auto peers = legacy["peers"].toObject();
            auto it = peers.begin(); auto peer = it.value().toObject();
            peer["address"] = "127.0.0.1"; peer.remove("resolvedAddress");
            it.value() = peer; legacy["peers"] = peers;
            QVERIFY(PeerStore::write(dir.path()+"/ab/peers.json", legacy));
        }
        {
            HostManager ah(nullptr,dir.path()+"/ah"),bh(nullptr,dir.path()+"/bh");
            PeerManager a(&ah,aCert,aKey,dir.path()+"/ab",0,QHostAddress::LocalHost);
            PeerManager b(&bh,bCert,bKey,dir.path()+"/bb",0,QHostAddress::LocalHost);
            QCOMPARE(ah.identity()["hostId"].toString(),aId); QCOMPARE(bh.identity()["hostId"].toString(),bId);
            QSignalSpy restored(&a,&PeerManager::peerBound); a.restoreHosts(); QCOMPARE(restored.size(),1);
            QCOMPARE(restored.first().first().toMap()["address"].toString(), QString("localhost"));
            HostManager ch(nullptr,dir.path()+"/ch");
            PeerManager c(&ch,credential("TEST_CERT_C"),credential("TEST_KEY_C"),dir.path()+"/cb",quint16(a.peers().first().toMap()["bindingPort"].toInt()),QHostAddress::LocalHost);
            QSignalSpy substituted(&c,&PeerManager::incomingRequest);
            a.request(QString("localhost:%1").arg(c.port()));
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000);
            QCOMPARE(substituted.size(),0); QVERIFY(c.peers().isEmpty());
            QVERIFY(a.status().contains("different device key"));
            a.revoke(a.peers().first().toMap()["fingerprint"].toString());
            QTRY_VERIFY_WITH_TIMEOUT(!a.busy(),5000); QVERIFY(a.peers().isEmpty());
            QCOMPARE(PeerStore::read(dir.path()+"/ah/state.json")["root"].toObject()["named_devices"].toArray().size(),0);
        }
    }
    void bindingPreservesRunningHostAndLease_data() {
        QTest::addColumn<bool>("reject");
        QTest::newRow("live-grant") << false;
        QTest::newRow("unsupported-helper-no-restart") << true;
    }
    void bindingPreservesRunningHostAndLease() {
        QFETCH(bool, reject);
        QTemporaryDir dir;
        HostManager ah(nullptr, dir.path()+"/ah"), bh(nullptr, dir.path()+"/bh");
        PeerManager a(&ah,credential("TEST_CERT_A"),credential("TEST_KEY_A"),dir.path()+"/ab",0,QHostAddress::LocalHost);
        PeerManager b(&bh,credential("TEST_CERT_B"),credential("TEST_KEY_B"),dir.path()+"/bb",0,QHostAddress::LocalHost);
        bh.start(2560,1440);
        const auto statePath = dir.path()+"/bh/test-sessions.json";
        QTRY_VERIFY_WITH_TIMEOUT(bh.canPair() && QFile::exists(statePath),5000);
        const QJsonObject active{{"generation",17},{"sessions",1},{"lease","existing-controller"}};
        QVERIFY(PeerStore::write(statePath, active));
        if (reject) { QFile marker(dir.path()+"/bh/reject-live-trust"); QVERIFY(marker.open(QIODevice::WriteOnly)); }
        bool interrupted = false;
        connect(&bh, &HostManager::changed, &bh, [&] { if (!bh.canPair()) interrupted = true; });
        QSignalSpy incoming(&b,&PeerManager::incomingRequest), bound(&b,&PeerManager::peerBound), trust(&bh,&HostManager::trustUpdated);
        a.request(QString("127.0.0.1:%1").arg(b.port()));
        QTRY_COMPARE_WITH_TIMEOUT(incoming.size(),1,5000);
        b.approve(b.requestId());
        QTRY_COMPARE_WITH_TIMEOUT(trust.size(),1,7000);
        QCOMPARE(trust.first().first().toBool(), !reject);
        if (!reject) QTRY_COMPARE_WITH_TIMEOUT(bound.size(),1,7000);
        else QCOMPARE(bound.size(),0);
        QVERIFY(!interrupted);
        QVERIFY(bh.canPair());
        QCOMPARE(PeerStore::read(statePath),active);
        const auto records = PeerStore::read(dir.path()+"/bh/state.json")["root"].toObject()["named_devices"].toArray();
        QCOMPARE(records.size(),reject ? 0 : 1);
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
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        b.reject(b.requestId());
        QTRY_VERIFY(!a.busy()); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(),5000);
        const QString stale=b.requestId(); a.cancel(); QTRY_VERIFY(!b.busy());
        b.approve(stale); QVERIFY(a.peers().isEmpty()); QVERIFY(b.peers().isEmpty());
    }
};
QTEST_MAIN(PeerBinding)
#include "peer-binding.moc"
