#include <QGuiApplication>
#include "multisessions.h"
#include "windowframe.h"
#include <QScopedValueRollback>
#include "peermanager.h"
#include "sessiongraph.h"
#include "gui/hostlayout.h"
#include "gui/sessionpresentation.h"
#include "streaming/video/encodedframe.h"
#include <QtEndian>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QByteArray frame(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n'; }
}
MultiSessions::MultiSessions(PeerManager* peers, QByteArray certificate, QByteArray key, QObject* parent)
    : QObject(parent), m_Certificate(certificate), m_Key(key), m_Peers(peers) {
    m_Identity = SessionGraph::identity(QSslCertificate(m_Certificate));
    m_Server.setSocketOptions(QLocalServer::UserAccessOption);
    m_Server.listen(QStringLiteral("deskport-sessions-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(&m_Server, &QLocalServer::newConnection, this, &MultiSessions::accept);
    m_VideoServer.setSocketOptions(QLocalServer::UserAccessOption);
    m_VideoServer.listen(QStringLiteral("deskport-video-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    connect(&m_VideoServer, &QLocalServer::newConnection, this, &MultiSessions::acceptVideo);
}
MultiSessions::~MultiSessions() {
    // Normal application exit waits asynchronously for shutdown(). Children also
    // exit on IPC loss, including a shell crash; do not retain detached viewers.
    for (auto entry : m_Entries) {
        if (entry->process) entry->process->disconnect(this);
        if (entry->socket) { entry->socket->disconnect(this); entry->socket->abort(); }
        if (entry->videoSocket) { entry->videoSocket->disconnect(this); entry->videoSocket->abort(); }
        if (entry->reserved) SessionGraph::release(m_Identity, entry->token);
        delete entry->presentation;
        delete entry;
    }
}
void MultiSessions::setCanvasWindow(QWindow* window) {
    Q_ASSERT(!busy()); m_CanvasWindow = window;
    trackCanvasWindowFrame(window);
    if (window) connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible && !m_RefreshingCanvasFrame) showDevices();
    });
    emit changed(); emit canvasChanged();
}
void MultiSessions::trackCanvasWindowFrame(QWindow* window) {
    WindowFrame::track(window);
}
void MultiSessions::setCanvasWindowFrameless(bool frameless) {
    if (!m_CanvasWindow || m_RefreshingCanvasFrame ||
        m_CanvasWindow->flags().testFlag(Qt::FramelessWindowHint) == frameless) return;
    if (!WindowFrame::requiresSurfaceRefresh(m_CanvasWindow)) {
        // This path keeps the canvas mapped and focused. Sending a release here
        // could leave the worker unfocused because an unchanged canvas does not
        // publish another viewport state.
        WindowFrame::setFrameless(m_CanvasWindow, frameless);
        return;
    }
    // QML calls this only for an explicit native-Wayland setting change. Send
    // the normal loss-of-focus boundary before briefly replacing its surface;
    // CanvasBridge retains that release even if focus returns in the same turn.
    auto released = m_CanvasState;
    released["visible"] = false; released["focused"] = false;
    released["grabbed"] = false; released["buttons"] = 0;
    updateCanvas(released);
    QScopedValueRollback<bool> refreshing(m_RefreshingCanvasFrame, true);
    WindowFrame::setFrameless(m_CanvasWindow, frameless);
    // Do not pump events or defer re-showing: a later user hide must remain final.
    // SessionCanvas publishes the actual new visibility/focus on its next update.
}
QObject* MultiSessions::presentation() const {
    // Devices hides the selected canvas; it does not end its presentation.
    // Keep its decoder and last frame until another session is selected or ends.
    const auto entry = m_Entries.value(m_Selected);
    return entry && (entry->state == "starting" || entry->state == "connected") ? entry->presentation : nullptr;
}
QVariantMap MultiSessions::canvas() const {
    const auto entry = m_Entries.value(m_Wanted);
    return entry ? entry->canvas : QVariantMap{};
}
void MultiSessions::updateCanvas(QJsonObject state) {
    m_CanvasState = state;
    if (auto entry = m_Entries.value(m_Wanted)) {
        const bool releaseInput = !state["visible"].toBool() || !state["focused"].toBool();
        if (releaseInput && entry->socket) {
            // Native window moves can enter a platform tracking loop before Qt
            // services its write notifier. Deliver the release without waiting.
            entry->socket->flush();
            if (entry->socket->bytesToWrite() >= 16384) {
                // Never silently discard an input-release boundary under IPC
                // backpressure. A wedged worker loses its session instead.
                entry->error = tr("The desktop worker did not respond.");
                entry->socket->abort();
                if (entry->process) entry->process->kill();
                return;
            }
        }
        send(entry, {{"command","canvas-state"},{"state",state}});
        if (releaseInput && entry->socket) entry->socket->flush();
    }
}
void MultiSessions::canvasInput(QJsonObject event) {
    const auto entry = m_Entries.value(m_Wanted);
    if (m_RefreshingCanvasFrame || !m_CanvasWindow || !m_CanvasWindow->isVisible() || (!m_CanvasWindow->isActive() && !(QGuiApplication::focusWindow() && m_CanvasWindow->isAncestorOf(QGuiApplication::focusWindow()))) || !entry ||
        !entry->exposed || !m_Hiding.isEmpty() || entry->state != "connected") return;
    send(entry, {{"command","canvas-input"},{"epoch",double(m_Epoch)},{"event",event}});
}
void MultiSessions::answerCanvasQuestion(int choice) {
    const auto entry = m_Entries.value(m_Wanted);
    if (!entry || (choice != 0 && choice != 1)) return;
    const auto question = entry->canvas.value("question").toJsonObject();
    if (question.isEmpty()) return;
    send(entry, {{"command","canvas-answer"}, {"id",question.value("id")}, {"choice",choice}});
}

bool MultiSessions::busy() const {
    // Opening owns a session before the deferred process launch. Window actions
    // in that first event turn must already target the connecting desktop.
    for (auto entry : m_Entries) if (entry->state == "starting" ||
        (entry->process && entry->process->state() != QProcess::NotRunning)) return true;
    return false;
}
QVariantMap MultiSessions::selectedTraffic() const {
    const auto entry = m_Entries.value(m_Selected);
    return entry ? entry->traffic : QVariantMap{};
}
QStringList MultiSessions::tuningLimited() const {
    QStringList ids;
    for (auto entry : m_Entries) if (entry->tuningLimited && entry->state == "connected") ids << entry->id;
    return ids;
}
void MultiSessions::setDiagnosticsEnabled(bool enabled) {
    for (auto entry : m_Entries) send(entry, {{"command","diagnostics"},{"enabled",enabled}});
}
void MultiSessions::setShortcutEnabled(bool enabled, const QString& sequence) {
    m_ShortcutEnabled = enabled;
    m_ShortcutSequence = sequence;
    for (auto entry : m_Entries) send(entry, {{"command","shortcut"},{"enabled",enabled},{"sequence",sequence}});
}
QStringList MultiSessions::ordered() const {
    // Devices the saved layout does not mention follow in connection order.
    return HostLayout::load().flattened(m_Order);
}
QVariantList MultiSessions::sessions() const {
    QVariantList rows;
    for (const auto& id : ordered()) if (auto entry = m_Entries.value(id)) rows.append(QVariantMap{{"id",entry->id},{"name",entry->name},{"state",entry->state},
        {"error",entry->error},{"selected",entry->id == m_Selected},{"visible",entry->id == m_Visible}});
    return rows;
}
QVariantMap MultiSessions::states() const {
    QVariantMap result;
    for (auto entry : m_Entries) result.insert(entry->id, entry->state);
    return result;
}
void MultiSessions::open(QString id, QString name, QString address, QString app) {
    if (m_Shutdown || m_Sleeping || m_Locked || id.isEmpty()) return;
    id = id.toLower();
    auto entry = m_Entries.value(id);
    if (entry && (entry->state == "starting" || (entry->process && entry->process->state() != QProcess::NotRunning))) { select(id); return; }
    if (!entry) { entry = new Entry; entry->id = id; m_Entries.insert(id, entry); }
    m_Order.removeAll(id); m_Order.append(id);
    entry->name = name; entry->address = address; entry->app = app;
    entry->error.clear(); entry->stopError.clear(); entry->state = "starting"; entry->fullscreenPending = false; entry->exposed = false;
    entry->token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (entry->videoSocket) { entry->videoSocket->abort(); entry->videoSocket = nullptr; }
    delete entry->presentation;
    entry->presentation = new SessionPresentation(this);
    connect(entry->presentation, &SessionPresentation::idrRequested, this, [this, entry] { send(entry, {{"command","video-idr"}}); });
    connect(entry->presentation, &SessionPresentation::failed, this, [this, entry](QString error) {
        stopSession(entry, error);
    });
    select(id);
    emit devicesRequested(); emit changed();
    QTimer::singleShot(0, this, [this, id] { launch(id); });
}
void MultiSessions::launch(const QString& id) {
    auto entry = m_Entries.value(id);
    if (!entry || m_Shutdown || entry->state != "starting" || (entry->process && entry->process->state() != QProcess::NotRunning)) return;
    const auto peer = m_Peers->sessionPeer(id);
    const QSslCertificate cert(peer.value("clientCert").toString().toUtf8());
    const int port = peer.value("bindingPort").toInt();
    if (!m_Server.isListening() || (canvasEnabled() && !m_VideoServer.isListening()) || !peer.value("ready").toBool() || !peer.value("granted").toBool() || cert.isNull() || port < 1 || port > 65535 || entry->address.isEmpty()) {
        ended(entry, tr("The device binding is unavailable. Refresh the device and try again.")); return;
    }
    entry->reserved = SessionGraph::reserve(m_Identity, {entry->token,entry->address,quint16(port),cert,m_Certificate,m_Key});
    if (!entry->reserved) { ended(entry, tr("This connection would create a loop.")); return; }
    auto process = new QProcess(this); entry->process = process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("DESKPORT_SESSION_ENDPOINT", m_Server.fullServerName());
    environment.insert("DESKPORT_SESSION_TOKEN", entry->token);
    if (canvasEnabled()) environment.insert("DESKPORT_VIDEO_ENDPOINT", m_VideoServer.fullServerName());
    process->setProcessEnvironment(environment);
    process->setProgram(QCoreApplication::applicationFilePath());
    process->setArguments({"--session-worker", entry->id, entry->app});
    // Drain output without persisting potentially identifying native-library text.
    connect(process, &QProcess::readyReadStandardOutput, process, [process] { process->readAllStandardOutput(); });
    connect(process, &QProcess::readyReadStandardError, process, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this,entry,process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && entry->process == process) {
            entry->process=nullptr; process->deleteLater(); ended(entry, tr("The desktop worker could not start."));
        }
    });
    connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this,entry,process](int code,QProcess::ExitStatus status) {
        ended(entry, !entry->stopError.isEmpty() ? entry->stopError : entry->state == "stopping" ? QString() : !entry->error.isEmpty() ? entry->error :
            (status == QProcess::NormalExit && code == 0 ? QString() : tr("The desktop worker stopped unexpectedly.")));
        entry->process = nullptr; process->deleteLater(); emit changed();
    });
    process->start();
    QTimer::singleShot(15000, process, [this,entry,process] {
        if (entry->process == process && !entry->socket && process->state() != QProcess::NotRunning) {
            entry->error = tr("The desktop worker did not respond."); process->kill();
        }
    });
}
void MultiSessions::accept() {
    while (auto socket = m_Server.nextPendingConnection()) {
        socket->setReadBufferSize(16385);
        auto bytes = std::make_shared<QByteArray>();
        auto owner = std::make_shared<Entry*>(nullptr);
        connect(socket, &QLocalSocket::readyRead, this, [this,socket,bytes,owner] {
            *bytes += socket->readAll();
            if (bytes->size() > 16384) { socket->abort(); return; }
            while (bytes->contains('\n')) {
                const auto end = bytes->indexOf('\n');
                const auto message = QJsonDocument::fromJson(bytes->left(end)).object(); bytes->remove(0,end+1);
                if (!*owner) {
                    for (auto entry : m_Entries) if (entry->token == message.value("token").toString() && !entry->socket && entry->process && entry->process->state() != QProcess::NotRunning) {
                        *owner = entry; entry->socket = socket; break;
                    }
                    if (!*owner) { socket->abort(); return; }
                    const auto source=m_Peers->sessionPeer((*owner)->id);
                    QJsonObject peer;
                    for (const auto& key : {"hostId","name","hostPort","hostCert","os"}) peer[key]=source.value(key);
                    peer["address"]=(*owner)->address;
                    QJsonObject initialize {{"command","initialize"},{"peer",peer},{"shortcutEnabled",m_ShortcutEnabled},
                                            {"shortcutSequence",m_ShortcutSequence}};
                    if (canvasEnabled()) initialize["canvas"] = m_CanvasState;
                    send(*owner, initialize);
                    if ((*owner)->state == "stopping") send(*owner,{{"command","disconnect"}});
                    send(*owner, {{"command","lifecycle"},{"sleeping",m_Sleeping},{"locked",m_Locked}});
                    present();
                } else if ((*owner)->socket == socket) receive(*owner, message);
                else { socket->abort(); return; }
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, [socket,owner] { if (!*owner) socket->abort(); });
    }
}
void MultiSessions::send(Entry* entry, const QJsonObject& message) {
    if (entry && entry->socket && entry->socket->bytesToWrite() < 16384) entry->socket->write(frame(message));
}
void MultiSessions::acceptVideo() {
    constexpr int MaxPacket = EncodedVideoFrame::MaxBytes + EncodedVideoFrame::MaxSegments * 5 + 64;
    while (auto socket = m_VideoServer.nextPendingConnection()) {
        socket->setReadBufferSize(MaxPacket + 4);
        struct State { QByteArray bytes; Entry* owner = nullptr; bool configured = false; };
        auto state = std::make_shared<State>();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, state] {
            state->bytes += socket->readAll();
            if (!state->owner) {
                const auto end = state->bytes.indexOf('\n');
                if (end < 0) { if (state->bytes.size() > 256) socket->abort(); return; }
                if (end > 256) { socket->abort(); return; }
                const auto token = QJsonDocument::fromJson(state->bytes.left(end)).object().value("token").toString();
                state->bytes.remove(0, end + 1);
                for (auto entry : m_Entries) if (entry->token == token && entry->presentation &&
                    entry->process && entry->process->state() != QProcess::NotRunning) {
                    state->owner = entry;
                    if (entry->videoSocket) entry->videoSocket->abort();
                    entry->videoSocket = socket;
                    break;
                }
                if (!state->owner) { socket->abort(); return; }
            }
            if (state->owner->videoSocket != socket) { socket->abort(); return; }
            while (state->bytes.size() >= 4) {
                const quint32 length = qFromBigEndian<quint32>(state->bytes.constData());
                if (!length || length > MaxPacket || (!state->configured && length > 16384)) { socket->abort(); return; }
                if (quint64(state->bytes.size()) < length + 4ULL) return;
                const auto packet = state->bytes.mid(4, length);
                state->bytes.remove(0, length + 4);
                if (!state->configured) {
                    const auto config = QJsonDocument::fromJson(packet).object();
                    const bool ok = state->owner->presentation->configure(config);
                    socket->write(frame(ok ? state->owner->presentation->capabilities() : QJsonObject{{"ok",false}}));
                    socket->flush();
                    if (!ok) { socket->disconnectFromServer(); return; }
                    state->configured = true;
                } else state->owner->presentation->frame(packet);
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(5000, socket, [socket, state] { if (!state->configured) socket->abort(); });
    }
}
void MultiSessions::receive(Entry* entry, const QJsonObject& message) {
    const auto type = message.value("type").toString();
    // A worker may report a late connection error while shutdown is already in
    // progress. It must not replace the original presentation failure or turn
    // an explicit user disconnect into an error.
    if (entry->state == "stopping" && (type == "connecting" || type == "connected" || type == "error")) return;
    if (type == "canvas") {
        const auto value = message.value("value").toObject();
        if (entry->presentation) entry->presentation->overlays(value.value("overlays").toObject());
        entry->canvas = {{"textInput",value.value("textInput").toBool()},
                         {"question",value.value("question").toObject()},
                         {"label",value.value("label").toString().left(1024)},
                         {"waiting",value.value("waiting").toBool()},
                         {"relative",value.value("relative").toBool()},
                         {"pointerRegion",value.value("pointerRegion").toObject()},
                         {"grab",value.value("grab").toBool()}, {"cursorHidden",value.value("cursorHidden").toBool()}};
        if (m_Wanted == entry->id) emit canvasChanged();
        return;
    }
    if (type == "canvas-minimize") {
        if (m_CanvasWindow && m_Wanted == entry->id && entry->exposed && m_Hiding.isEmpty()) m_CanvasWindow->showMinimized();
        return;
    }
    if (type == "canvas-fullscreen") {
        if (m_CanvasWindow && m_Wanted == entry->id && entry->exposed && m_Hiding.isEmpty()) {
            if (m_CanvasWindow->windowState() == Qt::WindowFullScreen) {
                if (m_CanvasRestoreState == Qt::WindowMaximized) m_CanvasWindow->showMaximized();
                else m_CanvasWindow->showNormal();
            } else { m_CanvasRestoreState = m_CanvasWindow->windowState(); m_CanvasWindow->showFullScreen(); }
        }
        return;
    }
    if (type == "shortcut") {
        if (m_ShortcutEnabled && !m_Shutdown && entry->exposed &&
            (m_Wanted == entry->id || m_Visible == entry->id)) emit shortcutRequested();
        return;
    }
    if (type == "traffic") {
        if (entry->presentation && message.value("video").isObject()) entry->presentation->status(message.value("video").toObject());
        for (const auto& key : {"received", "sent"}) {
            const auto value=message.value(key).toDouble(-1);
            if (value >= 0 && value < 1e18) entry->traffic[key]=value;
        }
        emit trafficChanged();
        emit sessionTraffic(entry->id, entry->traffic.value("received").toDouble() + entry->traffic.value("sent").toDouble());
        const bool limited = message.value("tuningLimited").toBool();
        if (limited != entry->tuningLimited) { entry->tuningLimited = limited; emit changed(); }
        return;
    }
    if (type == "hidden" && quint64(message.value("epoch").toDouble()) == entry->hideEpoch) {
        // An awaited hide completes a user's switch. An unsolicited one comes
        // from a session restarting underneath its retained window.
        const bool awaited = m_Hiding.remove(entry->id);
        if (awaited) entry->exposed = false;
        if (m_Visible == entry->id) m_Visible.clear();
        present(awaited);
    } else if (type == "devices") {
        if (!entry->quietLifecycle && !m_Sleeping && !m_Locked) { showDevices(); emit devicesRequested(); }
    }
    else if (type == "focus") {
        entry->focused = message.value("focused").toBool();
        if (!entry->focused) entry->focusLost.start();
    }
    else if (type == "next") {
        // Only the viewer that currently owns presentation may switch it.
        if (m_Wanted == entry->id || m_Visible == entry->id) selectNext();
        return;
    }
    else if (type == "ready") { present(!(m_Wanted == entry->id && entry->presented)); }
    else if (type == "connected") { entry->state = "connected"; entry->error.clear(); }
    else if (type == "shown" && m_Wanted == entry->id && m_Hiding.isEmpty()) {
        m_Visible = entry->id; entry->presented = true;
        if (entry->fullscreenPending) { entry->fullscreenPending = false; send(entry, {{"command","fullscreen"}}); }
        emit viewerShown();
    }
    else if (type == "error") { entry->error = message.value("message").toString().left(1024); entry->state = "error"; }
    else if (type == "connecting") { entry->state = "starting"; entry->error.clear(); }
    emit changed();
}
void MultiSessions::select(QString id) {
    id = id.toLower();
    if (!m_Entries.contains(id) || m_Shutdown || m_Sleeping || m_Locked) return;
    if (!m_ActivationTarget.isEmpty() && m_ActivationTarget != id) setActivationToken({});
    const auto selected=m_Entries.value(id);
    if (selected->state != "starting" && selected->state != "connected") { emit devicesRequested(); return; }
    selected->quietLifecycle = false;
    m_Selected = id; m_Wanted = id;
    emit canvasChanged();
    // The shell owns its window. Showing the connecting page must not wait for
    // worker startup or another viewer's input-release acknowledgement. Remote
    // input still waits for present() to complete that handoff.
    if (m_CanvasWindow) {
        if (m_CanvasWindow->windowState() == Qt::WindowMinimized) m_CanvasWindow->showNormal();
        else m_CanvasWindow->setVisible(true);
        m_CanvasWindow->raise(); m_CanvasWindow->requestActivate();
    }
    ++m_Epoch;
    // Only a viewer that may be presenting has to release input first; waiting
    // on hidden background viewers would add their latency to every switch.
    for (auto entry : m_Entries) if (entry->id != id && entry->socket && entry->exposed) {
        hide(entry);
    }
    present(); emit changed();
}
void MultiSessions::selectNext() {
    const auto order = ordered();
    if (m_Shutdown || order.isEmpty()) return;
    const auto current = order.indexOf(m_Wanted.isEmpty() ? m_Selected : m_Wanted);
    for (int step = 1; step <= order.size(); ++step) {
        const auto entry = m_Entries.value(order.at((qMax(current, 0) + step) % order.size()));
        if (entry && entry->state == "connected" && entry->id != m_Wanted) { select(entry->id); return; }
    }
}
bool MultiSessions::viewerInFront(bool trayClick) const {
    // While several desktops connect, the wanted viewer can be on screen before
    // the others finish hiding, so it is never recorded as visible. Its own
    // focus report is the evidence that it is the window in front.
    for (const auto& id : {m_Visible, m_Wanted}) {
        const auto entry = m_Entries.value(id);
        if (entry && entry->exposed &&
            (entry->focused || (trayClick && entry->focusLost.isValid() && entry->focusLost.elapsed() < 600))) return true;
    }
    return false;
}
bool MultiSessions::recall() {
    if (m_Shutdown || m_Sleeping || m_Locked) { setActivationToken({}); return false; }
    const auto recallEntry = [this](const QString& id) {
        // An activation waiting for another viewer to hide belongs only to
        // this target. Cancellation or a later selection cannot transfer it.
        if (m_ShortcutRecall || !m_ActivationToken.isEmpty()) m_ActivationTarget = id;
        select(id);
    };
    const auto last = m_Entries.value(m_Selected);
    if (last && (last->state == "connected" || last->state == "starting")) { recallEntry(last->id); return true; }
    for (const auto& id : ordered()) if (m_Entries.value(id)->state == "connected") { recallEntry(id); return true; }
    setActivationToken({});
    return false;
}
void MultiSessions::hide(Entry* entry) {
    // Repeated selections must not keep postponing an unresponsive worker's
    // deadline. Only process exit or an acknowledgement releases input ownership.
    const bool pending = m_Hiding.contains(entry->id);
    m_Hiding.insert(entry->id); entry->hideEpoch = m_Epoch;
    send(entry, {{"command","hide"},{"epoch",double(m_Epoch)}});
    if (pending || !entry->process || m_Sleeping) return;
    auto process = entry->process;
    const auto request = ++entry->hideRequest;
    QTimer::singleShot(2000, process, [this, entry, process, request] {
        if (entry->process == process && entry->hideRequest == request && m_Hiding.contains(entry->id)) {
            entry->error = tr("The desktop worker did not respond.");
            process->kill();
        }
    });
}
void MultiSessions::present(bool raise) {
    if (m_Shutdown || m_Sleeping || m_Locked || !m_Hiding.isEmpty() || m_Wanted.isEmpty()) return;
    auto entry = m_Entries.value(m_Wanted);
    if (entry && entry->socket && entry->state != "stopping" && entry->state != "error") {
        entry->exposed = true;
        QJsonObject show {{"command","show"},{"raise",raise},{"epoch",double(m_Epoch)}};
        if (m_CanvasWindow) {
            send(entry, {{"command","canvas-state"},{"state",m_CanvasState}});
        }
#ifdef Q_OS_WIN
        // Windows lets only the process that received the tray click take the
        // foreground; without this the worker's viewer opens behind others.
        if (raise && entry->process) AllowSetForegroundWindow(DWORD(entry->process->processId()));
#endif
        // Presenting can wait for another viewer to hide; the token waits too.
        if (raise && !m_ActivationToken.isEmpty()) show["activationToken"] = QString::fromUtf8(m_ActivationToken);
        if (raise && m_ShortcutRecall) show["shortcut"] = true;
        setActivationToken({});
        send(entry, show);
    }
}
void MultiSessions::showDevices() {
    setActivationToken({});
    bool exposed = false;
    for (auto entry : m_Entries) exposed |= entry->socket && entry->exposed;
    // Repeated Devices requests (tray, top bar and worker) are idempotent.
    if (m_Wanted.isEmpty() && !exposed) return;
    m_Wanted.clear(); ++m_Epoch;
    emit canvasChanged();
    for (auto entry : m_Entries) if (entry->socket && entry->exposed) {
        hide(entry);
    }
    emit changed();
}
void MultiSessions::disconnectSession(QString id) {
    auto entry = m_Entries.value(id.toLower()); if (!entry) return;
    stopSession(entry, {});
}
void MultiSessions::stopSession(Entry* entry, const QString& error) {
    const auto id = entry->id;
    // A later disconnect or failure must not replace the reason that started
    // this shutdown, nor re-arm the worker kill timer.
    if (entry->state == "stopping" && entry->process) return;
    entry->stopError = error;
    if (m_ActivationTarget == id) setActivationToken({});
    const bool leavingViewer = m_Wanted == id || m_Visible == id;
    entry->state = "stopping";
    if (leavingViewer) showDevices();
    send(entry, {{"command","disconnect"}});
    if (entry->process) {
        auto process = entry->process;
        QTimer::singleShot(7000, process, [entry,process] { if (entry->process == process && process->state() != QProcess::NotRunning) process->kill(); });
    } else ended(entry, entry->stopError);
    if (leavingViewer) emit devicesRequested();
    emit changed();
}
void MultiSessions::reconnect(QString id) { auto entry=m_Entries.value(id.toLower()); if (entry) send(entry,{{"command","reconnect"}}); }
// Each live session applies data saver itself: live on hosts that accept a
// bitrate ceiling, otherwise by reconnecting with its saved profile.
void MultiSessions::setDataSaverAll(bool on) {
    for (auto entry : m_Entries) if (entry->state == "connected") send(entry, {{"command","datasaver"},{"on",on}});
}
void MultiSessions::setDesktopAdjustment(QString id, double value) {
    auto entry=m_Entries.value(id.toLower()); if (entry) send(entry,{{"command","tuning"},{"value",value}});
}
void MultiSessions::fullscreen(QString id) {
    if (id.isEmpty()) id = m_Selected;
    auto entry = m_Entries.value(id.toLower());
    if (!entry || (entry->state != "starting" && entry->state != "connected")) return;
    if (m_Visible == entry->id && m_Wanted == entry->id && m_Hiding.isEmpty()) {
        send(entry, {{"command","fullscreen"}});
    } else {
        entry->fullscreenPending = true;
        select(entry->id);
    }
}
void MultiSessions::setLifecycleBlocked(bool sleeping, bool locked) {
    if (m_Sleeping == sleeping && m_Locked == locked) return;
    const bool wasSleeping = m_Sleeping;
    m_Sleeping = sleeping; m_Locked = locked;
    // OS suspension must not turn an elapsed hide deadline into a worker kill
    // on wake, before its queued input-release acknowledgement can be delivered.
    if (sleeping && !wasSleeping) for (auto entry : m_Entries) ++entry->hideRequest;
    // Forget presentation, not connection intent. Unlock/wake never raises a viewer.
    if (sleeping || locked) {
        for (auto entry : m_Entries) entry->quietLifecycle = true;
        showDevices();
    }
    if (wasSleeping && !sleeping) for (const auto& id : m_Hiding.values()) {
        m_Hiding.remove(id);
        hide(m_Entries.value(id)); // Fresh deadline after wake, without presentation.
    }
    for (auto entry : m_Entries)
        send(entry, {{"command","lifecycle"},{"sleeping",sleeping},{"locked",locked}});
}
void MultiSessions::shutdown() { m_Shutdown=true; setActivationToken({}); for (auto entry : m_Entries) disconnectSession(entry->id); }
void MultiSessions::ended(Entry* entry, const QString& error) {
    if (m_ActivationTarget == entry->id) setActivationToken({});
    emit sessionClosed(entry->id);
    if (entry->reserved) { SessionGraph::release(m_Identity,entry->token); entry->reserved=false; }
    if (entry->socket) { entry->socket->abort(); entry->socket=nullptr; }
    if (entry->videoSocket) { entry->videoSocket->abort(); entry->videoSocket=nullptr; }
    if (entry->presentation) entry->presentation->detach();
    entry->state=error.isEmpty() ? "disconnected" : "error"; entry->error=error;
    entry->stopError.clear();
    const bool awaited = m_Hiding.remove(entry->id); entry->exposed=false;
    const bool foreground = !entry->quietLifecycle && !m_Shutdown && !m_Sleeping && !m_Locked;
    // A worker can exit before acknowledging hide. Its old visible record must
    // not reopen the shell after the user hid it or selected another desktop.
    const bool showDevices = m_CanvasWindow
        ? m_Wanted == entry->id && m_CanvasWindow->isVisible()
        : m_Visible == entry->id || m_Wanted == entry->id;
    if (m_Visible==entry->id) m_Visible.clear();
    if (m_Wanted==entry->id) m_Wanted.clear();
    if (showDevices && foreground) emit devicesRequested();
    if (m_Selected==entry->id) emit canvasChanged();
    present(awaited); emit changed();
}

