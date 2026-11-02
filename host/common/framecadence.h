// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "smartstream.h"
#include "inputactivity.h"

namespace deskport {
// A still picture converges over several duplicate encodes. After content
// stops changing, keep duplicates at the full cadence briefly instead of the
// one-fps idle floor; measured text took about six seconds to sharpen at 1 fps.
constexpr std::int64_t kRefineMs = 500;
inline int refineFrameRate(std::int64_t nowMs, std::int64_t lastChangeMs, int ceiling) {
    return lastChangeMs >= 0 && nowMs - lastChangeMs < kRefineMs ? std::max(1, ceiling) : 1;
}

// Data saver. On a remote desktop the per-frame cost dominates: even an empty
// frame pads to a full packet plus FEC. A change is still sent at once,
// continuous change at most kSaverContentFps, and an unchanged picture is
// refreshed every kSaverIdleMs. Resolution and the negotiated frame rate are
// unchanged.
constexpr int kSaverContentFps = 8;
constexpr std::int64_t kSaverIdleMs = 5000;
// Like zero-hertz screen sharing, repeat after changes before entering idle.
// Packet size is only a convergence heuristic, not a decoder quality report:
// require two small duplicates and bound the work even if sizes stay unknown.
constexpr int kSaverRefineFps = 4;
constexpr std::int64_t kSaverRefineMaxMs = 3000;
constexpr int kSaverConvergedBytes = 512;
constexpr int kSaverConvergedFrames = 2;
// Skip extra duplicates for small updates such as a clock tick or caret: each
// frame pads to a full packet plus parity. Preserve any pending large update.
constexpr int kSaverSmallChangeBytes = 1024;
inline bool saverRefines(int encodedBytes) { return encodedBytes < 0 || encodedBytes > kSaverSmallChangeBytes; }
class SaverRefinement {
public:
    // Small changes preserve the current deadline, but interrupt convergence.
    // Unknown sizes (including a requested recovery) always start refinement.
    void changed(std::int64_t nowMs, int bytes = -1) {
        stableFrames = 0;
        if (saverRefines(bytes)) untilMs = nowMs + kSaverRefineMaxMs;
    }
    void refreshed(int bytes) {
        stableFrames = bytes >= 0 && bytes <= kSaverConvergedBytes ? stableFrames + 1 : 0;
        if (stableFrames >= kSaverConvergedFrames) untilMs = 0;
    }
    bool active(std::int64_t nowMs) const { return nowMs < untilMs; }
private:
    std::int64_t untilMs = 0;
    int stableFrames = 0;
};
inline int saverFrameRate(std::int64_t nowMs, bool refining, ActivityBoost boost, int ceiling) {
    const bool active = refining || nowMs < boost.untilMs;
    return active ? std::max(1, std::min(ceiling, kSaverRefineFps)) : 1;
}
// Encoded size of the latest picture on this encoder thread, by frame number.
struct EncodedFrame { std::int64_t frame = -1; int bytes = -1; };
inline thread_local EncodedFrame lastEncodedFrame;
inline int encodedBytes(std::int64_t frame) { return lastEncodedFrame.frame == frame ? lastEncodedFrame.bytes : -1; }
// Spaces captured content frames; recovery frames always pass.
class SaverGate {
public:
    bool admit(std::int64_t nowUs, bool recovery) {
        if (!recovery && nowUs < next) return false;
        next = nowUs + 1000000 / kSaverContentFps;
        return true;
    }
private:
    std::int64_t next = 0;
};

// Capture adapters report changed content; lack of input never suppresses video.
// Keep pending content across denied slots so the final desktop update is sent.
class FrameCadence {
public:
    explicit FrameCadence(int fps, std::int64_t startMs = 0) : policy(fps, startMs) {}
    StreamPolicy policy;
    ActivityBoost boost;
    bool saver = false;
    int floor(std::int64_t nowUs) {
        const int ceiling = policy.frameRate(nowUs / 1000);
        if (saver) return saverFrameRate(nowUs / 1000, refinement.active(nowUs / 1000), boost, ceiling);
        return std::max(activityFrameRate(boost, nowUs / 1000, ceiling),
                        refineFrameRate(nowUs / 1000, lastChange / 1000, ceiling));
    }
    bool admit(std::int64_t nowUs, bool changed, bool recovery) {
        // Saver decides whether a change refines once its encoded size is known.
        if (changed) (saver ? candidate : lastChange) = nowUs;
        if (saver && recovery) {
            refinement.changed(nowUs / 1000);
            candidate = nowUs; // the recovery output is not a converged duplicate
        }
        pending = pending || changed;
        const int fps = floor(nowUs);
        if (recovery || pending) {
            if (saver && !gate.admit(nowUs, recovery)) return false;
            if (!policy.admit(nowUs, recovery)) return false;
        } else if (sent && nowUs - lastEncoded < (saver && fps <= 1 ? kSaverIdleMs * 1000 : 1000000 / fps)) {
            return false;
        }
        lastEncoded = nowUs;
        pending = false;
        sent = true;
        return true;
    }
    // Reports the size of the frame encoded after admit(); -1 when unknown.
    void encoded(int bytes) {
        if (candidate < 0) {
            refinement.refreshed(bytes);
            return;
        }
        refinement.changed(candidate / 1000, bytes);
        candidate = -1;
    }
private:
    SaverGate gate;
    SaverRefinement refinement;
    bool pending = false, sent = false;
    std::int64_t lastEncoded = 0, lastChange = -1000000000, candidate = -1;
};
}
