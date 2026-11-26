#pragma once

#include <Limelight.h>
#include <libplacebo/colorspace.h>

extern "C" {
#include <libavutil/pixfmt.h>
}

inline enum pl_color_levels resolvePlVkColorLevels(int videoFormat,
                                                   enum AVColorRange colorRange,
                                                   enum pl_color_levels mappedLevels)
{
    // Preserve explicit full-range mappings (for example RGB frames or streams
    // tagged AVCOL_RANGE_JPEG in the bitstream).
    if (mappedLevels == PL_COLOR_LEVELS_FULL || colorRange == AVCOL_RANGE_JPEG) {
        return PL_COLOR_LEVELS_FULL;
    }

    // HACK: AMF AV1 encoding on Windows does not reliably signal full color
    // range in the bitstream even when full-range output was negotiated. Scope
    // that workaround to AV1 so limited-range H.264/HEVC streams (such as macOS
    // VideoToolbox) preserve their signaled limited range instead of lifting
    // blacks and desaturating colors.
    if (videoFormat & VIDEO_FORMAT_MASK_AV1) {
        return PL_COLOR_LEVELS_FULL;
    }

    if (colorRange == AVCOL_RANGE_MPEG) {
        return PL_COLOR_LEVELS_LIMITED;
    }

    return mappedLevels;
}
