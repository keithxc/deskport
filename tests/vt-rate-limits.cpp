// Synthetic VideoToolbox property checks; no screen capture or input.
#include "host/macos/vtbitrate.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {
void verify(VTCompressionSessionRef session, std::int64_t bps, int burst) {
    CFTypeRef average = nullptr, limits = nullptr;
    assert(VTSessionCopyProperty(session, kVTCompressionPropertyKey_AverageBitRate, nullptr, &average) == noErr);
    assert(VTSessionCopyProperty(session, kVTCompressionPropertyKey_DataRateLimits, nullptr, &limits) == noErr);
    assert(average && CFGetTypeID(average) == CFNumberGetTypeID());
    std::int64_t actualBps = 0;
    assert(CFNumberGetValue(static_cast<CFNumberRef>(average), kCFNumberSInt64Type, &actualBps));
    assert(actualBps == bps);
    assert(limits && CFGetTypeID(limits) == CFArrayGetTypeID());
    auto array = static_cast<CFArrayRef>(limits);
    assert(CFArrayGetCount(array) == 2);
    const auto bytes = static_cast<CFNumberRef>(CFArrayGetValueAtIndex(array, 0));
    const auto seconds = static_cast<CFNumberRef>(CFArrayGetValueAtIndex(array, 1));
    assert(CFGetTypeID(bytes) == CFNumberGetTypeID() && CFGetTypeID(seconds) == CFNumberGetTypeID());
    std::int64_t actualBytes = 0;
    double actualSeconds = 0;
    assert(CFNumberGetValue(bytes, kCFNumberSInt64Type, &actualBytes));
    assert(CFNumberGetValue(seconds, kCFNumberDoubleType, &actualSeconds));
    assert(actualBytes == bps * burst / 8 && actualSeconds == 1.0);
    CFRelease(average);
    CFRelease(limits);
}
void check(VTCompressionSessionRef session, std::int64_t bps, int burst) {
    assert(deskport::vt::applyRate(session, bps, burst));
    verify(session, bps, burst);
}
}

int main() {
    for (auto codec : {kCMVideoCodecType_H264, kCMVideoCodecType_HEVC}) {
        VTCompressionSessionRef session = nullptr;
        assert(VTCompressionSessionCreate(nullptr, 640, 360, codec, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, &session) == noErr);
        assert(session);
        // Restore the original one-second limit without altering the average.
        check(session, 1000000, 1);
        check(session, 1000000, 3);
        check(session, 1000000, 1);
        // A low manual bitrate remains the average target in saver mode too.
        check(session, 500000, 3);
        assert(!deskport::vt::applyRate(nullptr, 1000000, 3));
        assert(!deskport::vt::applyRate(session, 0, 3));
        assert(!deskport::vt::applyRate(session, -1, 3));
        assert(!deskport::vt::applyRate(session, 1000000, 0));
        assert(!deskport::vt::applyRate(session, 1000000, -1));
        assert(!deskport::vt::applyRate(session, std::numeric_limits<std::int64_t>::max(), 3));
        // Rejected inputs leave the last valid settings intact.
        verify(session, 500000, 3);
        check(session, 500000, 1);
        VTCompressionSessionInvalidate(session);
        CFRelease(session);
        std::printf("PASS: VT average and one-second burst limits: %s\n",
                    codec == kCMVideoCodecType_H264 ? "H.264" : "HEVC");
    }
}
