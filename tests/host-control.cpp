#include <QApplication>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include "cli/hostcontrol.h"
#include "hostmanager.h"
#include "seamlesshostmanager.h"
#include <QStandardPaths>
#include "peermanager.h"
#include "peerstore.h"
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

static QByteArray credential(const char* name) {
    QFile file(qEnvironmentVariable(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static QByteArray command(const QStringList& args) {
    QJsonArray values;
    for (const auto& value : args) values.append(value);
    return QJsonDocument(QJsonObject{{"version", 1}, {"args", values}}).toJson(QJsonDocument::Compact) + '\n';
}
static QJsonObject reply(QLocalSocket& socket) {
    return QJsonDocument::fromJson(socket.readLine()).object();
}
static void send(QLocalSocket& socket, const QStringList& args) {
    socket.connectToServer(DeskPortCli::socketName());
    QVERIFY(socket.waitForConnected(1000));
    socket.write(command(args));
    socket.flush();
}

class HostControl : public QObject {
    Q_OBJECT
private slots:
    void seamlessWindowStatesReachLocalStatus() {
        QTemporaryDir dir;
        const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        QVERIFY(!python.isEmpty());
        const auto path = dir.path() + "/fake-seamless-host";
        QFile helper(path);
        QVERIFY(helper.open(QIODevice::WriteOnly));
        helper.write("#!" + QFile::encodeName(python) + R"PY(
import json, time
events = [
    dict(version=1, type="ready", socket="deskport-test-1", width=1280, height=720),
    dict(version=1, type="window-create", id=1, width=320, height=200, title="Terminal", appId="org.example.Terminal"),
    dict(version=1, type="window-damage", id=1, sequence=1, bufferType="wl_shm", rectangles=[], rectanglesTruncated=False),
    dict(version=1, type="frame-metadata", id=1, sequence=1, width=320, height=200, pixelFormat="rgba8888-premultiplied", sha256="a"*64, damage=[], damageTruncated=False),
]
for event in events:
    print(json.dumps(event), flush=True)
time.sleep(30)
)PY");
        helper.close();
        QVERIFY(helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("DESKPORT_SEAMLESS_HOST_BINARY", QFile::encodeName(path));
        // Clear the fixture override even when a QtTest assertion returns early.
        const auto reset = qScopeGuard([] { qunsetenv("DESKPORT_SEAMLESS_HOST_BINARY"); });
        SeamlessHostManager seamless;
        QSignalSpy events(&seamless, &SeamlessHostManager::eventReceived);
        QVERIFY(seamless.start());
        QTRY_COMPARE_WITH_TIMEOUT(events.size(), 4, 5000);
        HostManager host(nullptr, dir.path()+"/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path()+"/binding", 0,
                          QHostAddress::LocalHost, PeerManager::Mode::SeamlessHost);
        DeskPortCli::ControlServer server(&host, &peers, nullptr, QString(), QStringLiteral("seamless"));
        server.setStatusExtension([&] {
            return seamless.localStatus();
        });
        QVERIFY(server.listen());
        QLocalSocket query; send(query, {"status"});
        QTRY_VERIFY(query.canReadLine());
        const auto response = reply(query);
        QVERIFY(response["ok"].toBool());
        const auto state = response["data"].toObject();
        QCOMPARE(state["mode"].toString(), QString("seamless"));
        QVERIFY(state["ready"].toBool());
        QCOMPARE(state["readiness"].toString(), QString("experimental"));
        QCOMPARE(state["waylandSocket"].toString(), QString("deskport-test-1"));
        QCOMPARE(state["windows"].toInt(), 1);
        const auto windows = state["windowStates"].toArray();
        QCOMPARE(windows, seamless.windowStates());
        QCOMPARE(windows.first().toObject()["title"].toString(), QString("Terminal"));
        QCOMPARE(windows.first().toObject()["lastFrame"].toObject()["sha256"].toString(), QString(64, 'a'));
        QProcess client;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "does-not-exist");
        environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
        client.setProcessEnvironment(environment);
        client.start(QCoreApplication::applicationFilePath(), {"--control-client", "status", "--json"});
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QProcess::NotRunning, 5000);
        QCOMPARE(client.exitCode(), 0);
        const auto cli = QJsonDocument::fromJson(client.readAllStandardOutput()).object();
        QVERIFY(cli["ok"].toBool());
        QCOMPARE(cli["data"].toObject()["windowStates"].toArray(), windows);
        QSignalSpy stopped(&seamless, &SeamlessHostManager::stopped);
        seamless.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.size(), 1, 7000);
        QLocalSocket ended; send(ended, {"status"}); QTRY_VERIFY(ended.canReadLine());
        const auto endedState = reply(ended)["data"].toObject();
        QCOMPARE(endedState["windows"].toInt(), 0);
        QVERIFY(endedState["windowStates"].toArray().isEmpty());
    }

    void browserPairingsCanBeListedAndRemovedByExactId() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path()+"/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        QJsonArray browsers{QJsonObject{{"id", "browser-one"}, {"name", "Chrome"}},
                            QJsonObject{{"id", "browser-two"}, {"name", "Safari"}}};
        bool writable = false;
        QString removed;
        server.setBrowserPairings([&] { return browsers; }, [&](const QString& id) {
            if (!writable) return false;
            removed = id;
            for (int i = 0; i < browsers.size(); ++i)
                if (browsers[i].toObject()["id"].toString() == id) { browsers.removeAt(i); return true; }
            return false;
        });
        QLocalSocket list; send(list, {"web", "list"}); QTRY_VERIFY(list.canReadLine());
        const auto result = reply(list);
        QVERIFY(result["ok"].toBool()); QCOMPARE(result["data"].toObject()["browsers"].toArray(), browsers);
        QVERIFY(!result["data"].toObject().contains("accessCode"));
        QLocalSocket stale; send(stale, {"web", "remove", "browser"}); QTRY_VERIFY(stale.canReadLine());
        QCOMPARE(reply(stale)["code"].toString(), QString("not-found")); QVERIFY(removed.isEmpty());
        QLocalSocket denied; send(denied, {"web", "remove", "browser-one"}); QTRY_VERIFY(denied.canReadLine());
        QCOMPARE(reply(denied)["code"].toString(), QString("remove-failed")); QCOMPARE(browsers.size(), 2);
        writable = true;
        QLocalSocket remove; send(remove, {"web", "remove", "browser-one"}); QTRY_VERIFY(remove.canReadLine());
        const auto done = reply(remove); QVERIFY(done["ok"].toBool());
        QCOMPARE(done["data"].toObject()["removed"].toString(), QString("browser-one"));
        QCOMPARE(removed, QString("browser-one")); QCOMPARE(browsers.size(), 1);
        QCOMPARE(browsers.first().toObject()["id"].toString(), QString("browser-two"));
    }
    void browserAccessIsExplicitLocalQuery() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path()+"/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        server.setBrowserInfo([] { return QJsonObject{{"enabled", true}, {"accessCode", "TEST42"},
            {"urls", QJsonArray{"https://127.0.0.1:48992/"}}}; });
        QLocalSocket regular;
        send(regular, {"status"});
        QTRY_VERIFY(regular.canReadLine());
        const auto state = reply(regular);
        QVERIFY(state["ok"].toBool());
        QVERIFY(!state["data"].toObject().contains("accessCode"));
        QLocalSocket browser;
        send(browser, {"web", "info"});
        QTRY_VERIFY(browser.canReadLine());
        const auto web = reply(browser);
        QVERIFY(web["ok"].toBool());
        QCOMPARE(web["data"].toObject()["accessCode"].toString(), QString("TEST42"));
        QVERIFY(!web["data"].toObject()["urls"].toArray().first().toString().contains("TEST42"));
        QVERIFY(!web["data"].toObject().contains("authenticatorUri")); // The secret is never part of info.
        // The authenticator secret is exported only by its own explicit request.
        QLocalSocket missing; send(missing, {"web", "authenticator"}); QTRY_VERIFY(missing.canReadLine());
        QCOMPARE(reply(missing)["code"].toString(), QString("unavailable"));
        int resets = 0; bool resetWorks = false;
        server.setBrowserAuthenticator([] { return QString("otpauth://totp/DeskPort:test?secret=AAAA"); },
                                       [&] { ++resets; return resetWorks; });
        QLocalSocket authenticator; send(authenticator, {"web", "authenticator"}); QTRY_VERIFY(authenticator.canReadLine());
        const auto exported = reply(authenticator);
        QVERIFY(exported["ok"].toBool());
        QCOMPARE(exported["data"].toObject()["authenticatorUri"].toString(), QString("otpauth://totp/DeskPort:test?secret=AAAA"));
        QLocalSocket failedReset; send(failedReset, {"web", "reset-code"}); QTRY_VERIFY(failedReset.canReadLine());
        QCOMPARE(reply(failedReset)["code"].toString(), QString("reset-failed"));
        resetWorks = true;
        QLocalSocket reset; send(reset, {"web", "reset-code"}); QTRY_VERIFY(reset.canReadLine());
        QVERIFY(reply(reset)["data"].toObject()["codeReset"].toBool()); QCOMPARE(resets, 2);
    }
    void invitationCliOutputAndRevocation() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path()+"/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path()+"/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "does-not-exist");
        environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--control-client","devices","invite","--address","VPS.example","--json"});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitCode(), 0);
        const auto bytes = child.readAllStandardOutput(); QVERIFY(!bytes.contains('\x1b'));
        const auto result = QJsonDocument::fromJson(bytes).object(); QVERIFY(result["ok"].toBool());
        const auto data = result["data"].toObject();
        QCOMPARE(data["entry"].toString(), QString("vps.example:%1").arg(peers.port()));
        const QUrlQuery query(QUrl(data["uri"].toString()));
        QCOMPARE(query.queryItems().size(), 6);
        QCOMPARE(query.queryItemValue("fp"), QString::fromLatin1(QSslCertificate(credential("TEST_CERT_A")).digest(QCryptographicHash::Sha256).toHex()));
        const auto remaining = qint64(data["expiresAt"].toDouble()) - QDateTime::currentSecsSinceEpoch();
        QVERIFY(remaining > 295 && remaining <= 300);
        QLocalSocket status; send(status, {"status"}); QTRY_VERIFY(status.canReadLine());
        QVERIFY(!status.readLine().contains(query.queryItemValue("token").toUtf8()));
        child.start(QCoreApplication::applicationFilePath(), {"--control-client","devices","invite","--address","[2001:db8::8]:55001"});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitCode(), 0);
        const auto terminal = child.readAllStandardOutput();
        QVERIFY(terminal.contains("\x1b[30;107m")); QVERIFY(terminal.contains("deskport://bind?"));
        QVERIFY(terminal.contains("terminal columns"));
        QVERIFY(terminal.contains("Expires:")); QVERIFY(terminal.contains("grants one client access"));
        const auto evidence = qEnvironmentVariable("DESKPORT_QR_TEST_OUTPUT");
        if (!evidence.isEmpty()) { QFile output(evidence); QVERIFY(output.open(QIODevice::WriteOnly)); QCOMPARE(output.write(terminal), qint64(terminal.size())); }
        QLocalSocket revoke; send(revoke, {"devices","revoke-invite"}); QTRY_VERIFY(revoke.canReadLine());
        QVERIFY(reply(revoke)["data"].toObject()["revoked"].toBool());
        QLocalSocket again; send(again, {"devices","revoke-invite"}); QTRY_VERIFY(again.canReadLine());
        const auto repeated = reply(again); QVERIFY(repeated["ok"].toBool()); QVERIFY(!repeated["data"].toObject()["revoked"].toBool());
        QVERIFY(!host.running()); QVERIFY(peers.peers().isEmpty());
    }
    void invalidInvitationAddresses_data() {
        QTest::addColumn<QString>("address");
        for (const auto& value : QStringList{"", "host:0", "host:65536", "https://host", "user@host", "host/path", "host?x", "host#x", "host\n", "[fe80::1%en0]:48991", "2001:db8::1", "-bad.example", "bad..example", "999.999.999.999:48991", QString(254,'a')})
            QTest::newRow(qPrintable(value)) << value;
    }
    void invalidInvitationAddresses() {
        QFETCH(QString, address);
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("QT_QPA_PLATFORM","does-not-exist"); child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--control-client","devices","invite","--address",address,"--json"});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 2);
        QCOMPARE(QJsonDocument::fromJson(child.readAllStandardOutput()).object()["code"].toString(), QString("usage"));
    }
    void init() {
        QSettings().clear();
        qputenv("DESKPORT_TEST_MODE", "normal");
    }
    void parserRejectsInvalidCommandsWithoutGui() {
        QVERIFY(DeskPortCli::isControlCommand({"deskport", "status"}));
        QVERIFY(!DeskPortCli::isControlCommand({"deskport", "stream"}));
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "does-not-exist");
        environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--control-client", "config", "set", "port", "22", "--json"});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 2);
        QCOMPARE(QJsonDocument::fromJson(child.readAllStandardOutput()).object()["code"].toString(), QString("usage"));
        child.start(QCoreApplication::applicationFilePath(), {"--control-client", "status", "--json"});
        QVERIFY(child.waitForFinished(5000));
        QCOMPARE(child.exitCode(), 1);
        QCOMPARE(QJsonDocument::fromJson(child.readAllStandardOutput()).object()["code"].toString(), QString("unavailable"));
    }
    void socketUsesPrivateOwnedDirectoryAndRejectsSymlinks() {
#ifdef Q_OS_UNIX
        QTemporaryDir config;
        const auto name = DeskPortCli::socketName(config.path());
        const auto directory = QFileInfo(name).absolutePath();
        QVERIFY(name.toUtf8().size() < 104);
        HostManager host(nullptr, config.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), config.path() + "/binding", 0, QHostAddress::LocalHost);
        {
            DeskPortCli::ControlServer server(&host, &peers, nullptr, config.path()); QVERIFY(server.listen());
            struct stat state {}; QVERIFY(::lstat(QFile::encodeName(directory).constData(), &state) == 0);
            QVERIFY(S_ISDIR(state.st_mode)); QCOMPARE(state.st_mode & 0777, mode_t(0700)); QCOMPARE(state.st_uid, ::getuid());
        }
        QVERIFY(!QFileInfo::exists(directory));
        QTemporaryDir target;
        QVERIFY(QFile::link(target.path(), directory));
        {
            DeskPortCli::ControlServer server(&host, &peers, nullptr, config.path()); QVERIFY(!server.listen());
            QVERIFY(server.errorString().contains("0700"));
        }
        QVERIFY(QFileInfo(directory).isSymLink()); QVERIFY(QFile::remove(directory));
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup));
        {
            DeskPortCli::ControlServer server(&host, &peers, nullptr, config.path()); QVERIFY(!server.listen());
        }
        QVERIFY(QDir().rmdir(directory));
#endif
    }
    void readOnlyClientAndRedaction() {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + "/binding"));
        QVERIFY(PeerStore::write(dir.path() + "/binding/peers.json", {{"version", 1}, {"peers", QJsonObject{{"test-fingerprint", QJsonObject{
            {"name", "Fixture"}, {"hostId", "fixture-id"}, {"ready", true}, {"granted", true},
            {"hostCert", "SECRET-CERTIFICATE"}, {"key", "SECRET-PRIVATE-KEY"}}}}}}));
        HostManager host(nullptr, dir.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers);
        QVERIFY(server.listen());
        QLocalSocket socket;
        send(socket, {"devices", "list"});
        QTRY_VERIFY(socket.canReadLine());
        const auto bytes = socket.readLine();
        QVERIFY(!bytes.contains("SECRET")); QVERIFY(!bytes.contains("hostCert"));
        const auto result = QJsonDocument::fromJson(bytes).object();
        QVERIFY(result["ok"].toBool());
        QCOMPARE(result["data"].toObject()["devices"].toArray().first().toObject()["id"].toString(), QString("fixture-id"));
        QProcess child;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_QPA_PLATFORM", "does-not-exist");
        environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
        child.setProcessEnvironment(environment);
        child.start(QCoreApplication::applicationFilePath(), {"--control-client", "status", "--json"});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 5000);
        QCOMPARE(child.exitCode(), 0);
        QVERIFY(QJsonDocument::fromJson(child.readAllStandardOutput()).object()["ok"].toBool());
        QVERIFY(!host.running());
    }
    void malformedOversizedAndStalledRequestsAreBounded() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        QLocalSocket malformed;
        malformed.connectToServer(DeskPortCli::socketName()); QVERIFY(malformed.waitForConnected(1000));
        malformed.write("{\"version\":1,\"args\":[17]}\n"); malformed.flush();
        QTRY_VERIFY(malformed.canReadLine()); QCOMPARE(reply(malformed)["code"].toString(), QString("protocol"));
        QLocalSocket oversized;
        oversized.connectToServer(DeskPortCli::socketName()); QVERIFY(oversized.waitForConnected(1000));
        oversized.write(QByteArray(8193, 'x')); oversized.flush();
        QTRY_VERIFY(oversized.canReadLine()); QCOMPARE(reply(oversized)["code"].toString(), QString("too-large"));
        QLocalSocket stalled;
        stalled.connectToServer(DeskPortCli::socketName()); QVERIFY(stalled.waitForConnected(1000));
        stalled.write("{"); stalled.flush();
        QTRY_VERIFY_WITH_TIMEOUT(stalled.canReadLine(), 4000);
        QCOMPARE(reply(stalled)["code"].toString(), QString("timeout"));
        QVERIFY(!host.running());
    }
    void sharingWaitsForStartupAndStopAndRejectsConcurrentMutation() {
        QTemporaryDir dir;
        qputenv("DESKPORT_TEST_MODE", "auth-gated");
        HostManager host(nullptr, dir.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        QLocalSocket start;
        send(start, {"sharing", "start"});
        QTRY_VERIFY(QFile::exists(dir.path() + "/host/auth-started"));
        QVERIFY(!start.canReadLine());
        QLocalSocket conflict; send(conflict, {"sharing", "stop"});
        QTRY_VERIFY(conflict.canReadLine()); QCOMPARE(reply(conflict)["code"].toString(), QString("busy"));
        QFile release(dir.path() + "/host/auth-release"); QVERIFY(release.open(QIODevice::WriteOnly)); release.close();
        QTRY_VERIFY_WITH_TIMEOUT(start.canReadLine(), 5000);
        QVERIFY(reply(start)["ok"].toBool()); QVERIFY(host.canPair());
        QLocalSocket stop; send(stop, {"sharing", "stop"});
        QTRY_VERIFY_WITH_TIMEOUT(stop.canReadLine(), 5000);
        QVERIFY(reply(stop)["ok"].toBool()); QVERIFY(!host.running());
    }
    void startupFailureIsReported() {
        QTemporaryDir dir;
        qputenv("DESKPORT_TEST_MODE", "auth-fail");
        HostManager host(nullptr, dir.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        QLocalSocket socket; send(socket, {"sharing", "start"});
        QTRY_VERIFY_WITH_TIMEOUT(socket.canReadLine(), 5000);
        QCOMPARE(reply(socket)["code"].toString(), QString("start-failed"));
        QVERIFY(!host.running());
    }
    void approvalRequiresExactRequestAndWaitsForCompletion_data() {
        QTest::addColumn<bool>("clientOnly");
        QTest::newRow("desktop-host-id") << false;
        QTest::newRow("mobile-client-fingerprint") << true;
    }
    void approvalRequiresExactRequestAndWaitsForCompletion() {
        QFETCH(bool, clientOnly);
        QTemporaryDir dir;
        HostManager aHost(nullptr, dir.path() + "/ah", false), bHost(nullptr, dir.path() + "/bh", false);
        PeerManager a(&aHost, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/ab", 0, QHostAddress::LocalHost,
                      clientOnly ? PeerManager::Mode::ClientOnly : PeerManager::Mode::PlatformDefault);
        PeerManager b(&bHost, credential("TEST_CERT_B"), credential("TEST_KEY_B"), dir.path() + "/bb", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&bHost, &b); QVERIFY(server.listen());
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(), 5000);
        const auto id = b.requestId();
        QLocalSocket wrong; send(wrong, {"devices", "approve", id + "-wrong"});
        QTRY_VERIFY(wrong.canReadLine()); QCOMPARE(reply(wrong)["code"].toString(), QString("stale-request"));
        QCOMPARE(b.requestId(), id); QVERIFY(b.peers().isEmpty());
        QLocalSocket pending; send(pending, {"devices", "pending"});
        QTRY_VERIFY(pending.canReadLine()); QCOMPARE(reply(pending)["data"].toObject()["requestId"].toString(), id);
        QLocalSocket approve; send(approve, {"devices", "approve", id});
        QTRY_VERIFY_WITH_TIMEOUT(approve.canReadLine(), 10000);
        QVERIFY(reply(approve)["ok"].toBool()); QVERIFY(!b.busy());
        QVERIFY(!b.peers().isEmpty()); QVERIFY(b.peers().first().toMap()["ready"].toBool());
        const auto peer = b.peers().first().toMap();
        const auto deviceId = peer[clientOnly ? "fingerprint" : "hostId"].toString();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.path() + "/bh/test-sessions.json"), 5000);
        QFile deny(dir.path() + "/bh/reject-live-trust"); QVERIFY(deny.open(QIODevice::WriteOnly)); deny.close();
        QLocalSocket rejectedRemoval; send(rejectedRemoval, {"devices", "remove", deviceId});
        QTRY_VERIFY_WITH_TIMEOUT(rejectedRemoval.canReadLine(), 5000);
        QCOMPARE(reply(rejectedRemoval)["code"].toString(), QString("remove-failed"));
        QCOMPARE(b.peers().size(), 1);
        QVERIFY(deny.remove());
        QLocalSocket remove; send(remove, {"devices", "remove", deviceId});
        QTRY_VERIFY_WITH_TIMEOUT(remove.canReadLine(), 5000);
        QVERIFY(reply(remove)["ok"].toBool()); QVERIFY(b.peers().isEmpty());
        aHost.stop(); bHost.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!aHost.running() && !bHost.running(), 5000);
    }
    void failedApprovalReturnsFailureInsteadOfAcknowledgement() {
        QTemporaryDir dir;
        HostManager aHost(nullptr, dir.path() + "/ah", false), bHost(nullptr, dir.path() + "/bh", false);
        PeerManager a(&aHost, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/ab", 0, QHostAddress::LocalHost);
        PeerManager b(&bHost, credential("TEST_CERT_B"), credential("TEST_KEY_B"), dir.path() + "/bb", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&bHost, &b); QVERIFY(server.listen());
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(), 5000);
        QLockFile lock(dir.path() + "/bh/instance.lock"); QVERIFY(lock.tryLock());
        QLocalSocket approve; send(approve, {"devices", "approve", b.requestId()});
        QTRY_VERIFY_WITH_TIMEOUT(approve.canReadLine(), 5000);
        QCOMPARE(reply(approve)["code"].toString(), QString("approval-failed"));
        QVERIFY(!b.busy()); QVERIFY(!bHost.running());
        for (const auto& peer : b.peers()) QVERIFY(!peer.toMap()["ready"].toBool());
    }
    void rejectedRequestReturnsMatchingId() {
        QTemporaryDir dir;
        HostManager aHost(nullptr, dir.path() + "/ah", false), bHost(nullptr, dir.path() + "/bh", false);
        PeerManager a(&aHost, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/ab", 0, QHostAddress::LocalHost);
        PeerManager b(&bHost, credential("TEST_CERT_B"), credential("TEST_KEY_B"), dir.path() + "/bb", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&bHost, &b); QVERIFY(server.listen());
        a.request(QString("localhost:%1").arg(b.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!b.requestId().isEmpty(), 5000);
        const auto id = b.requestId();
        QLocalSocket reject; send(reject, {"devices", "reject", id});
        QTRY_VERIFY(reject.canReadLine()); const auto result = reply(reject);
        QVERIFY(result["ok"].toBool()); QCOMPARE(result["data"].toObject()["requestId"].toString(), id);
        QVERIFY(b.peers().isEmpty()); QVERIFY(!b.busy());
    }
    void configValidationAndPortFailurePreserveCurrentValue() {
        QTemporaryDir dir;
        HostManager host(nullptr, dir.path() + "/host", false);
        PeerManager peers(&host, credential("TEST_CERT_A"), credential("TEST_KEY_A"), dir.path() + "/binding", 0, QHostAddress::LocalHost);
        DeskPortCli::ControlServer server(&host, &peers); QVERIFY(server.listen());
        const int originalPort = peers.port();
        QLocalSocket invalid; send(invalid, {"config", "set", "name", "new\nconfig"});
        QTRY_VERIFY(invalid.canReadLine()); QCOMPARE(reply(invalid)["code"].toString(), QString("usage"));
        QTcpServer occupied; QVERIFY(occupied.listen(QHostAddress::LocalHost));
        QLocalSocket conflict; send(conflict, {"config", "set", "port", QString::number(occupied.serverPort())});
        QTRY_VERIFY(conflict.canReadLine()); QCOMPARE(reply(conflict)["code"].toString(), QString("config-failed"));
        QCOMPARE(peers.port(), originalPort);
        const auto newPort = occupied.serverPort(); occupied.close();
        QLocalSocket port; send(port, {"config", "set", "port", QString::number(newPort)});
        QTRY_VERIFY(port.canReadLine()); QVERIFY(reply(port)["ok"].toBool()); QCOMPARE(peers.port(), int(newPort));
        QLocalSocket name; send(name, {"config", "set", "name", "Fixture host"});
        QTRY_VERIFY(name.canReadLine()); QVERIFY(reply(name)["ok"].toBool()); QCOMPARE(host.deviceName(), QString("Fixture host"));
        QLocalSocket missing; send(missing, {"devices", "remove", "does-not-exist"});
        QTRY_VERIFY(missing.canReadLine()); QCOMPARE(reply(missing)["code"].toString(), QString("not-found"));
    }
};

int main(int argc, char** argv) {
    const bool client = argc > 1 && QByteArray(argv[1]) == "--control-client";
    std::unique_ptr<QCoreApplication> app(client ? new QCoreApplication(argc, argv) : new QApplication(argc, argv));
    const auto directory = qEnvironmentVariable("DESKPORT_CLI_TEST_CONFIG");
    if (directory.isEmpty()) return 3;
    QCoreApplication::setOrganizationName("DeskPortCliTests");
    QCoreApplication::setApplicationName(QString::number(qHash(directory)));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    if (client) {
        auto arguments = QCoreApplication::arguments(); arguments.removeAt(1);
        return DeskPortCli::runControlCommand(arguments);
    }
    HostControl suite;
    return QTest::qExec(&suite, argc, argv);
}
#include "host-control.moc"
