// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace deskport::key_repeat {
// Key repeat follows the client's keyboard, not the arrival time of packets.
// A host that times repeats itself turns a key release delayed by the network
// into repeated characters. A client that sets this bit in the keyboard
// packet's flags on every key event forwards its own repeats as further
// key-down packets; the host then re-emits a held key only when asked and
// never starts its repeat timer for that key.
//
// The bit is self-describing per packet, so no negotiation is needed: an older
// host keeps its own timer and drops the repeated key-downs, and an older
// client never sets the bit. A client must use the same flags for the press,
// the repeats and the release of a key, because older hosts identify a held
// key by its code and flags together.
constexpr std::uint8_t kClientDriven = 0x80;

inline bool clientDriven(std::uint8_t flags) { return (flags & kClientDriven) != 0; }
// Flags as the host's own input code expects them.
inline std::uint8_t hostFlags(std::uint8_t flags) { return std::uint8_t(flags & ~kClientDriven); }

// Windows virtual-key codes whose local repeat is not forwarded: modifiers and
// lock keys carry state, not text or movement.
inline bool repeatable(std::uint16_t virtualKey) {
    switch (virtualKey & 0xFF) {
    case 0x10: case 0x11: case 0x12:              // Shift, Control, Alt
    case 0x14: case 0x90: case 0x91:              // Caps, Num and Scroll Lock
    case 0x5B: case 0x5C:                         // Left and right Meta
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
        return false;
    default:
        return true;
    }
}
}
