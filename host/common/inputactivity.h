// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace deskport {
struct ActivityBoost {
    std::int64_t untilMs = 0;
    int fps = 1;
};
// Authenticated control-thread state, reset with each media session. No key values
// are stored. Signed net displacement filters alternating one-pixel sensor jitter.
class InputActivity {
public:
    ActivityBoost observe(const unsigned char* p, std::size_t size, std::int64_t now) {
        if (!p || size < 8) return {};
        const auto declared = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
            (std::uint32_t(p[2]) << 8) | p[3];
        if (declared != size - 4) return {};
        const auto type = std::uint32_t(p[4]) | (std::uint32_t(p[5]) << 8) |
            (std::uint32_t(p[6]) << 16) | (std::uint32_t(p[7]) << 24);
        auto word = [p](int offset) { return int(std::int16_t((unsigned(p[offset]) << 8) | p[offset + 1])); };
        bool motion = false;
        if (type == 7 && size == 12) {
            if (now - motionStart > 120) { dx = dy = 0; motionStart = now; }
            dx += word(8); dy += word(10);
            if (std::abs(dx) + std::abs(dy) < 3) return {};
            dx = dy = 0; motionStart = now; motion = true;
        } else if (type == 5 && size == 18) {
            const int x = word(8), y = word(10), w = word(14), h = word(16);
            if (w <= 0 || h <= 0 || x < 0 || y < 0 || x > w || y > h) return {};
            if (!absolute || w != absWidth || h != absHeight) {
                absolute = true; absX = x; absY = y; absWidth = w; absHeight = h;
                return {};
            }
            if (std::abs(x - absX) + std::abs(y - absY) < 3) return {};
            absX = x; absY = y; motion = true;
        } else if (!(((type == 3 || type == 4) && size == 14) ||
                     ((type == 8 || type == 9) && size == 9) ||
                     (type == 10 && size == 14 && word(8) != 0) ||
                     (type == 0x55000001 && size == 10 && word(8) != 0) ||
                     (type == 0x17 && size > 8 && size <= 40))) return {};
        if (now - last > 200) { burstStart = now; count = 0; }
        last = now; count = std::min(count + 1, 100);
        const bool sustained = count >= 3 && now - burstStart >= 80;
        const int fps = 60;
        const auto until = now + (sustained ? 700 : motion ? 180 : 350);
        if (now >= boost.untilMs) boost = {};
        boost.fps = std::max(boost.fps, fps);
        boost.untilMs = std::max(boost.untilMs, until);
        return boost;
    }
private:
    int dx = 0, dy = 0, absX = 0, absY = 0, absWidth = 0, absHeight = 0, count = 0;
    bool absolute = false;
    std::int64_t motionStart = 0, last = -1000, burstStart = 0;
    ActivityBoost boost;
};
inline int activityFrameRate(ActivityBoost boost, std::int64_t now, int ceiling) {
    return std::max(1, std::min(ceiling, now < boost.untilMs ? boost.fps : 1));
}
}
