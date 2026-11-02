// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/macos/vtclock.h"
#include <cassert>
#include <iostream>

int main() {
    deskport::vt::FrameClock clock;
    const auto first = clock.submit(1000000, 10, 1, 60);
    const auto second = clock.submit(1125000, 11, 1, 60);
    const auto third = clock.submit(1375000, 12, 1, 60);
    assert(first == 0 && second == 7 && third == 22);
    // Hardware can drop earlier submissions; output identity is not native PTS.
    assert(clock.take(*third) == 12);
    assert(!clock.take(*first) && !clock.take(*second));
    assert(clock.submit(6375000, 13, 1, 60) == 322);
    assert(clock.take(322) == 13); // five idle seconds refill media-time budget
    assert(!clock.take(322));

    deskport::vt::FrameClock monotonic;
    assert(!monotonic.submit(-1, 0, 1, 60));
    assert(!monotonic.submit(1000000, 0, 0, 60));
    assert(!monotonic.submit(1000000, 0, 1, 0));
    assert(!monotonic.submit(1000000, -1, 1, 60));
    assert(monotonic.submit(9000000, 0, 1001, 30000) == 0);
    assert(monotonic.submit(10001000, 1, 1001, 30000) == 30);
    assert(monotonic.submit(10001000, 2, 1001, 30000) == 31);
    assert(monotonic.submit(9000001, 3, 1001, 30000) == 32);
    assert(monotonic.submit(11002000, 4, 1001, 30000) == 60);
    assert(!monotonic.take(59));
    assert(monotonic.take(60) == 4);

    deskport::vt::FrameClock bounded;
    for (int i = 0; i < 400; ++i) assert(bounded.submit(i * 1000000LL, i, 1, 60));
    assert(!bounded.take(0)); // never invent an identity outside retained history
    assert(bounded.take(399 * 60) == 399);
    deskport::vt::FrameClock other;
    assert(other.submit(20000000, 99, 1, 60) == 0);
    assert(other.take(0) == 99);
    std::cout << "PASS: sparse media time, idle gaps, dropped-frame identity, strict monotonicity and session isolation\n";
}
