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
// Data saver spends the automatic per-frame budget at its 8 fps content cadence,
// so each picture keeps normal quality while the 1x desktop and cadence cut the
// rate (about 1.4 Mbps for 2040x1228, 0.56 Mbps for 1280x800). It aims at office
// work within about 300 MB/hour; this is a video ceiling, not a byte guarantee.
constexpr int kDataSaverFps = 8;
inline int dataSaverBitrate(int width, int height) {
    const auto pixels = std::int64_t(std::max(1, width)) * std::max(1, height);
    return int(std::clamp<std::int64_t>(pixels * kDataSaverFps * 15000 / (2560LL * 1440 * 60), 500, 4000));
}

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
// A host without per-session settings cannot drop audio; saver then only caps
// the bitrate, so enabling it never makes the launch request fail.
inline SessionMediaSettings sessionMediaSettings(bool dataSaver, bool remoteAudio, bool smartStreaming,
                                                 bool hostSessionSettings = true) {
    if (!hostSessionSettings) return {remoteAudio, smartStreaming};
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
    return dataSaver ? std::min(chosen, dataSaverBitrate(width, height)) : chosen;
}
}
