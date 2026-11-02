// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <cstdio>

namespace deskport::vt {
inline thread_local bool completeSubmittedFrame = false;
inline thread_local OSStatus completionError = noErr;
// Only the host's data-saver encode call opts in; restore an outer scope on
// every return path. Other native VideoToolbox submissions forward unchanged.
class ScopedFrameCompletion {
public:
    explicit ScopedFrameCompletion(bool enabled) : previous(completeSubmittedFrame), previousError(completionError) {
        completeSubmittedFrame = enabled;
        completionError = noErr;
    }
    ~ScopedFrameCompletion() { completeSubmittedFrame = previous; completionError = previousError; }
    OSStatus status() const { return completionError; }
    ScopedFrameCompletion(const ScopedFrameCompletion&) = delete;
    ScopedFrameCompletion& operator=(const ScopedFrameCompletion&) = delete;
private:
    bool previous;
    OSStatus previousError;
};
}

// The host links the pinned libavcodec statically. Its VideoToolbox encoder
// drains the native callback queue only inside the encode callback, so polling
// avcodec_receive_packet after EAGAIN cannot deliver the final picture. Forward
// this public native API, then complete this PTS before FFmpeg checks its queue.
// Unlike flushing FFmpeg, native CompleteFrames permits subsequent submissions.
extern "C" OSStatus VTCompressionSessionEncodeFrame(
    VTCompressionSessionRef session, CVImageBufferRef imageBuffer,
    CMTime presentationTimeStamp, CMTime duration, CFDictionaryRef frameProperties,
    void* sourceFrameRefcon, VTEncodeInfoFlags* infoFlagsOut) {
    using Encode = decltype(&VTCompressionSessionEncodeFrame);
    static const Encode encode = reinterpret_cast<Encode>(dlsym(RTLD_NEXT, "VTCompressionSessionEncodeFrame"));
    if (!encode) {
        std::fputs("DeskPort: could not resolve native VideoToolbox frame submission\n", stderr);
        return kVTInvalidSessionErr;
    }
    const auto status = encode(session, imageBuffer, presentationTimeStamp, duration,
                               frameProperties, sourceFrameRefcon, infoFlagsOut);
    if (status != noErr || !deskport::vt::completeSubmittedFrame) return status;
    const auto completion = VTCompressionSessionCompleteFrames(session, presentationTimeStamp);
    if (completion != noErr) deskport::vt::completionError = completion;
    // A successful submission transfers sourceFrameRefcon to the callback.
    // Reporting a later completion error as submission failure makes FFmpeg
    // free callback-owned state. The host checks scoped status outside FFmpeg.
    return status;
}
