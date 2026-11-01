#include "browserhost.h"
#include "hostmanager.h"
#include "workspaceresolution.h"
#include "../../third_party/qrcodegen/qrcodegen.hpp"
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace {
QJsonObject failure(const QString& code) {
    return {{"version", 1}, {"status", false}, {"code", code}};
}
QJsonObject success() { return {{"version", 1}, {"status", true}}; }
// The browser reports its CSS viewport, devicePixelRatio and content zoom; the
// desktop size, backing scale and host floor come from the shared core policy.
DeskPortDisplay::Workspace browserWorkspace(const QJsonObject& body) {
    const auto view = body.value("viewport").toObject();
    if (view.isEmpty()) {
        // Older pages send a transport size only.
        const int width = body.value("width").toInt(), height = body.value("height").toInt();
        if (width < 640 || width > 1280 || height < 360 || height > 720 || width % 4 || height % 4) return {};
        return {QSize(width, height), 1};
    }
    const int width = view.value("width").toInt(), height = view.value("height").toInt();
    const double ratio = view.value("ratio").toDouble(1.0);
    if (width < 160 || height < 120 || width > 8192 || height > 8192 || !(ratio >= 0.5 && ratio <= 8.0)) return {};
    const double zoom = body.value("zoom").toDouble(1.0);
    const auto base = DeskPortDisplay::forClient(QSize(qRound(width * ratio), qRound(height * ratio)), ratio);
    if (base.pixels.isEmpty()) return {};
    return DeskPortDisplay::adjusted(base, zoom);
}
BrowserGateway::Options options(const QString& directory) {
    BrowserGateway::Options result;
    result.stateDirectory = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/browser" : directory;
    QSettings settings;
    const int port = qEnvironmentVariableIsSet("DESKPORT_WEB_PORT")
        ? qEnvironmentVariableIntValue("DESKPORT_WEB_PORT") : settings.value("web/port", 48992).toInt();
    result.port = port >= 1024 && port <= 65535 ? quint16(port) : quint16(48992);
    const QString bind = qEnvironmentVariable("DESKPORT_WEB_BIND", settings.value("web/bindAddress").toString());
    if (!bind.isEmpty()) {
        const QHostAddress address(bind);
        // An invalid explicit bind must never silently widen the listener.
        result.listenAddresses = {address.isNull() ? QHostAddress(QHostAddress::LocalHost) : address};
    }
    result.certificatePath = settings.value("web/certificate").toString();
    result.privateKeyPath = settings.value("web/privateKey").toString();
    result.allowedHosts = settings.value("web/allowedHosts").toStringList();
    result.tailnetIdentity = settings.value("web/tailnetIdentity", true).toBool();
    result.tailscaleProgram = settings.value("web/tailscale").toString();
    return result;
}
}

BrowserHost::BrowserHost(HostManager* host, QObject* parent, const QString& directory)
    : QObject(parent), m_Host(host),
      m_Gateway(BrowserGateway::Hooks{
          [this] { return state(); },
          [this](const QJsonObject& body, QObject* context, Completion done) {
              request(body, context, std::move(done));
          }}, options(directory), this) {
    connect(&m_Gateway, &BrowserGateway::changed, this, &BrowserHost::changed);
    connect(host, &HostManager::changed, this, [this] {
        if (!m_Host->running() && !m_Id.isEmpty()) {
            ++m_Epoch;
            m_Id.clear(); m_Operation = m_Ending = m_Cancelled = m_Polling = m_DisplayOwned = m_Resizing = false;
            dropResize();
            const auto waiters = std::move(m_EndWaiters);
            m_EndWaiters.clear();
            for (const auto& done : waiters) if (done) done(success());
        }
        m_Gateway.hostStateChanged();
        emit changed();
    });
    m_Poll.setInterval(1500);
    connect(&m_Poll, &QTimer::timeout, this, &BrowserHost::poll);
    host->setBrowserTakeover([this] { return busy(); }, [this](std::function<void()> done) {
        endForTakeover([done](QJsonObject) { done(); });
    });
}
BrowserHost::~BrowserHost() {
    m_Host->setBrowserTakeover({}, {});
    m_Poll.stop(); m_Gateway.stop();
}
void BrowserHost::endForTakeover(Completion done) {
    // The page learns why its session ended on its next request.
    if (!m_Id.isEmpty()) m_TakenOver = m_Id;
    end(std::move(done));
}
bool BrowserHost::start() {
    if (!m_Host->available() || !QSettings().value("web/enabled", true).toBool()) return false;
    m_Listening = m_Gateway.start();
    if (m_Listening) m_Poll.start();
    emit changed();
    return m_Listening;
}
void BrowserHost::stop() {
    m_Listening = false; m_Poll.stop(); m_Gateway.stop(); end(); emit changed();
}
QString BrowserHost::accessCode() const { return m_Gateway.accessCode(); }
qint64 BrowserHost::accessCodeExpiresAt() const { return m_Gateway.accessCodeExpiresAt(); }
qint64 BrowserHost::codeLockedUntil() const { return m_Gateway.codeLockedUntil(); }
QString BrowserHost::authenticatorUri() const { return m_Gateway.authenticatorUri(m_Host->deviceName()); }
QString BrowserHost::authenticatorQr() const { return qrSvg(authenticatorUri()); }
bool BrowserHost::resetAuthenticator() { return m_Gateway.resetAccessCode(); }
QStringList BrowserHost::urls() const { return m_Gateway.urls(); }
QString BrowserHost::errorString() const { return m_Gateway.errorString(); }
bool BrowserHost::listening() const { return m_Listening; }
bool BrowserHost::tailnetListening() const { return m_Gateway.tailnetListening(); }
bool BrowserHost::tailnetIdentity() const { return m_Gateway.tailnetIdentity(); }
QVariantList BrowserHost::pairedBrowsers() const { return m_Gateway.pairedBrowsers().toVariantList(); }
bool BrowserHost::revokeBrowser(const QString& id) { return m_Gateway.revokeBrowser(id); }
QString BrowserHost::qrSource() const {
    if (!m_Listening || urls().isEmpty()) return {};
    return qrSvg(urls().first());
}
QString BrowserHost::qrSvg(const QString& value) {
    if (value.isEmpty()) return {};
    const auto text = value.toUtf8();
    const auto qr = qrcodegen::QrCode::encodeText(text.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int size = qr.getSize();
    QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 " +
        QByteArray::number(size + 8) + ' ' + QByteArray::number(size + 8) +
        "' shape-rendering='crispEdges'><path fill='white' d='M0 0h" + QByteArray::number(size + 8) +
        "v" + QByteArray::number(size + 8) + "H0z'/><path fill='black' d='";
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
        if (qr.getModule(x, y)) svg += 'M' + QByteArray::number(x + 4) + ' ' + QByteArray::number(y + 4) + "h1v1h-1z";
    svg += "'/></svg>";
    return "data:image/svg+xml;base64," + QString::fromLatin1(svg.toBase64());
}
QJsonObject BrowserHost::localInfo() const {
    return {{"enabled", m_Listening}, {"urls", QJsonArray::fromStringList(urls())},
            {"accessCode", accessCode()}, {"accessCodeExpiresAt", accessCodeExpiresAt()},
            {"codeLockedUntil", codeLockedUntil()}, {"port", int(m_Gateway.port())},
            {"busy", busy()}, {"error", errorString()}};
}
QJsonObject BrowserHost::state() const {
    QJsonArray zoom;
    for (const double value : dp_catalog_tuning_values) zoom.append(value);
    return {{"sharing", m_Host->canPair()}, {"hostName", m_Host->deviceName()},
            {"mediaAvailable", m_Host->available()}, {"busy", busy()},
            {"capabilities", QJsonObject{{"resize", true},
                {"adaptiveDisplay", m_Host->adaptiveDisplayAvailable()},
                {"extendDisplay", m_Host->displayPoliciesAvailable()}, {"zoomChoices", zoom}}}};
}

void BrowserHost::request(const QJsonObject& body, QObject* context, Completion completion) {
    const QPointer<QObject> alive(context);
    const auto done = [alive, completion](QJsonObject result) { if (alive) completion(result); };
    const QString action = body.value("action").toString();
    const QString id = body.value("id").toString();
    if (action == "start") { begin(body, done); return; }
    if (!id.isEmpty() && id == m_TakenOver && id != m_Id) { done(failure("taken-over")); return; }
    if (action == "stop") {
        if (m_Id.isEmpty()) { done(success()); return; }
        if (id != m_Id) { done(failure("not-owner")); return; }
        end(done); return;
    }
    if (id.isEmpty() || id != m_Id || m_Ending) { done(failure("not-owner")); return; }
    if (action == "resize") { resize(body, done); return; }
    if (action != "answer" && action != "status" && action != "input" && action != "heartbeat") {
        done(failure("unsupported-action")); return;
    }
    m_Host->browserControl(body, this, done);
}

void BrowserHost::begin(const QJsonObject& input, Completion done) {
    if (busy()) { done(failure("busy")); return; }
    if (!m_Host->canPair() || m_Host->changing()) { done(failure("sharing-off")); return; }
    const QString id = input.value("id").toString();
    const auto workspace = browserWorkspace(input);
    const int policy = input.value("displayPolicy").toInt(0);
    if (id.size() < 16 || id.size() > 64 || workspace.pixels.isEmpty() || (policy != 0 && policy != 2)) {
        done(failure("invalid-session")); return;
    }
    if (policy && !m_Host->displayPoliciesAvailable()) { done(failure("policy-unavailable")); return; }
    m_Id = id; m_TakenOver.clear(); m_Operation = true; m_Cancelled = false; m_DisplayOwned = false;
    m_Policy = policy; m_Resize = {}; m_Resizing = false;
    const auto epoch = ++m_Epoch;
    QJsonObject body = input;
    // Media encodes this desktop size down to its own transport limit.
    body["width"] = workspace.pixels.width(); body["height"] = workspace.pixels.height();
    body.remove("viewport"); body.remove("zoom"); body.remove("displayPolicy");
    body["fps"] = std::clamp(body.value("fps").toInt(30), 1, 60);
    body["bitrateKbps"] = std::clamp(body.value("bitrateKbps").toInt(8000), 1000, 14000);
    emit changed();
    m_Host->browserControl({{"action", "reserve"}, {"id", id}}, this,
        [this, body, epoch, done, workspace](QJsonObject reserved) {
        if (epoch != m_Epoch) { done(failure("cancelled")); return; }
        m_Operation = false;
        if (!reserved.value("status").toBool()) {
            if (reserved.value("code").toString() == "unavailable") {
                // The request may have reached the host before its HTTP reply
                // was lost. Clean up our ID instead of forgetting a live lease.
                end([done, reserved](QJsonObject) { done(reserved); });
                return;
            }
            m_Id.clear(); m_Cancelled = false;
            const auto waiters = std::move(m_EndWaiters); m_EndWaiters.clear();
            for (const auto& waiter : waiters) if (waiter) waiter(success());
            emit changed(); done(reserved); return;
        }
        if (m_Cancelled) { end([done](QJsonObject) { done(failure("cancelled")); }); return; }
        if (!m_Host->adaptiveDisplayAvailable()) { beginMedia(body, epoch, done); return; }
        m_Operation = true; m_DisplayOwned = true;
        const int sequence = ++m_Sequence;
        auto pending = new QObject(this);
        connect(m_Host, &HostManager::displayResized, pending,
            [this, pending, sequence, body, epoch, done](int value, int, int, const QString& error) {
            if (value != sequence) return;
            // deleteLater alone leaves the connection active until the event
            // loop resumes; a duplicate acknowledgement must not start twice.
            QObject::disconnect(m_Host, nullptr, pending, nullptr);
            pending->deleteLater();
            if (epoch != m_Epoch) { done(failure("cancelled")); return; }
            m_Operation = false;
            if (!error.isEmpty() || m_Cancelled) {
                end([done, error](QJsonObject) { done(failure(error.isEmpty() ? "cancelled" : "display-unavailable")); });
            } else beginMedia(body, epoch, done);
        });
        if (!m_Host->resizeDisplay(body.value("width").toInt(), body.value("height").toInt(), workspace.scale, sequence, m_Policy)) {
            delete pending; m_Operation = false;
            end([done](QJsonObject) { done(failure("display-unavailable")); });
        }
    });
}

// Live viewport changes resize the host desktop first, then restart only the
// browser's video encoder. A newer request replaces one still waiting.
void BrowserHost::resize(const QJsonObject& body, Completion done) {
    const auto workspace = browserWorkspace(body);
    if (workspace.pixels.isEmpty()) { done(failure("invalid-session")); return; }
    if (m_Operation) { done(failure("busy")); return; }
    if (m_Resizing) {
        if (m_Resize.done) m_Resize.done(QJsonObject{{"version", 1}, {"status", true}, {"superseded", true}});
        m_Resize = {body, std::move(done)}; return;
    }
    m_Resizing = true;
    const auto epoch = m_Epoch;
    const QString id = m_Id;
    const auto finish = [this, epoch](QJsonObject result, Completion completion) {
        if (epoch != m_Epoch) { completion(failure("cancelled")); return; }
        m_Resizing = false;
        completion(result);
        if (m_Resize.done && !m_Ending && !m_Id.isEmpty()) {
            auto next = std::move(m_Resize); m_Resize = {};
            resize(next.body, std::move(next.done));
        }
    };
    const auto media = [this, id, workspace, epoch, finish, done]() {
        if (epoch != m_Epoch || m_Ending) { finish(failure("cancelled"), done); return; }
        m_Host->browserControl({{"action", "resize"}, {"id", id}, {"width", workspace.pixels.width()},
                                {"height", workspace.pixels.height()}}, this,
            [finish, done, workspace](QJsonObject result) {
            // Report only the applied desktop size, not media internals.
            if (result.value("status").toBool())
                result = QJsonObject{{"version", 1}, {"status", true},
                                     {"width", workspace.pixels.width()}, {"height", workspace.pixels.height()}};
            finish(result, done);
        });
    };
    if (!m_DisplayOwned || !m_Host->adaptiveDisplayAvailable()) { media(); return; }
    const int sequence = ++m_Sequence;
    auto pending = new QObject(this);
    connect(m_Host, &HostManager::displayResized, pending,
        [this, pending, sequence, media, finish, done](int value, int, int, const QString& error) {
        if (value != sequence) return;
        QObject::disconnect(m_Host, nullptr, pending, nullptr);
        pending->deleteLater();
        if (!error.isEmpty()) finish(failure("display-unavailable"), done);
        else media();
    });
    if (!m_Host->resizeDisplay(workspace.pixels.width(), workspace.pixels.height(), workspace.scale, sequence, m_Policy)) {
        delete pending;
        finish(failure("display-unavailable"), done);
    }
}

void BrowserHost::beginMedia(QJsonObject body, quint64 epoch, Completion done) {
    m_Operation = true;
    body["action"] = "start";
    m_Host->browserControl(body, this, [this, epoch, done](QJsonObject result) {
        if (epoch != m_Epoch) { done(failure("cancelled")); return; }
        m_Operation = false;
        if (!result.value("status").toBool() || m_Cancelled) {
            if (m_Cancelled) result = failure("cancelled");
            end([done, result](QJsonObject) { done(result); });
        } else { emit changed(); done(result); }
    });
}

void BrowserHost::end(Completion done) {
    if (done) m_EndWaiters.append(std::move(done));
    if (m_Ending) return;
    if (m_Operation) { m_Cancelled = true; return; }
    const auto finish = [this](QJsonObject result) {
        m_Id.clear(); m_Operation = m_Ending = m_Cancelled = m_Polling = m_DisplayOwned = m_Resizing = false;
            dropResize();
        const auto waiters = std::move(m_EndWaiters); m_EndWaiters.clear();
        emit changed();
        for (const auto& waiter : waiters) if (waiter) waiter(result);
    };
    if (m_Id.isEmpty() || !m_Host->running()) { finish(success()); return; }
    if (!m_Host->canPair()) {
        // A host in startup/shutdown still owns its media process. Wait for
        // definitive exit or restored management access before forgetting it.
        m_Cancelled = true;
        const QString id = m_Id;
        const auto epoch = m_Epoch;
        QTimer::singleShot(1000, this, [this, id, epoch] {
            if (m_Id == id && m_Epoch == epoch && !m_Ending && !m_Operation) end();
        });
        return;
    }
    m_Ending = true;
    const QString id = m_Id;
    const auto epoch = ++m_Epoch;
    const auto retry = [this, id, epoch](QJsonObject result) {
        // An unavailable management reply is not proof that the host released
        // its reservation. Keep ownership until cleanup is acknowledged.
        m_Ending = false; m_Polling = false;
        const auto waiters = std::move(m_EndWaiters); m_EndWaiters.clear();
        emit changed();
        for (const auto& waiter : waiters) if (waiter) waiter(result);
        QTimer::singleShot(1500, this, [this, id, epoch] {
            if (m_Id == id && m_Epoch == epoch && !m_Ending && !m_Operation) end();
        });
    };
    m_Host->browserControl({{"action", "stop"}, {"id", id}, {"keepReservation", true}}, this,
        [this, id, epoch, finish, retry](QJsonObject stopped) {
        if (epoch != m_Epoch) return;
        if (!stopped.value("status").toBool()) {
            const auto code = stopped.value("code").toString();
            if (code == "unauthorized" || code == "not-found") {
                // An authenticated host response says this reservation is gone
                // or belongs to someone else. Never restore their display.
                finish(success()); return;
            }
            // Do not restore a display while media might still be using it.
            retry(stopped);
            return;
        }
        const auto release = [this, id, epoch, finish, retry](bool displayOkay) {
            if (epoch != m_Epoch) return;
            if (displayOkay) m_DisplayOwned = false;
            m_Host->browserControl({{"action", "release"}, {"id", id}}, this,
                [epoch, this, finish, retry, displayOkay](QJsonObject result) {
                if (epoch != m_Epoch) return;
                if (!result.value("status").toBool()) {
                    const auto code = result.value("code").toString();
                    if (code == "unauthorized" || code == "not-found") { finish(success()); return; }
                    retry(result); return;
                }
                if (!displayOkay) result = failure("display-restore-failed");
                finish(result);
            });
        };
        if (m_DisplayOwned && m_Host->adaptiveDisplayAvailable()) m_Host->settleSessionDisplay(this, release);
        else release(true);
    });
}

void BrowserHost::poll() {
    if (m_Id.isEmpty() || m_Operation || m_Ending || m_Polling || !m_Host->canPair()) return;
    m_Polling = true;
    const auto epoch = m_Epoch;
    m_Host->browserControl({{"action", "status"}, {"id", m_Id}}, this, [this, epoch](QJsonObject result) {
        if (epoch != m_Epoch) return;
        m_Polling = false;
        const auto value = result.value("state").toString();
        if (!result.value("status").toBool() || value == "stopped" || value == "closed" || value == "failed") end();
    });
}
