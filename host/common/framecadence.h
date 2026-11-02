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
// frame pads to a full packet plus FEC. A change is still
// sent at once, continuous change at most kSaverContentFps; after a change or
// input only kSaverRefineFps duplicates refine the picture, and an unchanged
// picture is refreshed every kSaverIdleMs. Resolution and the negotiated frame
// rate are unchanged.
constexpr int kSaverContentFps = 8;
constexpr int kSaverRefineFps = 2;
// Leave enough time for one refinement encode at 500 ms. A 500 ms window with
// the strict comparison below would expire before that encode could happen.
constexpr std::int64_t kSaverRefineMs = 750;
constexpr std::int64_t kSaverIdleMs = 5000;
inline int saverFrameRate(std::int64_t nowMs, std::int64_t lastChangeMs, ActivityBoost boost, int ceiling) {
    const bool active = (lastChangeMs >= 0 && nowMs - lastChangeMs < kSaverRefineMs) || nowMs < boost.untilMs;
    return active ? std::max(1, std::min(ceiling, kSaverRefineFps)) : 1;
}
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
        if (saver) return saverFrameRate(nowUs / 1000, lastChange / 1000, boost, ceiling);
        return std::max(activityFrameRate(boost, nowUs / 1000, ceiling),
                        refineFrameRate(nowUs / 1000, lastChange / 1000, ceiling));
    }
    bool admit(std::int64_t nowUs, bool changed, bool recovery) {
        if (changed) lastChange = nowUs;
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
private:
    SaverGate gate;
    bool pending = false, sent = false;
    std::int64_t lastEncoded = 0, lastChange = -1000000000;
};
}
