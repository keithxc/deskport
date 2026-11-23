// PeerManager members that verify and grant remote session requests.
// Moved unchanged from peermanager.cpp.
#include "sessionfailure.h"
#include "diagnostics.h"
#include "smalltcp.h"
#include "../../shared/deskport-core/include/deskport/protocol.h"
#include "peermanager.h"
#include "peermanager-link.h"
#include "sessiongraph.h"
#include <QSysInfo>
#include <QPointer>
#include "peerstore.h"
#include "pairinginvite.h"
#include "clipboardprotocol.h"
#include "clipboard/process.h"
#ifdef Q_OS_MACOS
#include "macclipboard.h"
#endif
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include "nvaddress.h"
#include "localhostfilter.h"
#include <QSslSocket>
#include <QUrl>
#include <QSslError>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QHostInfo>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <functional>
#include <QDebug>
#include <QRegularExpression>
#include <QRandomGenerator>

using namespace PeerManagerDetail;

void PeerManager::sessionError(Link* link, const QString& code) {
    send(link, {{"type", DP_MESSAGE_SESSION_RESULT}, {"admitted", false}, {"code", code},
                {"error", DeskPortSessionFailure::message(code)}});
}

void PeerManager::sessionRequest(Link* link, const QJsonObject& message) {
    const auto peer = m_Peers.value(link->fingerprint).toObject();
    if (!link->incoming || !peer["ready"].toBool() || !peer["granted"].toBool() ||
        m_Revoking == link->fingerprint) { sessionError(link, "unauthorized"); return; }
    const bool takeover = message["type"] == DP_MESSAGE_SESSION_TAKEOVER;
    if (takeover && !link->sessionOptIn) { sessionError(link, "unauthorized"); return; }
    if (!takeover) link->videoPause = message["videoPause"].toInt() == DP_VIDEO_PAUSE_VERSION;
    if (!takeover) link->videoRate = message["videoRate"].toInt() == DP_VIDEO_RATE_VERSION;
    if (!takeover) link->topologyOptIn = message["sessionTopology"].toInt() == DP_SESSION_TOPOLOGY_VERSION;
    // Old client-only mobile peers cannot be a host in a return path. Desktop
    // peers must register pending edges; otherwise concurrent cycles are unsafe.
    if (!link->topologyOptIn && peer["role"] != "client") { sessionError(link, "topology-unsupported"); return; }
    if (link->topologyPending || m_TopologyOperations >= 32) { sessionError(link, "busy"); return; }
    if (m_Link == link) m_Link = nullptr;
    link->topologyPending = true; ++m_TopologyOperations;
    const QPointer<Link> alive(link);
    SessionGraph::check(SessionGraph::identity(m_Certificate), {link->fingerprint}, this,
        [this, alive, message](QString code) {
        --m_TopologyOperations;
        if (!alive || alive->ended) return;
        alive->topologyPending = false;
        if (!code.isEmpty()) { sessionError(alive, code); return; }
        sessionRequestVerified(alive, message);
    });
}

void PeerManager::sessionRequestVerified(Link* link, const QJsonObject& message) {
    const auto peer = m_Peers.value(link->fingerprint).toObject();
    if (!link->incoming || link->requested || link->clipboardControl ||
        !peer["ready"].toBool() || !peer["granted"].toBool() || m_Revoking == link->fingerprint) {
        sessionError(link, "unauthorized"); return;
    }
    if (!m_Host->running()) { sessionError(link, "unavailable"); return; }
    const bool takeover = message["type"].toString() == DP_MESSAGE_SESSION_TAKEOVER;
    if ((!takeover && message["sessionTakeover"].toInt() != DP_SESSION_TAKEOVER_VERSION) ||
        (takeover && !link->sessionOptIn)) { sessionError(link, "unauthorized"); return; }
    link->lifecycle = message["sessionLifecycle"].toInt() == DP_SESSION_LIFECYCLE_VERSION;
    link->sessionOptIn = true;
    if (m_Link == link) m_Link = nullptr;
    if (m_SessionOperation) { sessionError(link, "busy"); return; }
    auto previous = m_SessionLink;
    if (!takeover && link != previous && previous && previous->lifecycle && !previous->ended &&
        link->lifecycle && previous->fingerprint == link->fingerprint &&
        !previous->sessionLease.isEmpty() && message["resumeToken"].toString() == previous->sessionLease) {
        link->sessionLease = previous->sessionLease;
        link->sessionAdmitted = true;
        link->displayControl = previous->displayControl;
        link->displayPolicy = previous->displayPolicy;
        link->clientWindow = previous->clientWindow; link->clientFullScreen = previous->clientFullScreen;
        link->caretUpdates = previous->caretUpdates;
        link->caretWasValid = previous->caretWasValid;
        m_SessionLink = link;
        if (m_DisplayLink == previous) m_DisplayLink = link;
        if (m_ClipboardLink && m_ClipboardLink->fingerprint == link->fingerprint)
            fail(m_ClipboardLink, tr("Clipboard transport replaced"));
        previous->sessionLease.clear();
        fail(previous, tr("Session transport recovered"));
        ++m_SessionEpoch;
    }
    if (link == m_SessionLink && link->sessionAdmitted) {
        link->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
        QJsonObject response{{"type", DP_MESSAGE_SESSION_STATE}, {"busy", false}, {"admitted", true}};
        if (link->lifecycle) response["resumeToken"] = link->sessionLease;
        send(link, response); return;
    }
    if (takeover) {
        const bool valid = !link->sessionChallenge.isEmpty() &&
            message["challenge"].toString() == link->sessionChallenge &&
            QDateTime::currentMSecsSinceEpoch() <= link->sessionDeadline && link->sessionEpoch == m_SessionEpoch;
        link->sessionChallenge.clear(); // Every attempted confirmation consumes it.
        if (!valid) { sessionError(link, "stale"); return; }
        if (!m_Host->browserSessionBusy()) { acquireSession(link, link->sessionSnapshot, true); return; }
        // A native client takes over a browser session. The browser supervisor
        // ends it and restores the display first; the host refuses to admit a
        // native stream while a browser reservation exists.
        m_SessionOperation = true;
        const QPointer<Link> alive(link);
        m_Host->endBrowserSession([this, alive] {
            m_Host->sessionControl({}, this, [this, alive](QJsonObject result) {
                m_SessionOperation = false;
                if (!alive || alive->ended) return;
                if (!result["status"].toBool() || result["reserved"].toBool() || result["snapshot"].toString().isEmpty()) {
                    sessionError(alive, "unavailable"); return;
                }
                acquireSession(alive, result["snapshot"].toString(), true);
            });
        });
        return;
    }
    m_SessionOperation = true;
    const QPointer<Link> alive(link);
    m_Host->sessionControl({}, this, [this, alive](QJsonObject result) {
        m_SessionOperation = false;
        if (!alive || alive->ended) return;
        auto link = alive.data();
        if (!result["status"].toBool() || result["snapshot"].toString().isEmpty() || !result["sessions"].isDouble()) {
            sessionError(link, "unavailable"); return;
        }
        if (m_SessionLink || m_DisplayLink || m_ClipboardLink || result["sessions"].toInt() > 0 || result["reserved"].toBool()) {
            link->sessionSnapshot = result["snapshot"].toString();
            link->sessionChallenge = QUuid::createUuid().toString(QUuid::WithoutBraces);
            link->sessionDeadline = QDateTime::currentMSecsSinceEpoch() + DP_SESSION_CONFIRMATION_TTL_MS;
            link->sessionEpoch = m_SessionEpoch;
            send(link, {{"type", DP_MESSAGE_SESSION_STATE}, {"busy", true}, {"admitted", false},
                        {"challenge", link->sessionChallenge}, {"expiresInMs", DP_SESSION_CONFIRMATION_TTL_MS}});
        } else acquireSession(link, result["snapshot"].toString(), false);
    });
}

void PeerManager::acquireSession(Link* link, const QString& snapshot, bool takeover) {
    m_SessionOperation = true;
    link->sessionLease = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto lease = link->sessionLease;
    const QPointer<Link> alive(link);
    m_Host->sessionControl({{"action", "acquire"}, {"uuid", trustId(link->fingerprint)},
        {"lease", lease}, {"snapshot", snapshot}, {"takeover", takeover}}, this,
        [this, alive, lease, takeover](QJsonObject result) {
        if (!result["status"].toBool()) {
            m_SessionOperation = false;
            if (alive && !alive->ended) sessionError(alive, result["code"].toString("unavailable"));
            return;
        }
        // The management response means all old streams have stopped and joined.
        // Their input contexts are gone before any new display lease is admitted.
        const bool settleParked = m_Host->displayParked() &&
            (!alive || m_Host->parkedDisplayOwner() != alive->fingerprint);
        const bool hadDisplay = m_DisplayLink || settleParked;
        auto oldSession = m_SessionLink;
        auto oldDisplay = m_DisplayLink;
        auto oldClipboard = m_ClipboardLink;
        const QPointer<ClipboardProcess> oldHelper(oldClipboard ? oldClipboard->clipboardHelper : nullptr);
        for (auto old : {oldSession, oldDisplay, oldClipboard}) {
            if (!old || old->ended || old == alive) continue;
            if (old->sessionOptIn) send(old, {{"type", DP_MESSAGE_SESSION_ENDED}, {"reason", "taken-over"}});
            fail(old, tr("Another device took over this session"));
        }
        auto finish = [this, alive, lease, takeover](bool restored) {
            m_SessionOperation = false;
            const auto peer = alive ? m_Peers.value(alive->fingerprint).toObject() : QJsonObject();
            if (!restored || !alive || alive->ended || !peer["ready"].toBool() || !peer["granted"].toBool() || m_Revoking == alive->fingerprint) {
                m_Host->sessionControl({{"action", "release"}, {"lease", lease}}, this, [](QJsonObject) {});
                if (alive && !alive->ended) sessionError(alive, "unavailable");
                return;
            }
            m_SessionLink = alive; ++m_SessionEpoch;
            alive->sessionAdmitted = true;
            m_Host->claimParkedDisplay(alive->fingerprint);
            alive->lastDisplayRequest = QDateTime::currentMSecsSinceEpoch();
            QJsonObject response{{"type", takeover ? DP_MESSAGE_SESSION_RESULT : DP_MESSAGE_SESSION_STATE},
                                 {"busy", false}, {"admitted", true}};
            if (alive->lifecycle) response["resumeToken"] = alive->sessionLease;
            send(alive, response);
            emit changed();
        };
        // Closing a helper's stdin is asynchronous. Do not hand a new client the
        // clipboard while the previous delayed-rendering process can still write.
        auto barrier = new QTimer(this);
        const auto displayDone = std::make_shared<bool>(!hadDisplay);
        const auto displayOkay = std::make_shared<bool>(true);
        if (hadDisplay) m_Host->settleSessionDisplay(barrier, [displayDone, displayOkay](bool ok) {
            *displayDone = true; *displayOkay = ok;
        });
        const auto deadline = QDateTime::currentMSecsSinceEpoch() + 10500;
        connect(barrier, &QTimer::timeout, barrier, [barrier, oldHelper, displayDone, displayOkay, deadline, finish] {
            const bool ready = *displayDone && (!oldHelper || oldHelper->state() == QProcess::NotRunning);
            if (!ready && QDateTime::currentMSecsSinceEpoch() < deadline) return;
            barrier->stop(); finish(ready && *displayOkay); barrier->deleteLater();
        });
        barrier->start(25);
    });
}
