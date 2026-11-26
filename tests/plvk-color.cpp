// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/streaming/video/ffmpeg-renderers/plvk_color.h"

#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>

extern "C" {
#include <libavutil/frame.h>
}

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

AVFrame makeFrame(AVPixelFormat format, AVColorRange range,
                  AVColorSpace space = AVCOL_SPC_BT709,
                  AVColorPrimaries primaries = AVCOL_PRI_BT709,
                  AVColorTransferCharacteristic trc = AVCOL_TRC_BT709)
{
    AVFrame frame = {};
    frame.format = format;
    frame.width = 1920;
    frame.height = 1080;
    frame.color_range = range;
    frame.colorspace = space;
    frame.color_primaries = primaries;
    frame.color_trc = trc;
    return frame;
}

pl_frame mapAndResolve(const AVFrame& frame, int videoFormat)
{
    pl_frame mapped = {};
    pl_frame_from_avframe(&mapped, &frame);
    mapped.repr.levels = resolvePlVkColorLevels(
        videoFormat, frame.color_range, mapped.repr.levels);
    return mapped;
}

float decodeLumaToRgb(pl_color_repr repr, float y)
{
    pl_color_repr merged = repr;
    pl_color_repr_merge(&merged, &pl_color_repr_hdtv);
    if (!merged.bits.sample_depth) {
        merged.bits.sample_depth = merged.bits.color_depth;
    }
    const int depth = merged.bits.color_depth ? merged.bits.color_depth : 8;
    const float neutralChroma = float(1 << (depth - 1)) / float((1 << depth) - 1);
    pl_transform3x3 tr = pl_color_repr_decode(&merged, nullptr);
    float vec[3] = { y, neutralChroma, neutralChroma };
    pl_transform3x3_apply(&tr, vec);
    return vec[0];
}

void assertNear(float actual, float expected, float tolerance = 0.005f)
{
    assert(std::fabs(actual - expected) <= tolerance);
}

} // namespace

int main()
{
    // Limited-range H.264 / HEVC (8-bit SDR, 10-bit SDR, 10-bit HDR PQ) must
    // preserve PL_COLOR_LEVELS_LIMITED so black (16/255) maps to 0.0 and white
    // (235/255) maps to 1.0 instead of lifting blacks and crushing highlights.
    const int limitedCodecs[] = {
        VIDEO_FORMAT_H264,
        VIDEO_FORMAT_H265,
        VIDEO_FORMAT_H265_MAIN10,
    };
    for (int codec : limitedCodecs) {
        const bool is10Bit = (codec & VIDEO_FORMAT_MASK_10BIT) != 0;
        const AVPixelFormat fmt = is10Bit ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
        const float blackY = is10Bit ? (64.0f / 1023.0f) : (16.0f / 255.0f);
        const float whiteY = is10Bit ? (940.0f / 1023.0f) : (235.0f / 255.0f);
        AVFrame frame = makeFrame(fmt, AVCOL_RANGE_MPEG);
        pl_frame mapped = mapAndResolve(frame, codec);
        assert(mapped.repr.levels == PL_COLOR_LEVELS_LIMITED);
        assertNear(decodeLumaToRgb(mapped.repr, blackY), 0.0f);
        assertNear(decodeLumaToRgb(mapped.repr, whiteY), 1.0f);
    }

    // 10-bit BT.2020/PQ limited-range stream (macOS HDR host path).
    {
        AVFrame hdr = makeFrame(AV_PIX_FMT_P010, AVCOL_RANGE_MPEG,
                                AVCOL_SPC_BT2020_NCL, AVCOL_PRI_BT2020,
                                AVCOL_TRC_SMPTE2084);
        pl_frame mapped = mapAndResolve(hdr, VIDEO_FORMAT_H265_MAIN10);
        assert(mapped.repr.levels == PL_COLOR_LEVELS_LIMITED);
        assert(mapped.color.transfer == PL_COLOR_TRC_PQ);
        assert(mapped.color.primaries == PL_COLOR_PRIM_BT_2020);
        assertNear(decodeLumaToRgb(mapped.repr, 64.0f / 1023.0f), 0.0f);
        assertNear(decodeLumaToRgb(mapped.repr, 940.0f / 1023.0f), 1.0f);
    }

    // Full-range H.264 / HEVC streams (AVCOL_RANGE_JPEG) must stay full range.
    for (int codec : limitedCodecs) {
        const AVPixelFormat fmt = (codec & VIDEO_FORMAT_MASK_10BIT)
            ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
        AVFrame frame = makeFrame(fmt, AVCOL_RANGE_JPEG);
        pl_frame mapped = mapAndResolve(frame, codec);
        assert(mapped.repr.levels == PL_COLOR_LEVELS_FULL);
        assertNear(decodeLumaToRgb(mapped.repr, 0.0f), 0.0f);
        assertNear(decodeLumaToRgb(mapped.repr, 1.0f), 1.0f);
    }

    // Unspecified range on YCbCr H.264/HEVC stays unknown -> limited per spec,
    // while RGB frames keep full range.
    {
        AVFrame unspecifiedYuv = makeFrame(AV_PIX_FMT_NV12, AVCOL_RANGE_UNSPECIFIED);
        pl_frame mappedYuv = mapAndResolve(unspecifiedYuv, VIDEO_FORMAT_H265);
        assert(mappedYuv.repr.levels == PL_COLOR_LEVELS_UNKNOWN);
        assertNear(decodeLumaToRgb(mappedYuv.repr, 16.0f / 255.0f), 0.0f);
        assertNear(decodeLumaToRgb(mappedYuv.repr, 235.0f / 255.0f), 1.0f);

        AVFrame rgb = makeFrame(AV_PIX_FMT_RGB0, AVCOL_RANGE_UNSPECIFIED, AVCOL_SPC_RGB);
        pl_frame mappedRgb = mapAndResolve(rgb, VIDEO_FORMAT_H265);
        assert(mappedRgb.repr.levels == PL_COLOR_LEVELS_FULL);
    }

    // AV1 preserves the upstream AMF full-range workaround even when the
    // bitstream signals MPEG or unspecified range.
    const int av1Codecs[] = {
        VIDEO_FORMAT_AV1_MAIN8,
        VIDEO_FORMAT_AV1_MAIN10,
    };
    const AVColorRange ranges[] = {
        AVCOL_RANGE_MPEG,
        AVCOL_RANGE_UNSPECIFIED,
        AVCOL_RANGE_JPEG,
    };
    for (int codec : av1Codecs) {
        const AVPixelFormat fmt = (codec & VIDEO_FORMAT_MASK_10BIT)
            ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
        for (AVColorRange range : ranges) {
            AVFrame frame = makeFrame(fmt, range);
            pl_frame mapped = mapAndResolve(frame, codec);
            assert(mapped.repr.levels == PL_COLOR_LEVELS_FULL);
            assertNear(decodeLumaToRgb(mapped.repr, 0.0f), 0.0f);
            assertNear(decodeLumaToRgb(mapped.repr, 1.0f), 1.0f);
        }
    }

    puts("PASS: PlVkRenderer color levels preserve limited/full H.264/HEVC ranges and AV1 workaround");
    return 0;
}
