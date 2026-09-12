// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace DeskPortStream {
// The saved bandwidth is a ceiling; keep text resolution and avoid starting
// ordinary desktop streams at a gaming-sized bitrate. Never raise a user limit.
inline int initialBitrate(int limit, int width, int height, int fps, bool yuv444) {
    const auto pixels = std::int64_t(std::max(1, width)) * std::max(1, height);
    const auto budget = pixels * std::min(120, std::max(1, fps)) * 15000 / (2560LL * 1440 * 60);
    return std::min(limit, int(std::clamp<std::int64_t>(budget * (yuv444 ? 2 : 1), 5000, 40000)));
}
}
