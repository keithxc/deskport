// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <VideoToolbox/VideoToolbox.h>
#include <cstdint>
#include <cstring>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace deskport::vt {
// FFmpeg keeps its VTCompressionSessionRef private. The pinned encoder
// (FFmpeg bf1b838f, libavcodec/videotoolboxenc.c) starts VTEncContext with
// {AVClass *, enum AVCodecID, VTCompressionSessionRef}. Only read this prefix
// for FFmpeg's own VideoToolbox encoders and verify the CoreFoundation type
// before use; anything else is treated as "live rate control unsupported".
struct EncoderPrefix {
    const AVClass* av_class;
    enum AVCodecID codec_id;
    VTCompressionSessionRef session;
};

inline VTCompressionSessionRef session(AVCodecContext* ctx) {
    if (!ctx || !ctx->codec || !ctx->priv_data || !ctx->codec->name) return nullptr;
    const char* name = ctx->codec->name;
    if (std::strcmp(name, "h264_videotoolbox") && std::strcmp(name, "hevc_videotoolbox")) return nullptr;
    auto* prefix = static_cast<EncoderPrefix*>(ctx->priv_data);
    if (prefix->codec_id != ctx->codec_id || !prefix->session) return nullptr;
    if (CFGetTypeID(prefix->session) != VTCompressionSessionGetTypeID()) return nullptr;
    return prefix->session;
}

// Average bitrate with FFmpeg's original one-second hard limit. Shorter
// DataRateLimits windows were measured not to bound scene-change frames.
// A burst factor above 1 lets one second carry that multiple of the average,
// giving a large change more room without changing the average-rate target.
// AverageBitRate is an encoder target, not a strict traffic/debt guarantee.
inline bool applyRate(VTCompressionSessionRef s, std::int64_t bps, int burst = 1) {
    if (!s || bps <= 0 || burst < 1 || bps > std::numeric_limits<std::int64_t>::max() / burst) return false;
    const std::int64_t perSecond = bps * burst / 8;
    const double one = 1.0;
    CFNumberRef average = CFNumberCreate(nullptr, kCFNumberSInt64Type, &bps);
    CFNumberRef values[] = {
        CFNumberCreate(nullptr, kCFNumberSInt64Type, &perSecond),
        CFNumberCreate(nullptr, kCFNumberDoubleType, &one),
    };
    CFArrayRef limits = CFArrayCreate(nullptr, reinterpret_cast<const void**>(values), 2, &kCFTypeArrayCallBacks);
    const OSStatus a = VTSessionSetProperty(s, kVTCompressionPropertyKey_AverageBitRate, average);
    const OSStatus l = VTSessionSetProperty(s, kVTCompressionPropertyKey_DataRateLimits, limits);
    CFRelease(limits);
    for (auto* value : values) CFRelease(value);
    CFRelease(average);
    return a == noErr && l == noErr;
}

// Raise the minimum frame QP while a slow link is congested; 0 restores the
// encoder's own choice. This bounds large content-change frames at the cost of
// fine detail, without changing the negotiated resolution.
inline bool applyQualityFloor(VTCompressionSessionRef s, int minQp) {
    if (!s || minQp < 0 || minQp > 51) return false;
    CFNumberRef value = CFNumberCreate(nullptr, kCFNumberIntType, &minQp);
    const OSStatus status = VTSessionSetProperty(s, kVTCompressionPropertyKey_MinAllowedFrameQP, value);
    CFRelease(value);
    return status == noErr;
}
}
