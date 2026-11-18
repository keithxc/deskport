// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

class QKeyEvent;

namespace SeamlessKeymap {

// Returns the Linux evdev key code for a local key event, or 0 when the
// physical key is unknown. The server then falls back to the Qt key value.
quint32 evdevCodeForEvent(const QKeyEvent* event);

// Exposed for tests: macOS virtual key code (kVK_*) to evdev code.
quint32 evdevCodeForMacVirtualKey(quint32 virtualKey);

} // namespace SeamlessKeymap
