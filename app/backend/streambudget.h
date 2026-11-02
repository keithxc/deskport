// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace DeskPortStream {
// Resolution-aware desktop budget: 15 Mbps for 2560x1440 at 60 fps, scaled by
// pixels and frame rate, 5-80 Mbps. Slow links are adapted live by the host.
inline int automaticBitrate(int width, int height, int fps, bool yuv444) {
    const auto pixels = std::int64_t(std::max(1, width)) * std::max(1, height);
    const auto budget = pixels * std::min(120, std::max(1, fps)) * 15000 / (2560LL * 1440 * 60);
    return int(std::clamp<std::int64_t>(budget * (yuv444 ? 2 : 1), 5000, 80000));
}
// Office data saver keeps the desktop resolution and limits video to 1 Mbps.
// This is a video ceiling, not a total transport-byte or hourly-data guarantee.
inline int dataSaverBitrate(int) { return 1000; }

struct SessionMediaSettings {
    bool remoteAudio;
    bool smartStreaming;
    bool operator==(const SessionMediaSettings& other) const {
        return remoteAudio == other.remoteAudio && smartStreaming == other.smartStreaming;
    }
    bool operator!=(const SessionMediaSettings& other) const { return !(*this == other); }
};

// Data saver overrides the running session without changing saved preferences.
// Audio capture and smart cadence are launch-time options on the current host.
inline SessionMediaSettings sessionMediaSettings(bool dataSaver, bool remoteAudio, bool smartStreaming) {
    return {remoteAudio && !dataSaver, smartStreaming || dataSaver};
}

inline bool dataSaverNeedsReconnect(const SessionMediaSettings& negotiated,
                                   const SessionMediaSettings& requested, bool liveRateSupported) {
    return negotiated != requested || !liveRateSupported;
}

// Automatic streaming ignores the saved manual bitrate; manual streaming keeps
// it. Data saver caps either one.
inline int sessionBitrate(bool automatic, bool dataSaver, int manualKbps, int width, int height, int fps, bool yuv444) {
    const int budget = automaticBitrate(width, height, fps, yuv444);
    const int chosen = automatic ? budget : std::max(1, manualKbps);
    return dataSaver ? std::min(chosen, dataSaverBitrate(budget)) : chosen;
}
}
