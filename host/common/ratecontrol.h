// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deskport {
// Remote paths get live rate control and pacing. On-link private addresses keep
// the inherited LAN behavior. Tailscale's 100.64/10 and fd7a:115c:a1e0::/48 are
// overlay routes that may be relayed, so they count as remote.
inline bool remotePeer(const unsigned char* bytes, int size) {
    if (!bytes) return false;
    if (size == 16) {
        static const unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (std::equal(mapped, mapped + 12, bytes)) return remotePeer(bytes + 12, 4);
        bool loopback = bytes[15] == 1;
        for (int i = 0; i < 15; ++i) loopback = loopback && bytes[i] == 0;
        if (loopback || (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80)) return false;
        if (bytes[0] == 0xfd && bytes[1] == 0x7a && bytes[2] == 0x11 && bytes[3] == 0x5c &&
            bytes[4] == 0xa1 && bytes[5] == 0xe0) return true;
        return (bytes[0] & 0xfe) != 0xfc; // Other unique-local addresses are site-local.
    }
    if (size != 4) return false;
    const unsigned a = bytes[0], b = bytes[1];
    return !(a == 10 || a == 127 || (a == 172 && (b & 0xf0) == 16) ||
             (a == 192 && b == 168) || (a == 169 && b == 254));
}

// Session-local live bitrate target for remote paths. Relayed overlays rarely
// lose packets; they queue them. Rising control-channel RTT above the recent
// minimum is therefore congestion, as are unrecoverable FEC blocks. Decrease
// multiplicatively, recover additively, never exceed the negotiated bitrate.
class BitrateController {
public:
    BitrateController(int ceilingKbps, std::int64_t startMs)
        : negotiated(std::max(1, ceilingKbps)), ceiling(std::max(1, ceilingKbps)), floorValue(std::min(ceiling, std::max(1000, ceiling / 8))),
          target(ceiling), lastChange(startMs), lastCongestion(startMs - 60000), lastDecrease(startMs - 60000) {
        mins.fill(-1);
    }
    // Continue from a recently learned rate, e.g. after an adaptive resize
    // reconnect, instead of probing the slow link from the ceiling again.
    void resume(int kbps) { target = std::clamp(kbps, floorValue, ceiling); }
    int ceilingKbps() const { return ceiling; }
    // Live viewer ceiling (data saver). The negotiated rate stays the maximum;
    // the floor never exceeds the ceiling and the target never exceeds it.
    void setCeiling(int kbps) {
        ceiling = std::max(1, std::min(kbps, negotiated));
        floorValue = std::min(ceiling, std::max(1000, negotiated / 8));
        target = std::clamp(target, floorValue, ceiling);
    }
    int floorKbps() const { return floorValue; }
    int baselineMs() const {
        int best = -1;
        for (int m : mins) if (m >= 0 && (best < 0 || m < best)) best = m;
        return best;
    }
    // Sampled about twice a second from the existing control connection.
    void rtt(std::int64_t nowMs, int rttMs) {
        if (rttMs <= 0) return;
        const auto bucket = (nowMs / 5000) % int(mins.size());
        if (nowMs / 5000 != bucketTime[bucket]) { bucketTime[bucket] = nowMs / 5000; mins[bucket] = rttMs; }
        else mins[bucket] = std::min(mins[bucket], rttMs);
        for (std::size_t i = 0; i < mins.size(); ++i)
            if (nowMs / 5000 - bucketTime[i] >= std::int64_t(mins.size())) mins[i] = -1;
        const int base = baselineMs();
        const bool queued = base >= 0 && rttMs > base + std::max(30, base / 4);
        delayed = queued ? delayed + 1 : 0;
        if (delayed >= 2) congested(nowMs, rttMs);
    }
    void loss(std::int64_t nowMs) { congested(nowMs, -1); }
    // Measured video send rate. When it is far below the target, a delay spike
    // was not caused by this stream, so the target falls gently.
    void sent(int kbps) { sentKbps = kbps; }
    int targetKbps(std::int64_t nowMs) {
        if (target < ceiling && nowMs - lastCongestion >= 5000 && nowMs - lastChange >= 3000) {
            target = std::min(ceiling, target + std::max(250, ceiling / 20));
            lastChange = nowMs;
        }
        return target;
    }
    // A large content change at a fraction of the negotiated budget otherwise
    // produces frames that take most of a second to cross the link.
    bool constrained() const { return target * 2 < ceiling; }
private:
    void congested(std::int64_t nowMs, int rttMs) {
        lastCongestion = nowMs;
        if (nowMs - lastDecrease < 2000) return; // Let the queue drain first.
        // Still draining since the last cut (RTT down by 10% or more): cutting
        // again only lowers quality. Live 5G showed repeated cuts to the floor
        // while RTT fell 279 -> 205 -> 135 -> 98 ms at about 1 Mbps actual.
        if (rttMs > 0 && decreaseRtt > 0 && nowMs - lastDecrease < 6000 && rttMs * 10 <= decreaseRtt * 9) return;
        const bool underused = sentKbps >= 0 && sentKbps * 2 < target;
        target = std::max(floorValue, underused ? target * 85 / 100 : target * 7 / 10);
        lastDecrease = lastChange = nowMs;
        decreaseRtt = rttMs;
        delayed = 0;
    }
    int negotiated, ceiling, floorValue, target, delayed = 0, decreaseRtt = -1, sentKbps = -1;
    std::int64_t lastChange, lastCongestion, lastDecrease;
    std::array<int, 6> mins {};
    std::array<std::int64_t, 6> bucketTime {-1, -1, -1, -1, -1, -1};
};

// Encoder properties must be refreshed when saver or quality policy changes,
// even if the numeric bitrate remains equal (for example a 500 kbps cap).
struct EncoderRateState {
    int kbps = 0;
    bool constrained = false, saver = false;
    bool matches(int nextKbps, bool nextConstrained, bool nextSaver) const {
        return kbps == nextKbps && constrained == nextConstrained && saver == nextSaver;
    }
    void applied(int nextKbps, bool nextConstrained, bool nextSaver) {
        kbps = nextKbps; constrained = nextConstrained; saver = nextSaver;
    }
};

// Host-wide memory of the last remote rate. A reconnect within five minutes
// with the same negotiated ceiling resumes from it.
struct RateMemory {
    int ceilingKbps = 0, targetKbps = 0;
    std::int64_t updatedMs = 0;
    int resumeFor(int ceiling, std::int64_t nowMs) const {
        return ceiling == ceilingKbps && targetKbps > 0 && nowMs - updatedMs <= 300000 ? targetKbps : 0;
    }
    void store(int ceiling, int target, std::int64_t nowMs) { ceilingKbps = ceiling; targetKbps = target; updatedMs = nowMs; }
};

// Coalesces recovery key frames. After a loss the client asks again for nearly
// every frame until a complete key frame arrives; on a lossy 5G link that
// produced eight 113 KB key frames in five seconds and a 97% frame loss window.
// One key frame already in flight satisfies repeats; a lost one is requested
// again after the window, which follows the path RTT (250 ms to 1 s).
class KeyframeGate {
public:
    bool allow(std::int64_t nowMs, int rttMs) {
        const std::int64_t window = std::clamp<std::int64_t>(std::int64_t(rttMs) * 2, 250, 1000);
        if (last >= 0 && nowMs - last < window) { ++coalesced; return false; }
        last = nowMs;
        return true;
    }
    std::uint64_t coalesced = 0;
private:
    std::int64_t last = -1;
};

// Live ceiling shared by the authenticated local API and the encoder thread of
// the single admitted stream: the viewer's ceiling in per mille of the bitrate
// it requested at launch (0 = none), and whether the encoder changes rate live.
inline std::atomic<int> requestedKbps {0};
inline std::atomic<int> liveCeilingPermille {0};
inline std::atomic<int> liveEncoder {-1};
// Viewer's data saver state: from the launch request, then from video-rate.
inline std::atomic<int> liveSaver {0};

// Data saver FEC: one parity shard per block instead of the viewer's minimum
// (two) while no block has been unrecoverable for kSaverCleanMs. A small frame
// then costs one packet less; any unrecoverable block restores the minimum.
constexpr std::int64_t kSaverCleanMs = 60000;
inline std::size_t saverMinParity(bool saver, std::size_t requested, std::int64_t nowMs, std::int64_t lastLossMs) {
    return saver && requested > 1 && nowMs - lastLossMs >= kSaverCleanMs ? 1 : requested;
}
inline int ceilingPermille(long long ceilingKbps, long long requested) {
    if (ceilingKbps <= 0 || requested <= 0 || ceilingKbps >= requested) return 0;
    return int(std::max<long long>(1, ceilingKbps * 1000 / requested));
}
inline int applyPermille(int kbps, int permille) {
    return permille > 0 ? std::max(1, int((long long)kbps * permille / 1000)) : kbps;
}

// Packets per millisecond for the inherited pacer. Remote sessions pace at four
// times the live target (at least 20 Mbps) instead of about 800 Mbps.
inline std::size_t remotePacketsPerMs(int targetKbps, std::size_t blockSize) {
    const std::int64_t kbps = std::max<std::int64_t>(20000, std::int64_t(targetKbps) * 4);
    const std::int64_t bytesPerMs = kbps / 8;
    return std::max<std::size_t>(1, (bytesPerMs + blockSize / 2) / std::max<std::size_t>(1, blockSize));
}
}
