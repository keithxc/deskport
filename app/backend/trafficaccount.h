// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#ifndef DP_DISABLE_LOCAL_INPUT_ALIASES
#define DP_DISABLE_LOCAL_INPUT_ALIASES
#endif
#include "../../shared/deskport-core/portable/include/deskport/stream_budget.h"
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace DeskPortTraffic {
// Today's client traffic across every session. Sessions report cumulative
// byte counters; only increases are added. A counter that falls belongs to a
// new session and counts from zero. Local midnight starts a new day.
class DailyUsage {
public:
    std::int64_t account(const std::string& today, const std::string& session, std::int64_t total) {
        rollover(today);
        if (total < 0) return 0;
        const auto previous = last.find(session);
        const std::int64_t delta = static_cast<std::int64_t>(dp_bytes_delta(
            previous == last.end() ? 0 : static_cast<std::uint64_t>(previous->second),
            static_cast<std::uint64_t>(total), previous != last.end()));
        last[session] = total;
        const auto maximum = std::numeric_limits<std::int64_t>::max();
        used = used > maximum - delta ? maximum : used + delta;
        return delta;
    }
    void rollover(const std::string& today) {
        if (today != day) { day = today; used = 0; }
    }
    void forget(const std::string& session) { last.erase(session); }
    void reset() { used = 0; }
    std::string day;
    std::int64_t used = 0;
private:
    std::map<std::string, std::int64_t> last;
};

enum class Level {
    Normal = DP_BUDGET_NORMAL, Warning = DP_BUDGET_WARNING,
    Critical = DP_BUDGET_CRITICAL, Exceeded = DP_BUDGET_EXCEEDED
};
// Yellow from 70%, red from 85%, exceeded at 100% of a positive daily limit.
inline Level level(std::int64_t used, std::int64_t limit) {
    if (limit <= 0 || used < 0) return Level::Normal;
    return static_cast<Level>(dp_budget_level(static_cast<std::uint64_t>(used),
                                             static_cast<std::uint64_t>(limit)));
}
}
