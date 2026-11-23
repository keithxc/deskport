#pragma once

#include <cstdint>

// A mute request from the viewer activity loop starts the idle deadline.
// The SDL maintenance thread also advances it when no audio packets arrive.
class AudioIdleState
{
public:
    enum class Action { None, Mute, Pause, Resume };

    Action update(bool muted, uint32_t now)
    {
        if (muted != m_Muted) {
            m_Muted = muted;
            m_Paused = false;
            m_MutedAt = now;
            return muted ? Action::Mute : Action::Resume;
        }
        if (muted && !m_Paused && uint32_t(now - m_MutedAt) >= 1500) {
            m_Paused = true;
            return Action::Pause;
        }
        return Action::None;
    }

    bool muted() const { return m_Muted; }

    uint32_t timeUntilPause(uint32_t now) const
    {
        if (!m_Muted || m_Paused) {
            return UINT32_MAX;
        }
        const uint32_t elapsed = now - m_MutedAt;
        return elapsed >= 1500 ? 0 : 1500 - elapsed;
    }

private:
    bool m_Muted = false;
    bool m_Paused = false;
    uint32_t m_MutedAt = 0;
};
