// Nasty hack to avoid conflict between AVFoundation and
// libavutil both defining AVMediaType
#define AVMediaType AVMediaType_FFmpeg
#include "vt.h"
#undef AVMediaType

#import <Cocoa/Cocoa.h>
#import <VideoToolbox/VideoToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>

bool VTBaseRenderer::checkDecoderCapabilities(id<MTLDevice> /*device*/, PDECODER_PARAMETERS params) {
    if (params->videoFormat & VIDEO_FORMAT_MASK_H264) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_H264)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated H.264 decode via VT");
            return false;
        }
    }
    else if (params->videoFormat & VIDEO_FORMAT_MASK_H265) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated HEVC decode via VT");
            return false;
        }

        // The exact profile is qualified with a real hardware-only VT session
        // using the same synthetic bitstream in completeInitialization().
    }
    else if (params->videoFormat & VIDEO_FORMAT_MASK_AV1) {
        if (!VTIsHardwareDecodeSupported(kCMVideoCodecType_AV1)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No HW accelerated AV1 decode via VT");
            return false;
        }

        // 10-bit is part of the Main profile for AV1, so it will always
        // be present on hardware that supports 8-bit.
    }

    return true;
}
