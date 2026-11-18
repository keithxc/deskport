#pragma once
#include <cstdint>
#include <optional>

// Client window state only. Transport capability/acknowledgement remains in
// AdaptiveDisplay; unavailable windows never imply a disconnected session.
class ViewerActivity {
public:
    void update(bool requested, bool unavailable, bool focused, uint64_t now) {
        m_Available = requested && !unavailable;
        m_Focused = m_Available && focused;
        if (m_Available) m_HiddenSince.reset();
        else if (!m_HiddenSince) m_HiddenSince = now;
    }
    bool available() const { return m_Available; }
    // Native hide/minimize events can remain queued after an immediate restore.
    // Track the actual release as well as visibility, so that case resumes once.
    void inputSuspended() { m_InputAvailable = false; }
    bool takeInputChange() {
        if (m_InputAvailable == m_Available) return false;
        m_InputAvailable = m_Available;
        return true;
    }
    bool videoPaused(uint64_t now) const { return m_HiddenSince && now - *m_HiddenSince >= 5000; }
    bool audioMuted(bool receiveAudio, bool muteOnFocusLoss) const {
        return !receiveAudio || (muteOnFocusLoss && !m_Focused);
    }
private:
    bool m_Available = false, m_Focused = false, m_InputAvailable = true;
    std::optional<uint64_t> m_HiddenSince;
};
