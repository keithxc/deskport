#include "sessionworker.h"
#include "streaming/canvasbridge.h"
#include "streaming/video/videostatus.h"
#include "diagnostics.h"
#include "sleepmonitor.h"
#include "computermanager.h"
#include "cli/startstream.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/transitionwindow.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
namespace {
QByteArray frame(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n'; }
void postEvent(int command) { SDL_Event event{}; event.type = SDL_USEREVENT; event.user.code = command; SDL_PushEvent(&event); }
}
SessionWorker::SessionWorker(QString endpoint, QString token, QString host, QString app)
    : m_Endpoint(endpoint),m_Token(token),m_Host(host),m_App(app) { resize(1120,760); }
void SessionWorker::send(QJsonObject message) { if (m_Socket.state()==QLocalSocket::ConnectedState && m_Socket.bytesToWrite()<16384) m_Socket.write(frame(message)); }
int SessionWorker::run() {
    QTimer traffic;
    connect(&traffic, &QTimer::timeout, this, [this] {
        if (m_Session) {
            auto state=QJsonObject::fromVariantMap(m_Session->traffic()); state["type"]="traffic";
            if (CanvasBridge::enabled() && m_Session->inputDispatching()) state["video"] = VideoStatus::read();
            state["tuningLimited"]=m_Session->desktopTuningLimited(); send(state);
        }
    });
    traffic.start(1000);
    QTimer canvas; QJsonObject lastCanvas;
    connect(&canvas, &QTimer::timeout, this, [this, &lastCanvas] {
        if (!CanvasBridge::enabled()) return;
        const auto value = CanvasBridge::description();
        if (value != lastCanvas) { send({{"type","canvas"},{"value",value}}); lastCanvas = value; }
    });
    canvas.start(20);
    connect(&m_Socket,&QLocalSocket::connected,this,[this] {
        send({{"token",m_Token}});

    });
    connect(&m_Socket,&QLocalSocket::readyRead,this,[this] {
        m_Buffer += m_Socket.readAll();
        if (m_Buffer.size()>16384) { m_Socket.abort(); return; }
        while (m_Buffer.contains('\n')) { const auto end=m_Buffer.indexOf('\n'); auto message=QJsonDocument::fromJson(m_Buffer.left(end)).object(); m_Buffer.remove(0,end+1); command(message); }
    });
    connect(&m_Socket,&QLocalSocket::disconnected,this,[this] {
        m_Stopping=true;
        if (m_Session) { m_Session->cancelRecovery(); postEvent(DeskPortEndSession); }
        else QCoreApplication::quit();
        QTimer::singleShot(5000,qApp,[] { QCoreApplication::exit(1); });
    });
    m_Socket.setReadBufferSize(16385); m_Socket.connectToServer(m_Endpoint);
    QTimer::singleShot(10000,this,[this] { if(m_Socket.state()!=QLocalSocket::ConnectedState) QCoreApplication::exit(1); });
    const auto result=QCoreApplication::exec();
    // A forced IPC-loss exit must not free computers beneath a live SDL worker.
    if (!m_Session) delete m_Computers;
    m_Computers=nullptr;
    return result;
}
void SessionWorker::attach(Session* session) {
    m_Session=session; session->setViewerRequested(m_Visible);
    session->setClientBlocked(m_Sleeping || m_Locked);
    if (m_PendingTuning > 0) { session->applyDesktopAdjustment(m_PendingTuning); m_PendingTuning = 0; }
    if (m_Stopping) session->cancelRecovery();
    connect(session, &Session::canvasFullscreenRequested, this, [this] { send({{"type","canvas-fullscreen"}}); });
    connect(session, &Session::canvasMinimizeRequested, this, [this] { send({{"type","canvas-minimize"}}); });
    send({{"type","connecting"}});
    connect(session,&Session::connectionStarted,this,[this] { send({{"type","connected"}}); });
    connect(session,&Session::viewerReadyChanged,this,[this,session] {
        if (session->viewerReady()) { send({{"type","ready"}}); if(m_Visible) send({{"type","shown"}}); }
    });
    connect(session,&Session::presentationShown,this,[this] { if(m_Visible)send({{"type","shown"}}); });
    connect(session,&Session::presentationHidden,this,[this] { send({{"type","hidden"},{"epoch",double(m_HideEpoch)}}); });
    connect(session,&Session::viewerFocusChanged,this,[this](bool focused) { send({{"type","focus"},{"focused",focused}}); });
    connect(session,&Session::stageFailed,this,[this](QString stage,int code,QString) {
        send({{"type","error"},{"message",tr("Connection failed at %1 (%2).").arg(stage).arg(code)}});
    });
    connect(session,&Session::displayLaunchError,this,[this](QString error) { send({{"type","error"},{"message",error}}); });
    connect(session,&Session::readyForDeletion,this,[this,session] {
        m_Session=nullptr;
        if (!m_Stopping && session->adaptiveRestartPending()) {
            auto next=session->adaptiveContinuation();
            if (!next) { finish(); return; }
            // Keep the pending object cancellable during backoff and sleep.
            m_Session = next;
            QTimer::singleShot(next->retryDelay(),this,[this,next] { attach(next); });
        } else finish();
    });
    QTimer::singleShot(0,this,[this,session] { session->exec(this); });
}
void SessionWorker::command(const QJsonObject& message) {
    const auto action=message.value("command").toString();
    if (action=="initialize" && !m_Computers && !m_Stopping) {
        ShortcutState::configure(message.value("shortcutEnabled").toBool(),
            message.value("shortcutSequence").toString(ShortcutBinding::defaultSequence()));
        if (message.value("canvas").isObject()) CanvasBridge::configure(message.value("canvas").toObject());
        m_Computers = new ComputerManager(StreamingPreferences::get(), true, m_Host);
        const auto peer=message.value("peer").toObject();
        if (!peer.isEmpty() && (peer.value("hostId").toString().compare(m_Host,Qt::CaseInsensitive) != 0 ||
            !m_Computers->addBoundHost(peer.toVariantMap(),false,true))) { finish(tr("The device binding is unavailable.")); return; }
        auto launcher = new CliStartStream::Launcher(m_Host,m_App,nullptr,this);
        connect(launcher,&CliStartStream::Launcher::sessionCreated,this,[this](QString,Session* session) { attach(session); });
        connect(launcher,&CliStartStream::Launcher::failed,this,[this](QString error) { finish(error); });
        connect(launcher,&CliStartStream::Launcher::appQuitRequired,this,[this](QString) { finish(tr("Another application is running on that server.")); });
        launcher->execute(m_Computers);
    } else if (action=="canvas-state" && CanvasBridge::enabled()) {
        CanvasBridge::state(message.value("state").toObject());
    } else if (action=="canvas-input" && CanvasBridge::enabled()) {
        if (m_Visible && quint64(message.value("epoch").toDouble()) == m_InputEpoch)
            CanvasBridge::input(message.value("event").toObject());
    } else if (action=="canvas-answer" && CanvasBridge::enabled() && m_Visible) {
        CanvasBridge::answer(quint64(message.value("id").toDouble()), message.value("choice").toInt(-1));
    } else if (action=="video-idr" && m_Session && m_Session->inputDispatching()) {
        LiRequestIdrFrame();
    } else if (action=="shortcut" && message.value("enabled").isBool()) {
        ShortcutState::configure(message.value("enabled").toBool(),
            message.value("sequence").toString(ShortcutBinding::defaultSequence()));
    } else if (action=="lifecycle" && message.value("sleeping").isBool() && message.value("locked").isBool()) {
        m_Sleeping = message.value("sleeping").toBool(); m_Locked = message.value("locked").toBool();
        if (m_Session) m_Session->setClientBlocked(m_Sleeping || m_Locked);
        if (m_Sleeping || m_Locked) {
            m_Visible = false;
            if (m_Session) {
                m_Session->releaseForClientLock();
                if (m_Sleeping) m_Session->endForSystemSleep();
            }
        }
        send({{"type","lifecycle"},{"blocked",m_Sleeping || m_Locked}});
    } else if (action=="diagnostics" && message.value("enabled").isBool()) {
        Diagnostics::instance().setEnabled(message.value("enabled").toBool());
    } else if (action=="show") {
        // Only a user's request raises the window; recovery restores state quietly.
        if (m_Sleeping || m_Locked || SleepMonitor::asleep() || ScreenLockMonitor::locked()) return;
        const bool raise=message.value("raise").toBool(true);
        m_Visible=true; m_InputEpoch = quint64(message.value("epoch").toDouble());
        CanvasBridge::requested(true);
        CanvasBridge::inputEnabled(m_Session && m_Session->inputDispatching());
        // A quiet recovery can arrive while an explicit recall is queued on
        // the SDL thread. It must not cancel that user's pending activation.
        if (raise) WindowActivation::setPending(message.value("activationToken").toString().toUtf8(),
                                                message.value("shortcut").toBool());
        if(m_Session) { m_Session->setViewerRequested(true); if(m_Session->viewerReady()) postEvent(raise ? DeskPortRecallWindow : DeskPortRestoreViewer); }
    } else if(action=="hide") {
        WindowActivation::setPending({});
        CanvasBridge::requested(false);
        m_Visible=false; m_HideEpoch=quint64(message.value("epoch").toDouble());
        if(m_Session) { m_Session->setViewerRequested(false); postEvent(DeskPortHideWindow); }
        if(!m_Session || !m_Session->inputDispatching()) send({{"type","hidden"},{"epoch",double(m_HideEpoch)}});
    } else if(action=="disconnect") {
        WindowActivation::setPending({});
        m_Stopping=true; if(m_Session) {m_Session->cancelRecovery();postEvent(DeskPortEndSession);} else finish();
    } else if(action=="reconnect" && m_Session) m_Session->requestReconnect();
    else if(action=="tuning" && StreamingPreferences::validDesktopAdjustment(message.value("value").toDouble())) {
        // The shell already saved it; the stream restarts only for a new desktop size.
        const double value=message.value("value").toDouble();
        if(m_Session) m_Session->applyDesktopAdjustment(value); else m_PendingTuning=value;
    }
    else if(action=="fullscreen" && m_Session && m_Visible) postEvent(DeskPortFullscreen);
    else if(action=="datasaver" && m_Session && message.value("on").isBool()) m_Session->setDataSaver(message.value("on").toBool());
}
void SessionWorker::showDevicesDuringSession() { m_Visible=false; if(m_Session)m_Session->setViewerRequested(false); send({{"type","devices"}}); }
void SessionWorker::prepareViewerRecall() { send({{"type","devices"}}); }
void SessionWorker::showNextSession() { if(m_Visible) send({{"type","next"}}); }
void SessionWorker::requestShortcutToggle() {
    if (m_Visible && !m_Stopping && ShortcutState::enabled()) send({{"type","shortcut"}});
}
void SessionWorker::finish(const QString& error) {
    if(m_Finished)return; m_Finished=true;
    if(!error.isEmpty())send({{"type","error"},{"message",error}});
    m_Socket.flush(); QTimer::singleShot(0,qApp,&QCoreApplication::quit);
}
