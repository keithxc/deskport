// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace deskport {
// Exact comparison of visible plane bytes: padding changes are not screen changes.
inline bool samePlane(const unsigned char* a, std::size_t aStride,
                      const unsigned char* b, std::size_t bStride,
                      std::size_t rowBytes, std::size_t rows) {
    if (!a || !b || !rowBytes || !rows || aStride < rowBytes || bStride < rowBytes) return false;
    for (std::size_t y = 0; y < rows; ++y)
        if (std::memcmp(a + y * aStride, b + y * bStride, rowBytes)) return false;
    return true;
}

// Only unrecoverable FEC blocks are congestion evidence. Parse without alignment
// assumptions and reject invalid sizes/counts before the streaming mailbox.
inline bool unrecoverableFec(const unsigned char* p, std::size_t size) {
    if (!p || size != 21) return false;
    auto u16 = [p](int n) { return (unsigned(p[n]) << 8) | p[n + 1]; };
    const auto data = u16(10), parity = u16(12), received = u16(14), recovered = u16(16);
    return data > 0 && data + parity <= 255 && received <= data && recovered <= parity &&
        p[20] >= 1 && p[20] <= 4 && p[19] < p[20] && received + recovered < data;
}

// Session-local, monotonic-time policy. Do not infer congestion from intentional
// idle frames, low content frame rates, or render-queue drops.
class StreamPolicy {
public:
    explicit StreamPolicy(int fps) : ceiling(std::max(1, fps)), target(ceiling) {}
    void loss(std::int64_t nowMs) {
        lastLoss = nowMs;
        if (nowMs - lastEpisode < 1000) return; // one burst is one episode
        if (nowMs - lastEpisode > 5000) episodes = 0;
        lastEpisode = nowMs;
        ++episodes;
        if (episodes >= 3 && nowMs - lastChange >= 8000) {
            target = std::max(std::min(15, ceiling), target / 2);
            lastChange = nowMs; episodes = 0; activeFrames = 0;
        }
    }
    int frameRate(std::int64_t nowMs) {
        if (target < ceiling && nowMs - lastLoss >= 30000 &&
            nowMs - lastChange >= 30000 && activeFrames >= 300) {
            target = std::min(ceiling, target * 2);
            lastChange = nowMs; activeFrames = 0; episodes = 0;
        }
        return target;
    }
    bool admit(std::int64_t nowUs, bool recovery = false) {
        if (recovery || target == ceiling) { ++activeFrames; nextFrame = nowUs; return true; }
        if (nowUs + 1000 < nextFrame) return false;
        ++activeFrames;
        const auto period = 1000000 / target;
        nextFrame = std::max(nextFrame + period, nowUs + period - 1000);
        return true;
    }
private:
    int ceiling, target, episodes = 0;
    std::int64_t lastLoss = 0, lastEpisode = -10000, lastChange = 0, nextFrame = 0;
    std::uint64_t activeFrames = 0;
};
}
