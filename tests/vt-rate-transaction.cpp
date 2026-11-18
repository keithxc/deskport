// Fault injection around the production VideoToolbox property transaction.
// This checks rollback only; tests/vt-live-rate.cpp uses actual hardware.
#include <VideoToolbox/VideoToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cassert>
#include <map>

static std::map<CFStringRef, CFTypeRef> properties;
static int calls = 0, failAt = 0;
static bool failCopy = false;
static OSStatus copyProperty(VTSessionRef, CFStringRef key, CFAllocatorRef, void* out) {
    // Reproduce hardware that accepts limits but cannot read them back.
    if (key == kVTCompressionPropertyKey_DataRateLimits || failCopy) return -12900;
    *static_cast<CFTypeRef*>(out) = CFRetain(properties.at(key));
    return noErr;
}
static OSStatus setProperty(VTSessionRef, CFStringRef key, CFTypeRef value) {
    if (++calls == failAt) return -12900;
    if (properties[key]) CFRelease(properties[key]);
    properties[key] = value ? CFRetain(value) : nullptr;
    return noErr;
}
#define VTSessionCopyProperty copyProperty
#define VTSessionSetProperty setProperty
#include "host/macos/vtbitrate.h"
#undef VTSessionCopyProperty
#undef VTSessionSetProperty

static std::int64_t number(CFTypeRef value) {
    std::int64_t n = -1;
    assert(CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &n));
    return n;
}
int main() {
    auto session = reinterpret_cast<VTCompressionSessionRef>(1);
    // A real setup establishes the original one-second FFmpeg limit and QP.
    assert(deskport::vt::applyRate(session, 15000000));
    assert(deskport::vt::applyQualityFloor(session, 0));
    assert(deskport::vt::applyQualityCeiling(session, 51));
    assert(deskport::vt::applyExpectedFrameRate(session, 60));
    for (int stage = 1; stage <= 6; ++stage) {
        calls = 0; failAt = stage;
        assert(!deskport::vt::apply(session, 3000000, 32, 15000000, 3, 40, 8));
        assert(number(properties.at(kVTCompressionPropertyKey_AverageBitRate)) == 15000000);
        assert(number(properties.at(kVTCompressionPropertyKey_MinAllowedFrameQP)) == 0);
        assert(number(properties.at(kVTCompressionPropertyKey_MaxAllowedFrameQP)) == 51);
        assert(number(properties.at(kVTCompressionPropertyKey_ExpectedFrameRate)) == 60);
        auto limits = static_cast<CFArrayRef>(properties.at(kVTCompressionPropertyKey_DataRateLimits));
        assert(number(CFArrayGetValueAtIndex(limits, 0)) == 1875000);
        assert(number(CFArrayGetValueAtIndex(limits, 1)) == 1);
    }
    calls = failAt = 0; failCopy = true;
    assert(!deskport::vt::apply(session, 3000000, 32, 15000000) && calls == 0);
    failCopy = false;
    assert(!deskport::vt::apply(session, 3000000, 32, 0) && calls == 0);
    assert(deskport::vt::apply(session, 3000000, 32, 15000000));
    assert(calls == 3); // Ordinary mode must not require saver-only MaxQP/cadence writes.
    assert(number(properties.at(kVTCompressionPropertyKey_AverageBitRate)) == 3000000);
    assert(number(properties.at(kVTCompressionPropertyKey_MinAllowedFrameQP)) == 32);
    assert(deskport::vt::apply(session, 15000000, 0, 3000000));
    for (auto [key, value] : properties) if (value) CFRelease(value);
}
