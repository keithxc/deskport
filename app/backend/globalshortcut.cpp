#include "globalshortcut.h"
#include "shortcutrecorderinhibitor.h"
#include <QGuiApplication>
#include <QDebug>

GlobalShortcut::GlobalShortcut(QObject* parent) : QObject(parent),
    m_RecorderInhibitor(new ShortcutRecorderInhibitor) { m_Clock.start(); }
GlobalShortcut::~GlobalShortcut() { if (m_Backend) m_Backend->stop(); }

void GlobalShortcut::setAvailable(bool available) {
    const bool changed = m_Available != available;
    m_Available = available;
    emit fallbackAvailabilityChanged(available && m_Backend && m_Backend->fixedChord);
    if (!available) {
        m_Gate.reset();
        m_Token.clear();
    } else {
        m_Error.clear();
    }
    emit statusChanged();
    if (changed) emit availabilityChanged(available);
}

void GlobalShortcut::start() {
    m_Started = true;
    if (!m_Enabled || m_Recording || m_Backend || qEnvironmentVariableIsSet("DESKPORT_DISABLE_GLOBAL_SHORTCUT")) return;
    // Isolated test/offscreen runs must not bind a real desktop shortcut or
    // display a portal permission dialog.
    const auto platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal") return;
    m_Backend = createGlobalShortcutBackend(this);
    m_Backend->binding = ShortcutBinding::parse(m_Sequence);
    m_Error.clear();
    auto* backend = m_Backend;
    m_Backend->edge = [this, backend](bool pressed, const QByteArray& token) {
        if (m_Backend != backend || !m_Enabled || !m_Available) return;
        if (!token.isEmpty()) m_Token = token;
        const bool activate = m_Gate.nativeEdge(pressed, m_Clock.elapsed());
        const auto activation = m_Token;
        // A suppressed release still ends this token's lifetime. Never reuse
        // a previous portal grant for a later shortcut activation.
        if (!pressed) m_Token.clear();
        if (activate) emit activated(activation);
    };
    m_Backend->availability = [this, backend](bool available) {
        if (m_Backend == backend && m_Enabled) setAvailable(available);
    };
    m_Backend->error = [this, backend](const QString& error) {
        if (m_Backend != backend || !m_Enabled) return;
        m_Error = error;
        setAvailable(false);
        qWarning().noquote() << "DeskPort global shortcut unavailable:" << error;
    };
    // Asynchronous backends report success after the user grants the binding.
    if (m_Backend->start()) setAvailable(true);
}

void GlobalShortcut::setEnabled(bool enabled) {
    if (m_Enabled == enabled) return;
    m_Enabled = enabled;
    if (enabled) {
        if (m_Started) start();
        return;
    }
    stop();
}

bool GlobalShortcut::setSequence(const QString& sequence) {
    const auto normalized = ShortcutBinding::parse(sequence).portable();
    if (normalized.isEmpty()) return false;
    if (normalized == m_Sequence) return true;
    stop();
    m_Sequence = normalized;
    m_Error.clear();
    emit statusChanged();
    if (m_Started) start();
    return true;
}

void GlobalShortcut::setRecording(bool recording) {
    if (m_Recording == recording) return;
    m_Recording = recording;
    m_RecorderInhibitor->setEnabled(recording);
    if (!recording) m_RecorderInhibitor->setWindow(nullptr);
    if (recording) stop();
    else if (m_Started) start();
}

void GlobalShortcut::setRecordingWindow(QWindow* window) {
    m_RecorderInhibitor->setWindow(window);
}

void GlobalShortcut::stop() {
    // Invalidate callbacks before stopping: an asynchronous portal reply or a
    // queued release from the old registration must never revive the binding.
    auto* backend = m_Backend;
    m_Backend = nullptr;
    setAvailable(false);
    m_Gate.reset();
    m_Token.clear();
    if (backend) {
        backend->stop();
        backend->deleteLater();
    }
}

void GlobalShortcut::fallbackActivated() {
    if (m_Available && m_Backend->fixedChord && m_Gate.fallback(m_Clock.elapsed())) emit activated({});
}
