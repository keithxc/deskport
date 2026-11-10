// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/common/keyrepeat.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace deskport::key_repeat;

int main() {
    // The bit is distinct from the protocol's non-normalized flag and is
    // removed before the host's own input code sees the flags.
    assert(kClientDriven == 0x80);
    assert(!clientDriven(0x00) && !clientDriven(0x01));
    assert(clientDriven(0x80) && clientDriven(0x81));
    assert(hostFlags(0x80) == 0x00 && hostFlags(0x81) == 0x01 && hostFlags(0x01) == 0x01);

    // Text, editing and navigation keys repeat; modifiers and locks do not.
    for (int key : {0x08, 0x0D, 0x20, 0x25, 0x26, 0x27, 0x28, 0x2E, 0x4A, 0x70, 0xBA})
        assert(repeatable(std::uint16_t(key)) && repeatable(std::uint16_t(0x8000 | key)));
    for (int key : {0x10, 0x11, 0x12, 0x14, 0x5B, 0x5C, 0x90, 0x91, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5})
        assert(!repeatable(std::uint16_t(key)) && !repeatable(std::uint16_t(0x8000 | key)));
    std::puts("PASS: key repeat flag and repeatable keys");
}
