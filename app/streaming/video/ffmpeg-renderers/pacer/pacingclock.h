#pragma once
#include <chrono>

// Advance deadlines against a monotonic clock and discard missed ticks after
// suspension. The clock does not claim physical display-vblank alignment.
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    PacingClock(int hz, Clock::time_point now)
        : m_Interval(std::chrono::nanoseconds(1000000000LL / hz)), m_Next(now + m_Interval) {}
    Clock::time_point deadline() const { return m_Next; }
    void reset(Clock::time_point now) { m_Next = now + m_Interval; }
    void advance(Clock::time_point now) {
        m_Next += m_Interval;
        if (m_Next < now) m_Next = now + m_Interval;
    }
private:
    Clock::duration m_Interval;
    Clock::time_point m_Next;
};
