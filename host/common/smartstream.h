// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace deskport {
// A generation, sampled before inspecting mailboxes, closes the gap between
// checking several sources and sleeping. Notifications never consume payloads.
class CadenceWake {
public:
    using Clock = std::chrono::steady_clock;
    explicit CadenceWake(std::shared_ptr<CadenceWake> alsoNotify = {})
        : combined(std::move(alsoNotify)) {}
    std::uint64_t generation() {
        std::lock_guard lock(mutex);
        return serial;
    }
    void notify() {
        {
            std::lock_guard lock(mutex);
            ++serial;
        }
        cv.notify_all();
        if (combined) combined->notify();
    }
    bool waitUntil(std::uint64_t observed, Clock::time_point deadline) {
        std::unique_lock lock(mutex);
        return cv.wait_until(lock, deadline, [&] { return serial != observed; });
    }
private:
    const std::shared_ptr<CadenceWake> combined;
    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t serial = 0;
};

// The mailbox owner holds its existing lock during subscription/notification.
// Weak ownership avoids retaining encoders after teardown or reinitialization.
class CadenceListeners {
public:
    void watch(const std::shared_ptr<CadenceWake>& wake) {
        for (auto it = listeners.begin(); it != listeners.end();) {
            auto current = it->lock();
            if (!current) it = listeners.erase(it);
            else if (current == wake) return;
            else ++it;
        }
        listeners.emplace_back(wake);
    }
    void notify() {
        for (auto it = listeners.begin(); it != listeners.end();) {
            if (auto wake = it->lock()) { wake->notify(); ++it; }
            else it = listeners.erase(it);
        }
    }
private:
    std::vector<std::weak_ptr<CadenceWake>> listeners;
};

// Only capture lifecycle, real activity and controls subscribe here. Ordinary
// image arrivals notify their own encoder, avoiding cross-session polling.
// A PipeWire snapshot waits for frames or controls. Its pacing sleep waits
// only for controls, so early producer frames stay buffered until their slot.
inline auto pipewireFrameOrControl = std::make_shared<CadenceWake>();
inline auto captureWake = std::make_shared<CadenceWake>(pipewireFrameOrControl);
class CadenceControls {
public:
    void watch(const std::shared_ptr<CadenceWake>& wake) {
        std::lock_guard lock(mutex);
        listeners.watch(wake);
    }
    void notify() {
        std::lock_guard lock(mutex);
        listeners.notify();
        captureWake->notify();
    }
private:
    std::mutex mutex;
    CadenceListeners listeners;
};
inline CadenceControls cadenceControls;
// Synchronous capture and encode share one thread. Its next encode slot drives
// PipeWire's idle deadline; asynchronous capture keeps the native idle limit.
inline thread_local std::chrono::steady_clock::time_point captureDeadline =
    std::chrono::steady_clock::time_point::max();
inline thread_local bool pipewireControlTicks = false;
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
    explicit StreamPolicy(int fps, std::int64_t startMs = 0)
        : ceiling(std::max(1, fps)), target(ceiling), lastLoss(startMs),
          lastEpisode(startMs - 10000), lastChange(startMs) {}
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
    std::int64_t readyUs(std::int64_t nowUs) const {
        return target == ceiling ? nowUs : std::max(nowUs, nextFrame - 1000);
    }
private:
    int ceiling, target, episodes = 0;
    std::int64_t lastLoss = 0, lastEpisode = -10000, lastChange = 0, nextFrame = 0;
    std::uint64_t activeFrames = 0;
};
}
