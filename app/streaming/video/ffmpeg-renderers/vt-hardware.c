// SPDX-License-Identifier: GPL-3.0-or-later
#include "vt-hardware.h"
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <stdatomic.h>
#include <string.h>

struct nal_span { const uint8_t *bytes; size_t size; };
static size_t next_start(const uint8_t *bytes, size_t size, size_t begin, size_t *prefix) {
    for (size_t i = begin; i + 3 <= size; ++i) {
        if (bytes[i] || bytes[i+1]) continue;
        if (bytes[i+2] == 1) { *prefix = 3; return i; }
        if (i + 4 <= size && !bytes[i+2] && bytes[i+3] == 1) { *prefix = 4; return i; }
    }
    return size;
}
struct probe_state {
    atomic_bool decoded;
    VTDecompressionOutputCallback observer;
    void *observer_context;
};
static void probe_output(void *context, void *source, OSStatus status, VTDecodeInfoFlags flags,
                         CVImageBufferRef image, CMTime pts, CMTime duration) {
    (void)source; (void)flags; (void)pts; (void)duration;
    struct probe_state *state = context;
    if (status == noErr && image) atomic_store(&state->decoded, true);
    if (state->observer) state->observer(state->observer_context, source, status, flags, image, pts, duration);
}

static bool decode_hardware_image(const uint8_t *bytes, size_t size, OSType output_type,
                                  VTDecompressionOutputCallback observer, void *observer_context) {
    if (!bytes || size < 6 || size > 8 * 1024 * 1024) return false;
    struct nal_span nals[128];
    const uint8_t *sets[3] = {NULL, NULL, NULL};
    size_t set_sizes[3] = {0, 0, 0}, count = 0, total = 0, prefix = 0;
    size_t start = next_start(bytes, size, 0, &prefix);
    while (start < size) {
        size_t begin = start + prefix, next_prefix = 0;
        size_t end = next_start(bytes, size, begin, &next_prefix);
        if (end - begin < 2 || count == 128) return false;
        nals[count++] = (struct nal_span){bytes + begin, end - begin};
        total += 4 + end - begin;
        unsigned type = (bytes[begin] >> 1) & 0x3f;
        if (type >= 32 && type <= 34 && !sets[type-32]) {
            sets[type-32] = bytes + begin;
            set_sizes[type-32] = end - begin;
        }
        start = end; prefix = next_prefix;
    }
    if (!count || !sets[0] || !sets[1] || !sets[2]) return false;
    CMVideoFormatDescriptionRef format = NULL;
    CMBlockBufferRef block = NULL;
    CMSampleBufferRef sample = NULL;
    VTDecompressionSessionRef session = NULL;
    CFDictionaryRef specification = NULL, attributes = NULL;
    CFNumberRef pixel_type = NULL;
    CFTypeRef hardware = NULL;
    struct probe_state state = {false, observer, observer_context};
    bool result = false;
    OSStatus status = CMVideoFormatDescriptionCreateFromHEVCParameterSets(NULL, 3, sets, set_sizes, 4, NULL, &format);
    if (status != noErr) goto done;
    const void *keys[] = {kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder};
    const void *values[] = {kCFBooleanTrue};
    specification = CFDictionaryCreate(NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!specification) goto done;
    if (output_type) {
        pixel_type = CFNumberCreate(NULL, kCFNumberSInt32Type, &output_type);
        if (!pixel_type) goto done;
        const void *attribute_keys[] = {kCVPixelBufferPixelFormatTypeKey};
        const void *attribute_values[] = {pixel_type};
        attributes = CFDictionaryCreate(NULL, attribute_keys, attribute_values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }
    if (output_type && !attributes) goto done;
    VTDecompressionOutputCallbackRecord callback = {probe_output, &state};
    status = VTDecompressionSessionCreate(NULL, format, specification, attributes, &callback, &session);
    if (status != noErr) goto done;
    status = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, total, NULL, NULL, 0, total,
                                               kCMBlockBufferAssureMemoryNowFlag, &block);
    if (status != noErr) goto done;
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t length = (uint32_t)nals[i].size;
        uint8_t header[] = {length >> 24, length >> 16, length >> 8, length};
        if (CMBlockBufferReplaceDataBytes(header, block, offset, 4) != noErr ||
            CMBlockBufferReplaceDataBytes(nals[i].bytes, block, offset + 4, length) != noErr) goto done;
        offset += 4 + length;
    }
    CMSampleTimingInfo timing = {CMTimeMake(1, 60), kCMTimeZero, kCMTimeInvalid};
    status = CMSampleBufferCreateReady(NULL, block, format, 1, 1, &timing, 1, &total, &sample);
    if (status != noErr) goto done;
    status = VTDecompressionSessionDecodeFrame(session, sample, 0, NULL, NULL);
    if (status != noErr) goto done;
    if (VTDecompressionSessionFinishDelayedFrames(session) != noErr ||
        VTDecompressionSessionWaitForAsynchronousFrames(session) != noErr) goto done;
    status = VTSessionCopyProperty(session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hardware);
    result = status == noErr && hardware == kCFBooleanTrue && atomic_load(&state.decoded);
done:
    if (session) { VTDecompressionSessionInvalidate(session); CFRelease(session); }
    if (hardware) CFRelease(hardware);
    if (attributes) CFRelease(attributes);
    if (pixel_type) CFRelease(pixel_type);
    if (sample) CFRelease(sample);
    if (block) CFRelease(block);
    if (specification) CFRelease(specification);
    if (format) CFRelease(format);
    return result;
}

bool dp_vt_hevc_hardware_probe(const uint8_t *bytes, size_t size) {
    return decode_hardware_image(bytes, size, 0, NULL, NULL);
}
