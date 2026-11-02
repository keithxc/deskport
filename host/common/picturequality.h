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

// Try one sharp first picture after real capture. Refinement, idle and recovery
// pictures keep the encoder's full QP range, so a hard quality limit cannot
// indefinitely suppress delivery. This state belongs to one encoder thread.
class PictureQuality {
public:
    void configured(bool enabled) {
        active = enabled;
        maximum = 51; // the regular rate setter successfully wrote this range
        strict = false;
        if (!active) fresh = retry = false;
    }

    // Mark a popped capture; conversion failure exits before it is consumed.
    // A pending gate may retain it over several iterations until submission.
    void captured() { if (active) fresh = true; }

    int requestedMaximum(bool key) {
        strict = active && fresh && !key && !retry;
        fresh = false;
        retry = false;
        return strict ? 26 : 51;
    }

    bool needsMaximum(int requested) const { return maximum != requested; }
    void appliedMaximum(int applied) { maximum = applied; }

    // Only output for this call's logical frame counts. A delayed older packet
    // is not proof that the new picture survived the strict attempt.
    void completed(bool currentPictureProduced) {
        retry = strict && !currentPictureProduced;
        strict = false;
    }

private:
    bool active = false, fresh = false, retry = false, strict = false;
    int maximum = 51;
};
}
