#include "clipboardsync.h"
#include "backend/clipboardprotocol.h"
#include <SDL.h>

ClipboardSync::ClipboardSync(std::unique_ptr<ClipboardChannel> channel) : m_Channel(std::move(channel)) {}
void ClipboardSync::tick() {
    if (!m_Channel) return;
    if (SDL_GetTicks() - m_LastPoll < 250) return;
    m_LastPoll = SDL_GetTicks();
    if (!m_Channel->error().isEmpty()) { m_Status = m_Channel->error(); return; }
    if (!m_Channel->ready()) return;
    // SDL owns the native clipboard connection while the streaming loop owns the
    // main thread. Never access the Qt clipboard from the network worker.
    char* raw = SDL_GetClipboardText();
    if (!raw) { m_Status = QStringLiteral("Unable to read the local clipboard."); return; }
    QString current = QString::fromUtf8(raw); SDL_free(raw);
    if (!m_Initialized) { m_Observed = current; m_Initialized = true; m_Changed = false; }
    if (current != m_Observed) { m_Observed = current; m_Dirty = true; ++m_LocalGeneration; }
    if (m_Changed && !SDL_HasClipboardText()) {
        ++m_LocalGeneration;
        m_Dirty = false;
        m_Status = QStringLiteral("Only plain text clipboard sharing is supported; images and files are not sent.");
    }
    m_Changed = false;
    QJsonObject reply;
    if (m_Channel->take(reply)) {
        m_InFlight = false;
        if (reply["rev"].toInt(-1) < m_Revision) {
            m_Status = QStringLiteral("Invalid clipboard revision; reconnect to resume sharing.");
            m_Channel.reset(); return;
        }
        m_Revision = reply["rev"].toInt();
        if (reply.contains("error")) m_Status = reply["error"].toString();
        if (reply.contains("text")) {
            QString remote;
            if (!DeskPortClipboard::decode(reply["text"], remote)) {
                m_Status = QStringLiteral("Invalid remote clipboard text.");
            } else if (current == m_SentSnapshot && m_LocalGeneration == m_SentGeneration) {
                if (SDL_SetClipboardText(remote.toUtf8().constData()) == 0) {
                    current = m_Observed = remote; m_Dirty = false; m_Status.clear();
                } else m_Status = QStringLiteral("Unable to write the local clipboard.");
            }
            // A new local copy made while the request was in flight remains
            // pending and is sent against the returned host revision.
        }
    }
    if (m_InFlight) return;
    QJsonObject request{{"type", "clipboard-poll"}, {"seq", m_Sequence + 1}, {"rev", m_Revision}};
    if (m_Dirty) {
        QString encoded;
        if (!DeskPortClipboard::encode(current, encoded))
            m_Status = QStringLiteral("Clipboard text exceeds 1 MiB or contains unsupported NUL characters.");
        else if (!SDL_HasClipboardText())
            m_Status = QStringLiteral("Only plain text clipboard sharing is supported; images and files are not sent.");
        else { request["text"] = encoded; m_Status.clear(); }
    }
    if (m_Channel->submit(request)) {
        ++m_Sequence; m_SentSnapshot = current; m_SentGeneration = m_LocalGeneration; m_Dirty = false; m_InFlight = true;
    }
}
