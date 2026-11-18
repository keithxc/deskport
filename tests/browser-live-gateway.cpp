// Isolated end-to-end fixture only. This is never linked into DeskPort.
// Its upstream is an explicitly configured loopback Sunshine fixture, while
// Xvfb owns the test display. Production display policy lives in BrowserHost.
#include "browsergateway.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <QVector>
#include <unistd.h>

namespace {
using Completion = std::function<void(QJsonObject)>;
QJsonObject failure(const QString& code) { return {{"status", false}, {"code", code}}; }
QJsonObject success() { return {{"status", true}}; }
void report(const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    (void)!::write(STDOUT_FILENO, bytes.constData(), size_t(bytes.size()));
}
QByteArray contents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 65536 ? file.readAll() : QByteArray();
}
class FixtureBridge : public QObject {
public:
    QUrl endpoint;
    QSslCertificate certificate;
    QByteArray basic;
    QNetworkAccessManager manager;
    QString owner;
    bool starting = false, stopping = false, cancelled = false;
    bool forceInputDisabled = true;
    QVector<Completion> stopWaiters;

    FixtureBridge() { manager.setProxy(QNetworkProxy::NoProxy); }
    void post(QJsonObject body, Completion done) {
        const auto action = body.value("action").toString();
        QNetworkRequest request(endpoint);
        auto trust = QSslConfiguration::defaultConfiguration();
        trust.setCaCertificates({certificate});
        trust.setPeerVerifyMode(QSslSocket::VerifyPeer);
        request.setSslConfiguration(trust);
        request.setTransferTimeout(15000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", basic);
        auto reply = manager.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, done, action] {
            const auto bytes = reply->readAll();
            const auto document = QJsonDocument::fromJson(bytes);
            const bool okay = reply->error() == QNetworkReply::NoError &&
                reply->sslConfiguration().peerCertificate() == certificate && bytes.size() <= 262144 && document.isObject();
            const auto result = okay ? document.object() : failure("fixture-upstream-unavailable");
            auto code = result.value("code").toString();
            if (!QRegularExpression("^[a-z0-9-]{0,64}$").match(code).hasMatch()) code = "invalid-code";
            // Fixture diagnostics deliberately omit credentials, SDP, IDs,
            // request bodies and response bodies. Preserve application codes
            // such as unsupported-codec instead of hiding them behind HTTP 503.
            const QJsonObject diagnostic{{"action", action},
                {"httpStatus", reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
                {"errorString", reply->error() == QNetworkReply::NoError ? QString() : reply->errorString().left(256)},
                {"code", code}};
            const auto line = QJsonDocument(diagnostic).toJson(QJsonDocument::Compact) + '\n';
            (void)!::write(STDERR_FILENO, line.constData(), size_t(line.size()));
            reply->deleteLater(); done(result);
        });
    }
    void end(Completion done) {
        if (done) stopWaiters.append(std::move(done));
        if (starting) { cancelled = true; return; }
        if (stopping) return;
        const auto finish = [this](QJsonObject result) {
            stopping = false;
            if (result.value("status").toBool()) { owner.clear(); cancelled = false; }
            const auto waiters = std::move(stopWaiters); stopWaiters.clear();
            for (const auto& waiter : waiters) waiter(result);
        };
        if (owner.isEmpty()) { finish(success()); return; }
        stopping = true;
        const auto id = owner;
        post({{"action", "stop"}, {"id", id}, {"keepReservation", true}}, [this, id, finish](QJsonObject result) {
            if (!result.value("status").toBool()) { finish(result); return; }
            post({{"action", "release"}, {"id", id}}, finish);
        });
    }
    void request(const QJsonObject& body, QObject* context, Completion callback) {
        const QPointer<QObject> guard(context);
        const auto done = [guard, callback](QJsonObject result) { if (guard) callback(result); };
        const auto action = body.value("action").toString(), id = body.value("id").toString();
        if (action == "start") {
            if (!owner.isEmpty()) { done(failure("busy")); return; }
            owner = id; starting = true; cancelled = false;
            QJsonObject mediaRequest = body;
            // Real capture/encode validation must not allocate the host's global
            // input devices just because the production client enables input.
            if (forceInputDisabled) mediaRequest["input"] = false;
            post({{"action", "reserve"}, {"id", id}}, [this, mediaRequest, done](QJsonObject result) {
                if (!result.value("status").toBool()) {
                    starting = false; owner.clear(); end({}); done(result); return;
                }
                if (cancelled) { starting = false; end([done](QJsonObject) { done(failure("cancelled")); }); return; }
                post(mediaRequest, [this, done](QJsonObject media) {
                    starting = false;
                    if (!media.value("status").toBool() || cancelled) {
                        if (cancelled) media = failure("cancelled");
                        end([done, media](QJsonObject) { done(media); });
                    } else done(media);
                });
            });
            return;
        }
        if (action == "stop" && owner.isEmpty()) { done(success()); return; }
        if (id != owner || id.isEmpty()) { done(failure("not-owner")); return; }
        if (action == "stop") { end(done); return; }
        if (starting || stopping) { done(failure("busy")); return; }
        post(body, done);
    }
};
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("DeskPort Browser Live Fixture");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"fixture", "Private JSON with loopback URL, certificate path, username and password.", "file"});
    parser.addOption({"state", "Isolated gateway state directory.", "directory"});
    parser.addOption({"certificate", "Synthetic HTTPS certificate for explicit test trust.", "file"});
    parser.addOption({"key", "Matching private synthetic HTTPS key.", "file"});
    parser.addOption({"bind", "Explicit loopback or LAN address for this fixture.", "address", "127.0.0.1"});
    parser.addOption({"port", "HTTPS fixture port (0 selects an unused port).", "port", "0"});
    parser.process(application);
    if (!parser.isSet("fixture") || !parser.isSet("state")) parser.showHelp(2);
    const auto fixture = QJsonDocument::fromJson(contents(parser.value("fixture"))).object();
    FixtureBridge bridge;
    bridge.forceInputDisabled = fixture.value("forceInputDisabled").toBool(true);
    bridge.endpoint = QUrl(fixture.value("url").toString());
    const auto target = QHostAddress(bridge.endpoint.host());
    if (bridge.endpoint.scheme() != "https" || !target.isLoopback() ||
        bridge.endpoint.path() != "/api/deskport/browser" || !bridge.endpoint.userInfo().isEmpty() ||
        !bridge.endpoint.query().isEmpty() || !bridge.endpoint.fragment().isEmpty()) {
        report({{"error", "Fixture upstream must be the explicit loopback HTTPS browser endpoint."}}); return 2;
    }
    const auto certPath = fixture.value("certificate").toString();
    const auto certificates = QSslCertificate::fromData(contents(certPath));
    const auto password = fixture.value("password").toString();
    if (!QFileInfo(certPath).isAbsolute() || certificates.isEmpty() || password.isEmpty()) {
        report({{"error", "Fixture requires an absolute certificate path and a nonempty password."}}); return 2;
    }
    bridge.certificate = certificates.first();
    const auto username = fixture.value("username").toString("deskport");
    bridge.basic = "Basic " + (username + ':' + password).toUtf8().toBase64();
    BrowserGateway::Options options;
    options.stateDirectory = parser.value("state");
    options.certificatePath = parser.value("certificate");
    options.privateKeyPath = parser.value("key");
    options.listenAddresses = {QHostAddress(parser.value("bind"))};
    bool portOkay;
    const int port = parser.value("port").toInt(&portOkay);
    if (!portOkay || port < 0 || port > 65535 || !QFileInfo(options.stateDirectory).isAbsolute()) {
        report({{"error", "Fixture requires an absolute state directory and a valid port."}}); return 2;
    }
    options.port = quint16(port);
    // Each code works once; drivers ask for "next-code" to step this clock.
    qint64 clockOffset = 0;
    options.clock = [&clockOffset] { return QDateTime::currentSecsSinceEpoch() + clockOffset; };
    BrowserGateway::Hooks hooks;
    hooks.state = [&bridge] { return QJsonObject{{"sharing", true}, {"hostName", "DeskPort isolated Xvfb fixture"},
        {"mediaAvailable", true}, {"busy", !bridge.owner.isEmpty()}}; };
    hooks.request = [&bridge](const QJsonObject& body, QObject* context, Completion done) { bridge.request(body, context, std::move(done)); };
    BrowserGateway gateway(hooks, options);
    if (!gateway.start()) { report({{"error", gateway.errorString()}}); return 2; }
    const auto info = [&gateway] { report({{"port", gateway.port()}, {"code", gateway.accessCode()},
        {"authenticator", gateway.authenticatorUri("fixture")}, {"urls", QJsonArray::fromStringList(gateway.urls())}}); };
    info();
    QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray input;
    bool quitting = false;
    QObject::connect(&notifier, &QSocketNotifier::activated, &application, [&] {
        char bytes[4096]; const auto size = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (size > 0) input.append(bytes, int(size));
        else { notifier.setEnabled(false); return; }
        int end;
        while ((end = input.indexOf('\n')) >= 0) {
            const auto command = input.left(end); input.remove(0, end + 1);
            if (command == "info") info();
            else if (command.startsWith('{')) {
                const auto request = QJsonDocument::fromJson(command).object();
                const auto action = request.value("action").toString();
                QJsonObject result{{"requestId", request.value("requestId")}, {"ok", true}};
                if (action == "list") result["pairedBrowsers"] = gateway.pairedBrowsers();
                else if (action == "next-code") { clockOffset += 30; result["code"] = gateway.accessCode(); }
                else if (action == "revoke") {
                    result["ok"] = gateway.revokeBrowser(request.value("id").toString());
                    if (!result.value("ok").toBool()) result["error"] = gateway.errorString();
                } else if (action == "status") {
                    result["active"] = gateway.active(); result["port"] = gateway.port();
                    result["hasOwner"] = !bridge.owner.isEmpty();
                    result["starting"] = bridge.starting; result["stopping"] = bridge.stopping;
                    result["pairedBrowsers"] = gateway.pairedBrowsers();
                } else result["ok"] = false;
                report(result);
            }
            else if (command == "quit" && !quitting) {
                quitting = true; gateway.stop();
                bridge.end([&application](QJsonObject) { application.quit(); });
                QTimer::singleShot(20000, &application, &QCoreApplication::quit);
            }
        }
    });
    return application.exec();
}
