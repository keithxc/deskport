// SPDX-License-Identifier: GPL-3.0-or-later
#include "seamlesskeymap.h"

#include <QKeyEvent>

namespace SeamlessKeymap {

quint32 evdevCodeForMacVirtualKey(quint32 virtualKey)
{
    // Physical key positions (ANSI/ISO/JIS kVK_* constants from Carbon
    // Events.h) mapped to linux/input-event-codes.h. Layout translation is the
    // remote keymap's job, as it is for a local Linux keyboard.
    static const quint16 table[128] = {
        /* 0x00 A */ 30, /* S */ 31, /* D */ 32, /* F */ 33,
        /* 0x04 H */ 35, /* G */ 34, /* Z */ 44, /* X */ 45,
        /* 0x08 C */ 46, /* V */ 47, /* ISO section */ 86, /* B */ 48,
        /* 0x0C Q */ 16, /* W */ 17, /* E */ 18, /* R */ 19,
        /* 0x10 Y */ 21, /* T */ 20, /* 1 */ 2, /* 2 */ 3,
        /* 0x14 3 */ 4, /* 4 */ 5, /* 6 */ 7, /* 5 */ 6,
        /* 0x18 = */ 13, /* 9 */ 10, /* 7 */ 8, /* - */ 12,
        /* 0x1C 8 */ 9, /* 0 */ 11, /* ] */ 27, /* O */ 24,
        /* 0x20 U */ 22, /* [ */ 26, /* I */ 23, /* P */ 25,
        /* 0x24 Return */ 28, /* L */ 38, /* J */ 36, /* ' */ 40,
        /* 0x28 K */ 37, /* ; */ 39, /* \ */ 43, /* , */ 51,
        /* 0x2C / */ 53, /* N */ 49, /* M */ 50, /* . */ 52,
        /* 0x30 Tab */ 15, /* Space */ 57, /* ` */ 41, /* Backspace */ 14,
        /* 0x34 */ 0, /* Escape */ 1, /* Right Command */ 126, /* Command */ 125,
        /* 0x38 Shift */ 42, /* Caps Lock */ 58, /* Option */ 56, /* Control */ 29,
        /* 0x3C Right Shift */ 54, /* Right Option */ 100, /* Right Control */ 97, /* Fn */ 0,
        /* 0x40 F17 */ 187, /* Keypad . */ 83, /* */ 0, /* Keypad * */ 55,
        /* 0x44 */ 0, /* Keypad + */ 78, /* */ 0, /* Keypad Clear */ 69,
        /* 0x48 Volume Up */ 115, /* Volume Down */ 114, /* Mute */ 113, /* Keypad / */ 98,
        /* 0x4C Keypad Enter */ 96, /* */ 0, /* Keypad - */ 74, /* F18 */ 188,
        /* 0x50 F19 */ 189, /* Keypad = */ 117, /* Keypad 0 */ 82, /* Keypad 1 */ 79,
        /* 0x54 Keypad 2 */ 80, /* Keypad 3 */ 81, /* Keypad 4 */ 75, /* Keypad 5 */ 76,
        /* 0x58 Keypad 6 */ 77, /* Keypad 7 */ 71, /* F20 */ 190, /* Keypad 8 */ 72,
        /* 0x5C Keypad 9 */ 73, /* JIS Yen */ 124, /* JIS Underscore */ 89, /* JIS Keypad , */ 121,
        /* 0x60 F5 */ 63, /* F6 */ 64, /* F7 */ 65, /* F3 */ 61,
        /* 0x64 F8 */ 66, /* F9 */ 67, /* JIS Eisu */ 0, /* F11 */ 87,
        /* 0x68 JIS Kana */ 0, /* F13 */ 183, /* F16 */ 186, /* F14 */ 184,
        /* 0x6C */ 0, /* F10 */ 68, /* Context Menu */ 127, /* F12 */ 88,
        /* 0x70 */ 0, /* F15 */ 185, /* Help/Insert */ 110, /* Home */ 102,
        /* 0x74 Page Up */ 104, /* Forward Delete */ 111, /* F4 */ 62, /* End */ 107,
        /* 0x78 F2 */ 60, /* Page Down */ 109, /* F1 */ 59, /* Left */ 105,
        /* 0x7C Right */ 106, /* Down */ 108, /* Up */ 103, /* */ 0,
    };
    return virtualKey < 128 ? table[virtualKey] : 0;
}

namespace {
quint32 evdevCodeForModifierKey(int key)
{
    // Fallback when the platform reports no native key for a modifier.
    switch (key) {
    case Qt::Key_Shift:
        return 42;
    case Qt::Key_Control:
        return 29;
    case Qt::Key_Alt:
        return 56;
    case Qt::Key_Meta:
        return 125;
    case Qt::Key_CapsLock:
        return 58;
    default:
        return 0;
    }
}
} // namespace

quint32 evdevCodeForEvent(const QKeyEvent* event)
{
    quint32 code = 0;
#if defined(Q_OS_MACOS)
    code = evdevCodeForMacVirtualKey(event->nativeVirtualKey());
#elif defined(Q_OS_LINUX)
    // XCB and Wayland report XKB key codes, which are evdev codes plus 8.
    if (event->nativeScanCode() > 8 && event->nativeScanCode() < 776)
        code = event->nativeScanCode() - 8;
#endif
    if (code == 0)
        code = evdevCodeForModifierKey(event->key());
    return code;
}

} // namespace SeamlessKeymap
