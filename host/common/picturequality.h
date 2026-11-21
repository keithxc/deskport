// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace deskport {
// Conservative eligibility for this native Max26 attempt, not a quality
// guarantee: 992 kbps at 2040x1228 passed the local replay; 499 kbps dropped
// changed pictures. Require at least 300 effective kbps per million pixels.
// Divide the positive budget first so arbitrary int dimensions cannot overflow.
inline bool sharpPictureBudget(int kbps, int width, int height) {
    return kbps > 0 && width > 0 && height > 0 &&
        std::int64_t(kbps) * 1000000 / 300 >= std::int64_t(width) * height;
}

// Progressive sharpness for data saver:
// 1. Fresh content-change picture: allow a moderate ceiling (QP 34) so large
//    switches or interactive scrolling/dragging stay small and never burst-spike
//    or get dropped by VideoToolbox rate limits.
// 2. First stationary refinement duplicate after a change: sharpen to QP 26 so
//    stopped text converges cleanly within ~150-250 ms without a first-frame spike.
// 3. Subsequent refinement, idle and recovery pictures keep the full QP 51 range.
class PictureQuality {
public:
    void configured(bool enabled) {
        active = enabled;
        maximum = 51; // the regular rate setter successfully wrote this range
        strict = false;
        if (!active) { fresh = refine = retry = false; }
    }

    // Mark a popped capture; conversion failure exits before it is consumed.
    // Continuous motion keeps re-marking fresh, staying at QP 34 until motion stops.
    void captured() {
        if (active) {
            fresh = true;
            refine = false;
        }
    }

    int requestedMaximum(bool key) {
        if (key) {
            strict = fresh = refine = retry = false;
            return 51;
        }
        if (!active || retry) {
            strict = fresh = refine = retry = false;
            return 51;
        }
        if (fresh) {
            fresh = false;
            refine = true;
            strict = true;
            return 34;
        }
        if (refine) {
            refine = false;
            strict = true;
            return 26;
        }
        strict = false;
        return 51;
    }

    bool needsMaximum(int requested) const { return maximum != requested; }
    void appliedMaximum(int applied) { maximum = applied; }

    // Only output for this call's logical frame counts. A delayed older packet
    // is not proof that the new picture survived the bounded attempt.
    void completed(bool currentPictureProduced) {
        if (strict && !currentPictureProduced) {
            retry = true;
            refine = false;
        }
        strict = false;
    }

private:
    bool active = false, fresh = false, refine = false, retry = false, strict = false;
    int maximum = 51;
};
}
