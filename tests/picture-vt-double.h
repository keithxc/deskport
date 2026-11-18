// Narrow C API double for the real host/macos/vtbitrate.h adapter.
// Models property ownership, independent failures and FFmpeg's private prefix;
// it does not model a native encoder or prove hardware acceptance.
#pragma once
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

struct CFValue {
    mutable int references = 1;
    double number = 0;
    std::vector<const CFValue*> elements;
};
using CFTypeRef = const CFValue*;
using CFNumberRef = CFTypeRef;
using CFArrayRef = CFTypeRef;
using CFStringRef = int;
using OSStatus = int;
constexpr int noErr = 0, kCFNumberDoubleType = 1, kCFNumberSInt64Type = 2, kCFNumberIntType = 3;
constexpr int kVTCompressionPropertyKey_AverageBitRate = 1, kVTCompressionPropertyKey_DataRateLimits = 2,
    kVTCompressionPropertyKey_MinAllowedFrameQP = 3, kVTCompressionPropertyKey_MaxAllowedFrameQP = 4,
    kVTCompressionPropertyKey_ExpectedFrameRate = 5, kCFTypeArrayCallBacks = 0;
inline int liveCFValues = 0;
inline CFTypeRef CFRetain(CFTypeRef value) { if (value) ++value->references; return value; }
inline void CFRelease(CFTypeRef value) {
    if (value && !--value->references) {
        for (auto element : value->elements) CFRelease(element);
        delete value;
        --liveCFValues;
    }
}
inline CFNumberRef CFNumberCreate(std::nullptr_t, int type, const void* input) {
    auto value = new CFValue;
    ++liveCFValues;
    if (type == kCFNumberDoubleType) value->number = *static_cast<const double*>(input);
    else if (type == kCFNumberSInt64Type) value->number = double(*static_cast<const std::int64_t*>(input));
    else { assert(type == kCFNumberIntType); value->number = *static_cast<const int*>(input); }
    return value;
}
inline CFArrayRef CFArrayCreate(std::nullptr_t, const void** values, int size, const int*) {
    auto array = new CFValue;
    ++liveCFValues;
    for (int i = 0; i < size; ++i)
        array->elements.push_back(CFRetain(static_cast<CFTypeRef>(values[i])));
    return array;
}

using Writes = std::vector<std::pair<char, int>>;
struct Rational { int num = 60, den = 1; };
inline double av_q2d(Rational value) { return double(value.num) / value.den; }
struct AVClass {};
enum AVCodecID { AV_CODEC_ID_H264 };
struct AVCodec { const char* name = "h264_videotoolbox"; };
struct Context;
using VTCompressionSessionRef = Context*;
struct Context {
    int min = 0, max = 51, fail = -1, rateCalls = 0, width = 2040, height = 1228;
    int propertyCalls = 0, failAt = 0, copyFailKey = 0;
    bool expectedAccepted = true, limitsReadable = true, validSessionType = true;
    double expected = 30, limitSeconds = 1;
    std::int64_t bit_rate = 15000000, rc_max_rate = 15000000;
    std::int64_t average = bit_rate, limitBytes = rc_max_rate / 8;
    Rational framerate;
    Writes writes;
    std::vector<char> operations;
    AVCodec encoder;
    const AVCodec* codec = &encoder;
    AVCodecID codec_id = AV_CODEC_ID_H264;
    // Same prefix layout as FFmpeg, read by the production adapter.
    struct Prefix { const AVClass* av_class; AVCodecID codec_id; Context* session; } prefix {nullptr, codec_id, this};
    void* priv_data = &prefix;
};
using AVCodecContext = Context;
inline int VTCompressionSessionGetTypeID() { return 7; }
inline int CFGetTypeID(Context* context) { return context->validSessionType ? 7 : 0; }
inline OSStatus VTSessionCopyProperty(Context* ctx, CFStringRef key, std::nullptr_t, CFTypeRef* value) {
    if (ctx->copyFailKey == key || (key == kVTCompressionPropertyKey_DataRateLimits && !ctx->limitsReadable)) return -1;
    if (key == kVTCompressionPropertyKey_DataRateLimits) {
        auto bytes = CFNumberCreate(nullptr, kCFNumberSInt64Type, &ctx->limitBytes);
        auto seconds = CFNumberCreate(nullptr, kCFNumberDoubleType, &ctx->limitSeconds);
        CFNumberRef elements[] = {bytes, seconds};
        *value = CFArrayCreate(nullptr, reinterpret_cast<const void**>(elements), 2, &kCFTypeArrayCallBacks);
        CFRelease(bytes); CFRelease(seconds);
    } else if (key == kVTCompressionPropertyKey_AverageBitRate)
        *value = CFNumberCreate(nullptr, kCFNumberSInt64Type, &ctx->average);
    else if (key == kVTCompressionPropertyKey_MinAllowedFrameQP)
        *value = CFNumberCreate(nullptr, kCFNumberIntType, &ctx->min);
    else if (key == kVTCompressionPropertyKey_MaxAllowedFrameQP)
        *value = CFNumberCreate(nullptr, kCFNumberIntType, &ctx->max);
    else { assert(key == kVTCompressionPropertyKey_ExpectedFrameRate); *value = CFNumberCreate(nullptr, kCFNumberDoubleType, &ctx->expected); }
    return noErr;
}
inline OSStatus VTSessionSetProperty(Context* ctx, CFStringRef key, CFTypeRef value) {
    assert(value); // This fixture starts with all selected properties configured.
    ++ctx->propertyCalls;
    if (key == kVTCompressionPropertyKey_ExpectedFrameRate) ctx->operations.push_back('F');
    if (key == kVTCompressionPropertyKey_AverageBitRate) { ++ctx->rateCalls; ctx->operations.push_back('R'); }
    if (key == kVTCompressionPropertyKey_DataRateLimits) ctx->operations.push_back('L');
    if (key == kVTCompressionPropertyKey_MinAllowedFrameQP) ctx->writes.push_back({'m', int(value->number)});
    if (key == kVTCompressionPropertyKey_MaxAllowedFrameQP) ctx->writes.push_back({'M', int(value->number)});
    if (ctx->propertyCalls == ctx->failAt) return -1;
    switch (key) {
    case kVTCompressionPropertyKey_AverageBitRate: ctx->average = std::int64_t(value->number); break;
    case kVTCompressionPropertyKey_DataRateLimits:
        assert(value->elements.size() == 2);
        ctx->limitBytes = std::int64_t(value->elements[0]->number);
        ctx->limitSeconds = value->elements[1]->number;
        break;
    case kVTCompressionPropertyKey_MinAllowedFrameQP:
        if (value->number > ctx->max) return -1;
        ctx->min = int(value->number); break;
    case kVTCompressionPropertyKey_MaxAllowedFrameQP:
        if (value->number == ctx->fail || value->number < ctx->min) return -1;
        ctx->max = int(value->number); break;
    case kVTCompressionPropertyKey_ExpectedFrameRate:
        if (!ctx->expectedAccepted) return -1;
        ctx->expected = value->number; break;
    default: assert(false);
    }
    return noErr;
}
