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
int readQp(VTCompressionSessionRef session, CFStringRef key) {
    CFTypeRef value = nullptr;
    assert(VTSessionCopyProperty(session, key, nullptr, &value) == noErr);
    assert(value && CFGetTypeID(value) == CFNumberGetTypeID());
    int qp = -1;
    assert(CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &qp));
    CFRelease(value);
    return qp;
}
void verifyRange(VTCompressionSessionRef session, int minimum, int maximum) {
    assert(readQp(session, kVTCompressionPropertyKey_MinAllowedFrameQP) == minimum);
    assert(readQp(session, kVTCompressionPropertyKey_MaxAllowedFrameQP) == maximum);
}
void checkRange(VTCompressionSessionRef session, int minimum, int maximum) {
    assert(deskport::vt::applyQualityRange(session, minimum, maximum));
    verifyRange(session, minimum, maximum);
}
double expectedFrameRate(VTCompressionSessionRef session) {
    CFTypeRef value = nullptr;
    assert(VTSessionCopyProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, nullptr, &value) == noErr);
    assert(value && CFGetTypeID(value) == CFNumberGetTypeID());
    double fps = 0;
    assert(CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &fps));
    CFRelease(value);
    return fps;
}
}

int main() {
    for (auto codec : {kCMVideoCodecType_H264, kCMVideoCodecType_HEVC}) {
        VTCompressionSessionRef session = nullptr;
        assert(VTCompressionSessionCreate(nullptr, 640, 360, codec, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, &session) == noErr);
        assert(session);
        for (const double fps : {8.0, 60.0, 8.0}) {
            assert(deskport::vt::applyExpectedFrameRate(session, fps));
            assert(expectedFrameRate(session) == fps);
        }
        assert(!deskport::vt::applyExpectedFrameRate(nullptr, 8.0));
        for (const double invalid : {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(),
                                     -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            assert(!deskport::vt::applyExpectedFrameRate(session, invalid));
            assert(expectedFrameRate(session) == 8.0);
        }
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
        // Raising the minimum past the previous maximum must first relax it;
        // returning from congestion must restore the saver's complete range.
        checkRange(session, 0, 51);
        checkRange(session, 22, 26);
        checkRange(session, 32, 51);
        checkRange(session, 22, 26);
        checkRange(session, 0, 51);
        assert(!deskport::vt::applyQualityRange(nullptr, 22, 26));
        assert(!deskport::vt::applyQualityRange(session, -1, 26));
        assert(!deskport::vt::applyQualityRange(session, 22, 52));
        assert(!deskport::vt::applyQualityRange(session, 32, 26));
        assert(!deskport::vt::applyQualityRange(session, 0, -1));
        verifyRange(session, 0, 51);
        verify(session, 500000, 3); // QP range changes do not alter rate limits.
        assert(expectedFrameRate(session) == 8.0); // rate/QP do not reset cadence
        check(session, 500000, 1);
        VTCompressionSessionInvalidate(session);
        CFRelease(session);
        // A successful property write alone does not prove frames are emitted:
        // the separate frame replay must also account for every dropped input.
        std::printf("PASS: VT expected FPS 8/60/8, rate properties and QP range transitions: %s (frame delivery not exercised)\n",
                    codec == kCMVideoCodecType_H264 ? "H.264" : "HEVC");
    }
}
