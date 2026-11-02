// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <optional>

namespace deskport::vt {
// VideoToolbox allocates bitrate over media time. Sparse desktop submissions
// must retain their real spacing, independently of sequential wire frame IDs.
// The host configures no B frames.
class FrameClock {
public:
    std::optional<std::int64_t> submit(std::int64_t nowUs, std::int64_t frameNumber,
                                      int timeBaseNum, int timeBaseDen) {
        if (nowUs < 0 || frameNumber < 0 || timeBaseNum <= 0 || timeBaseDen <= 0) return {};
        const auto epoch = firstUs < 0 ? nowUs : firstUs;
        const auto elapsed = std::max<std::int64_t>(0, nowUs - epoch);
        const long double ticks = static_cast<long double>(elapsed) * timeBaseDen /
                                  (1000000.0L * timeBaseNum);
        if (ticks >= static_cast<long double>(std::numeric_limits<std::int64_t>::max()) ||
            previous == std::numeric_limits<std::int64_t>::max()) return {};
        const auto pts = std::max(previous + 1, static_cast<std::int64_t>(ticks));
        firstUs = epoch;
        previous = pts;
        if (pending.size() == 256) pending.pop_front();
        pending.push_back({pts, frameNumber});
        return pts;
    }

    std::optional<std::int64_t> take(std::int64_t pts) {
        const auto found = std::find_if(pending.begin(), pending.end(),
                                       [pts](const Entry& e) { return e.pts == pts; });
        if (found == pending.end()) return {};
        const auto frame = found->frame;
        // With no B frames, earlier submitted identities cannot arrive later.
        pending.erase(pending.begin(), std::next(found));
        return frame;
    }

private:
    struct Entry { std::int64_t pts, frame; };
    std::int64_t firstUs = -1, previous = -1;
    std::deque<Entry> pending;
};
}
