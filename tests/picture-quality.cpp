// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/common/picturequality.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    assert(deskport::sharpPictureBudget(992, 2040, 1228));
    assert(!deskport::sharpPictureBudget(499, 2040, 1228));
    assert(deskport::sharpPictureBudget(351, 1280, 800));
    assert(!deskport::sharpPictureBudget(751, 2040, 1228));
    assert(deskport::sharpPictureBudget(752, 2040, 1228));
    assert(deskport::sharpPictureBudget(300, 1000, 1000));
    assert(!deskport::sharpPictureBudget(299, 1000, 1000));
    for (const int invalid : {0, -1, std::numeric_limits<int>::min()}) {
        assert(!deskport::sharpPictureBudget(invalid, 2040, 1228));
        assert(!deskport::sharpPictureBudget(992, invalid, 1228));
        assert(!deskport::sharpPictureBudget(992, 2040, invalid));
    }
    assert(!deskport::sharpPictureBudget(std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    assert(deskport::sharpPictureBudget(std::numeric_limits<int>::max(), 1, 1));
    deskport::PictureQuality quality;
    quality.captured();
    assert(quality.requestedMaximum(false) == 51); // ordinary mode
    quality.completed(false);
    quality.configured(true);
    quality.captured();
    quality.captured(); // coalesced/pending capture is still one first attempt
    assert(quality.requestedMaximum(false) == 26);
    assert(quality.needsMaximum(26));
    quality.appliedMaximum(26);
    assert(!quality.needsMaximum(26));
    quality.completed(true);
    assert(quality.requestedMaximum(false) == 51); // refinement consumes no fresh mark
    quality.appliedMaximum(51);
    quality.completed(true);
    assert(!quality.needsMaximum(51));

    quality.captured();
    assert(quality.requestedMaximum(false) == 26);
    quality.completed(false);
    quality.captured();
    quality.configured(true); // a rate update must not cancel the lenient retry
    assert(quality.requestedMaximum(false) == 51);
    quality.completed(false);
    quality.captured();
    assert(quality.requestedMaximum(false) == 26);
    quality.completed(true);

    // Even continuously changing content cannot starve delivery with Max26.
    for (int attempt = 0; attempt < 20; ++attempt) {
        quality.captured();
        assert(quality.requestedMaximum(false) == (attempt % 2 ? 51 : 26));
        quality.completed(false);
    }
    quality.captured();
    assert(quality.requestedMaximum(true) == 51); // recovery wins over fresh/strict
    quality.completed(false);
    quality.captured();
    assert(quality.requestedMaximum(false) == 26);
    quality.completed(false);
    assert(quality.requestedMaximum(true) == 51); // KEY also consumes a retry
    quality.completed(true);
    assert(quality.requestedMaximum(false) == 51); // no stale fresh mark after KEY
    quality.completed(false);

    quality.captured();
    assert(quality.requestedMaximum(false) == 26);
    quality.completed(false);
    quality.configured(false); // congestion/saver off clears the sharp policy
    quality.captured();
    assert(quality.requestedMaximum(false) == 51);
    assert(!quality.needsMaximum(51));
    quality.completed(false);
    quality.configured(true);
    assert(quality.requestedMaximum(false) == 51);
    quality.completed(false);
    deskport::PictureQuality other;
    other.configured(true);
    assert(other.requestedMaximum(false) == 51);
    std::cout << "PASS: fresh first attempt, refinement, drop retry, continuous capture, KEY and policy transitions\n";
}
