// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic samples only: no screen permission, display enumeration or capture.
#import "host/macos/screen-video.m"
#include <assert.h>

static CMSampleBufferRef sample(SCFrameStatus status) {
    CVPixelBufferRef pixels = NULL;
    assert(CVPixelBufferCreate(NULL, 64, 64, kCVPixelFormatType_32BGRA, NULL, &pixels) == kCVReturnSuccess);
    CMVideoFormatDescriptionRef format = NULL;
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &format) == noErr);
    CMSampleTimingInfo timing = {CMTimeMake(1,60), kCMTimeZero, kCMTimeInvalid};
    CMSampleBufferRef result = NULL;
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, format, &timing, &result) == noErr);
    NSMutableArray *attachments = (__bridge NSMutableArray *)CMSampleBufferGetSampleAttachmentsArray(result, true);
    attachments[0][SCStreamFrameInfoStatus] = @(status);
    CFRelease(format);
    CFRelease(pixels);
    return result;
}
static DPCapture *session(dispatch_queue_t queue, FrameCallbackBlock callback) {
    DPCapture *capture = [[DPCapture alloc] init];
    capture.queue = queue;
    capture.signal = dispatch_semaphore_create(0);
    capture.callback = callback;
    return capture;
}
static void testConversion(BOOL hdr, BOOL p3) {
    CVPixelBufferRef pixels = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    assert(CVPixelBufferCreate(NULL, 64, 64, kCVPixelFormatType_444YpCbCr10BiPlanarFullRange,
        (__bridge CFDictionaryRef)attributes, &pixels) == kCVReturnSuccess);
    CVBufferSetAttachment(pixels, kCVImageBufferColorPrimariesKey, hdr ? (p3 ? kCVImageBufferColorPrimaries_P3_D65 : kCVImageBufferColorPrimaries_ITU_R_2020) : kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferTransferFunctionKey, hdr ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ : kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferYCbCrMatrixKey, hdr && !p3 ? kCVImageBufferYCbCrMatrix_ITU_R_2020 : kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVPixelBufferLockBaseAddress(pixels, 0);
    for (int y = 0; y < 64; ++y) {
        uint16_t *luma = (uint16_t *)((char *)CVPixelBufferGetBaseAddressOfPlane(pixels, 0) + y * CVPixelBufferGetBytesPerRowOfPlane(pixels, 0));
        uint16_t *uv = (uint16_t *)((char *)CVPixelBufferGetBaseAddressOfPlane(pixels, 1) + y * CVPixelBufferGetBytesPerRowOfPlane(pixels, 1));
        for (int x = 0; x < 64; ++x) {
            luma[x] = (x * 16) << 6;
            uv[2*x] = ((p3 && y >= 32) ? 650 : 512) << 6;
            uv[2*x+1] = ((p3 && y >= 32) ? 400 : 512) << 6;
        }
    }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    CGColorSpaceRef sourceSpace = CGColorSpaceCreateWithName(p3 ? kCGColorSpaceDisplayP3_PQ : hdr ? kCGColorSpaceITUR_2100_PQ : kCGColorSpaceITUR_709);
    CVBufferSetAttachment(pixels, kCVImageBufferCGColorSpaceKey, sourceSpace, kCVAttachmentMode_ShouldPropagate);
    CFRelease(sourceSpace);
    CVBufferSetAttachment(pixels, CFSTR("DeskPortSyntheticTag"), CFSTR("synthetic"), kCVAttachmentMode_ShouldPropagate);
    CMVideoFormatDescriptionRef description = NULL;
    CMSampleBufferRef input = NULL;
    CMSampleTimingInfo timing = {CMTimeMake(1,60), CMTimeMake(7,60), kCMTimeInvalid};
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &description) == noErr);
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, description, &timing, &input) == noErr);
    DPCapture *capture = [DPCapture new];
    capture.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    capture.hdr = hdr;
    CMSampleBufferRef eightBit = sample(SCFrameStatusComplete);
    assert([capture convertSample:eightBit] == NULL); // No padded 8-bit HDR.
    CFRelease(eightBit);
    CMSampleBufferRef output = [capture convertSample:input];
    assert(output);
    assert(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(output), timing.presentationTimeStamp) == 0);
    CVPixelBufferRef converted = CMSampleBufferGetImageBuffer(output);
    assert(CVPixelBufferGetPixelFormatType(converted) == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
    assert(CVPixelBufferGetWidth(converted) == 64);
    CFTypeRef tag = CVBufferCopyAttachment(converted, CFSTR("DeskPortSyntheticTag"), NULL);
    assert(tag && CFEqual(tag, CFSTR("synthetic")));
    CFRelease(tag);
    CFTypeRef outputSpace = CVBufferCopyAttachment(converted, kCVImageBufferCGColorSpaceKey, NULL);
    CGColorSpaceRef expectedSpace = CGColorSpaceCreateWithName(hdr ? kCGColorSpaceITUR_2100_PQ : kCGColorSpaceITUR_709);
    assert(outputSpace && CFEqual(outputSpace, expectedSpace));
    CFRelease(outputSpace); CFRelease(expectedSpace);
    CVPixelBufferLockBaseAddress(converted, kCVPixelBufferLock_ReadOnly);
    uint16_t *luma = CVPixelBufferGetBaseAddressOfPlane(converted, 0);
    for (int x = 2; x < 62; ++x) {
        int expected = 64 + (x * 16 * 876 + 511) / 1023;
        assert(abs((luma[x] >> 6) - expected) <= 4); // Native range conversion rounds within one 8-bit code.
    }
    unsigned fractional = 0;
    for (int x = 2; x < 62; ++x) fractional += ((luma[x] >> 6) & 3) != 0;
    assert(fractional > 20); // Not an 8-bit surface padded/tagged as 10-bit.
    if (p3) {
        uint16_t *colorY = (uint16_t *)((char *)CVPixelBufferGetBaseAddressOfPlane(converted, 0) + 63 * CVPixelBufferGetBytesPerRowOfPlane(converted, 0));
        uint16_t *colorUV = (uint16_t *)((char *)CVPixelBufferGetBaseAddressOfPlane(converted, 1) + 31 * CVPixelBufferGetBytesPerRowOfPlane(converted, 1));
        int identityY = 64 + (512 * 876 + 511) / 1023;
        int identityU = 512 + (138 * 896 + 511) / 1023;
        int identityV = 512 - (112 * 896 + 511) / 1023;
        // Saturated P3/709-matrix pixels must actually change in the BT.2020
        // target, beyond the full/video-range conversion. Retagging fails.
        assert(abs((colorY[32] >> 6) - identityY) > 4 ||
               abs((colorUV[32] >> 6) - identityU) > 4 ||
               abs((colorUV[33] >> 6) - identityV) > 4);
    }
    CVPixelBufferUnlockBaseAddress(converted, kCVPixelBufferLock_ReadOnly);
    CFTypeRef transfer = CVBufferCopyAttachment(converted, kCVImageBufferTransferFunctionKey, NULL);
    assert(transfer && CFEqual(transfer, hdr ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ : kCVImageBufferTransferFunction_ITU_R_709_2));
    CFRelease(transfer);
    // Native format-description colorimetry can fill a missing image attachment.
    CVBufferRemoveAttachment(pixels, kCVImageBufferTransferFunctionKey);
    CMSampleBufferRef fromFormat = [capture convertSample:input];
    assert(fromFormat); CFRelease(fromFormat);
    // A conflicting image transfer function must never be hidden by that fallback.
    CVBufferSetAttachment(pixels, kCVImageBufferTransferFunctionKey, hdr ? kCVImageBufferTransferFunction_ITU_R_709_2 : kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ, kCVAttachmentMode_ShouldPropagate);
    assert([capture convertSample:input] == NULL);
    CFRelease(output); CFRelease(input); CFRelease(description); CFRelease(pixels);
    printf("PASS: native %s xf44->P010, 10-bit ramp precision, range, timing, color tags, missing-tag rejection\n", hdr ? (p3 ? "P3/PQ to BT2020/PQ" : "BT2020/PQ") : "BT709/SDR");
}
static void testRGBConversion(BOOL hdr) {
    CVPixelBufferRef pixels = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    assert(CVPixelBufferCreate(NULL, 64, 64, kCVPixelFormatType_64RGBAHalf, (__bridge CFDictionaryRef)attributes, &pixels) == kCVReturnSuccess);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(hdr ? kCGColorSpaceDisplayP3_PQ : kCGColorSpaceSRGB);
    CVBufferSetAttachment(pixels, kCVImageBufferCGColorSpaceKey, space, kCVAttachmentMode_ShouldPropagate);
    CFRelease(space);
    CVPixelBufferLockBaseAddress(pixels, 0);
    for (int y = 0; y < 64; ++y) {
        __fp16 *row = (__fp16 *)((char *)CVPixelBufferGetBaseAddress(pixels) + y * CVPixelBufferGetBytesPerRow(pixels));
        for (int x = 0; x < 64; ++x) { row[4*x] = row[4*x+1] = row[4*x+2] = (__fp16)(x/63.0); row[4*x+3] = 1; }
    }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    CMVideoFormatDescriptionRef description = NULL; CMSampleBufferRef input = NULL;
    CMSampleTimingInfo timing = {CMTimeMake(1,60), kCMTimeZero, kCMTimeInvalid};
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &description) == noErr);
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, description, &timing, &input) == noErr);
    DPCapture *capture = [DPCapture new]; capture.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange; capture.hdr = hdr;
    CMSampleBufferRef output = [capture convertSample:input]; assert(output);
    CVPixelBufferRef result = CMSampleBufferGetImageBuffer(output);
    assert(CVPixelBufferGetPixelFormatType(result) == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
    // Resume must reuse the Metal pipeline and pool warmed by the eager path.
    DPFrameConverter *warmed = capture.frameConverter;
    DPColorConverter *metal = warmed.rgbConverter;
    CVPixelBufferPoolRef pool = warmed.pool;
    assert(metal && pool);
    CMSampleBufferRef buffered = [capture bufferSample:input];
    DPDeferredConversion *pending = (__bridge DPDeferredConversion *)CMGetAttachment(buffered, DPDeferredConversionKey, NULL);
    assert(pending.converter == warmed);
    CMSampleBufferRef resumed = DPCopyCaptureSampleForEncoder(buffered);
    assert(resumed && warmed.rgbConverter == metal && warmed.pool == pool && capture.frameConverter == warmed);
    CVPixelBufferRef resumedPixels = CMSampleBufferGetImageBuffer(resumed);
    CVPixelBufferLockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(resumedPixels, kCVPixelBufferLock_ReadOnly);
    for (size_t plane = 0; plane < 2; ++plane)
        for (size_t y = 0; y < CVPixelBufferGetHeightOfPlane(result, plane); ++y)
            assert(!memcmp((char *)CVPixelBufferGetBaseAddressOfPlane(result, plane) + y * CVPixelBufferGetBytesPerRowOfPlane(result, plane),
                (char *)CVPixelBufferGetBaseAddressOfPlane(resumedPixels, plane) + y * CVPixelBufferGetBytesPerRowOfPlane(resumedPixels, plane),
                64 * sizeof(uint16_t)));
    CVPixelBufferUnlockBaseAddress(resumedPixels, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    CFRelease(resumed);
    CFRelease(buffered);
    CVPixelBufferLockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    uint16_t *row = CVPixelBufferGetBaseAddressOfPlane(result, 0); unsigned fractional = 0;
    for (int x = 1; x < 63; ++x) {
        double value = (double)x / 63;
        if (!hdr) {
            double linear = value <= 0.04045 ? value/12.92 : pow((value+0.055)/1.055, 2.4);
            value = linear < 0.018 ? 4.5*linear : 1.099*pow(linear, 0.45)-0.099;
        }
        assert(abs((row[x] >> 6) - (int)lround(64 + 876*value)) <= 2);
        fractional += ((row[x] >> 6) & 3) != 0;
    }
    assert(fractional > 20);
    CVPixelBufferUnlockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    CFRelease(output); CFRelease(input); CFRelease(description); CFRelease(pixels);
    printf("PASS: native Metal RGB half-float %s to P010, analytic transfer ramp, full PQ highlights\n", hdr ? "P3/PQ" : "sRGB -> BT709 SDR");
}
// Golden codes computed in double precision from P3-D65/BT.2020 primary
// xy coordinates, D65 white, absolute ST2084 and BT.2020 nonconstant YCbCr.
// These include primaries and unequal channels: gray alone cannot test gamut.
static void testP3ColorBars(void) {
    const float colors[][3] = {{.75,0,0},{0,.75,0},{0,0,.75},{.5,.25,.125},{.25,.5,.75},{1,1,1},{0,0,0}};
    const int expected[][3] = {{485,283,657},{656,354,452},{390,691,549},{353,413,599},{513,624,487},{940,512,512},{64,512,512}};
    CVPixelBufferRef source = NULL, target = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES};
    assert(CVPixelBufferCreate(NULL, 56, 8, kCVPixelFormatType_64RGBAHalf, (__bridge CFDictionaryRef)attributes, &source) == kCVReturnSuccess);
    assert(CVPixelBufferCreate(NULL, 56, 8, kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange, (__bridge CFDictionaryRef)attributes, &target) == kCVReturnSuccess);
    CVPixelBufferLockBaseAddress(source, 0);
    for (unsigned y = 0; y < 8; ++y) {
        __fp16 *row = (__fp16 *)((char *)CVPixelBufferGetBaseAddress(source) + y*CVPixelBufferGetBytesPerRow(source));
        for (unsigned x = 0; x < 56; ++x) { for (unsigned c = 0; c < 3; ++c) row[4*x+c] = (__fp16)colors[x/8][c]; row[4*x+3] = 1; }
    }
    CVPixelBufferUnlockBaseAddress(source, 0);
    DPColorConverter *converter = [DPColorConverter new]; assert(converter);
    assert([converter convert:source toP010:target sourceColor:DPColorSourceP3PQ]);
    CVPixelBufferLockBaseAddress(target, kCVPixelBufferLock_ReadOnly);
    const uint16_t *luma = CVPixelBufferGetBaseAddressOfPlane(target, 0);
    const uint16_t *chroma = CVPixelBufferGetBaseAddressOfPlane(target, 1);
    for (unsigned bar = 0; bar < 7; ++bar) {
        unsigned x = bar*8+4;
        assert(abs((luma[x] >> 6) - expected[bar][0]) <= 2);
        assert(abs((chroma[x] >> 6) - expected[bar][1]) <= 2);
        assert(abs((chroma[x+1] >> 6) - expected[bar][2]) <= 2);
    }
    CVPixelBufferUnlockBaseAddress(target, kCVPixelBufferLock_ReadOnly);
    CFRelease(source); CFRelease(target);
    puts("PASS: native Metal P3/PQ RGB bars match independent BT.2020/PQ YUV golden codes");
}
static void testDenseRGBPrecision(void) {
    CVPixelBufferRef pixels = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES};
    assert(CVPixelBufferCreate(NULL, 1024, 8, kCVPixelFormatType_64RGBAHalf, (__bridge CFDictionaryRef)attributes, &pixels) == kCVReturnSuccess);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceITUR_2100_PQ);
    CVBufferSetAttachment(pixels, kCVImageBufferCGColorSpaceKey, space, kCVAttachmentMode_ShouldPropagate); CFRelease(space);
    CVPixelBufferLockBaseAddress(pixels, 0);
    for (int y = 0; y < 8; ++y) {
        __fp16 *row = (__fp16 *)((char *)CVPixelBufferGetBaseAddress(pixels) + y*CVPixelBufferGetBytesPerRow(pixels));
        for (int x = 0; x < 1024; ++x) { row[4*x] = row[4*x+1] = row[4*x+2] = (__fp16)(x/1023.0); row[4*x+3] = 1; }
    }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    CMVideoFormatDescriptionRef description = NULL; CMSampleBufferRef input = NULL;
    CMSampleTimingInfo timing = {CMTimeMake(1,60), kCMTimeZero, kCMTimeInvalid};
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &description) == noErr);
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, description, &timing, &input) == noErr);
    DPCapture *capture = [DPCapture new]; capture.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange; capture.hdr = YES;
    CMSampleBufferRef output = [capture convertSample:input]; assert(output);
    CVPixelBufferRef result = CMSampleBufferGetImageBuffer(output); CVPixelBufferLockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    const uint16_t *row = CVPixelBufferGetBaseAddressOfPlane(result, 0); bool seen[1024] = {false}; unsigned unique = 0;
    for (int x = 0; x < 1024; ++x) { unsigned code = row[x] >> 6; if (!seen[code]) { seen[code] = true; ++unique; }
        assert(abs((int)code - (int)lround(64 + 876*(double)(__fp16)(x/1023.0))) <= 1); }
    assert(unique > 800 && (row[0] >> 6) == 64 && (row[1023] >> 6) == 940);
    CVPixelBufferUnlockBaseAddress(result, kCVPixelBufferLock_ReadOnly);
    CFRelease(output); CFRelease(input); CFRelease(description); CFRelease(pixels);
    printf("PASS: %u distinct 10-bit PQ luma codes, black/10,000-nit white preserved, no 8-bit intermediate\n", unique);
}
static void testLifecycleNudge(void) {
    dispatch_queue_t queue = dispatch_queue_create("deskport.synthetic-lifecycle", DISPATCH_QUEUE_SERIAL);
    dispatch_semaphore_t joined = dispatch_semaphore_create(0);
    __block BOOL pendingContext = YES;
    __block NSUInteger frames = 0, ticks = 0, joins = 0;
    __weak DPCapture *weakCapture;
    @autoreleasepool {
        DPCapture *capture = session(queue, ^bool(CMSampleBufferRef s) {
            if (s) ++frames;
            else {
                ++ticks;
                if (pendingContext) {
                    pendingContext = NO;
                    ++joins; // Simulates draining the new encoder context queue.
                    dispatch_semaphore_signal(joined);
                }
            }
            return true;
        });
        weakCapture = capture;
        SCStream *unusedStream = (SCStream *)(id)[NSObject new];
        CMSampleBufferRef complete = sample(SCFrameStatusComplete);
        dispatch_sync(queue, ^{
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            // A new context can enqueue its nudge before heartbeat registration.
            DPNudgeCaptureLifecycle();
            [capture startHeartbeat];
            // Disable the watchdog: all following progress must be event-driven.
            dispatch_source_set_timer(capture.timer, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 0);
        });
        assert(dispatch_semaphore_wait(joined, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0);
        dispatch_sync(queue, ^{
            assert(frames == 1 && joins == 1 && ticks == 1 && capture.latest == complete);
            pendingContext = YES; // Another encoder joins an already-static capture.
        });
        DPNudgeCaptureLifecycle();
        assert(dispatch_semaphore_wait(joined, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0);
        dispatch_sync(queue, ^{
            assert(frames == 1 && joins == 2 && ticks == 2 && capture.latest == complete);
            // Posting from the capture queue must enqueue, never join itself.
            pendingContext = YES;
            DPNudgeCaptureLifecycle();
            assert(joins == 2);
        });
        assert(dispatch_semaphore_wait(joined, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) == 0);
        dispatch_sync(queue, ^{
            assert(joins == 3 && ticks == 3 && frames == 1);
            // A queued notification racing stop must not invoke the callback.
            DPNudgeCaptureLifecycle();
            [capture finish];
            assert(!capture.lifecycleObserver && !capture.latest);
        });
        DPNudgeCaptureLifecycle();
        dispatch_sync(queue, ^{ assert(joins == 3 && ticks == 3 && frames == 1); });
        CFRelease(complete);
    }
    dispatch_sync(queue, ^{});
    assert(!weakCapture);
    puts("PASS: immediate idle encoder join, startup nudge race, asynchronous lifecycle-only dispatch, stop race, weak observer lifetime");
}

int main(void) {
    @autoreleasepool {
        testLifecycleNudge();
        // Unsupported formats fail without invoking AVFoundation or capture.
        assert(setenv("DESKPORT_CAPTURE_DISPLAY_FILE", "/unused-test-descriptor", 1) == 0);
        DPScreenVideo *unsupported = [DPScreenVideo new];
        unsupported.pixelFormat = kCVPixelFormatType_64RGBAHalf;
        assert([unsupported capture:^bool(CMSampleBufferRef s) { return false; }] == nil);
        assert(!dp_source_hdr(0)); // Managed virtual source never masquerades as HDR.
        unsetenv("DESKPORT_CAPTURE_DISPLAY_FILE");
        SCStreamConfiguration *sdr = dp_capture_configuration(kCVPixelFormatType_32BGRA, YES);
        assert(sdr.pixelFormat == kCVPixelFormatType_32BGRA);
        if (@available(macOS 15.0, *)) {
#if defined(__arm64__)
            SCStreamConfiguration *hdr = dp_capture_configuration(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange, YES);
            assert(hdr.pixelFormat == kCVPixelFormatType_64RGBAHalf);
            assert(hdr.captureDynamicRange == SCCaptureDynamicRangeHDRCanonicalDisplay);
            assert(CFEqual(hdr.colorSpaceName, kCGColorSpaceDisplayP3_PQ));
            SCStreamConfiguration *tenSdr = dp_capture_configuration(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange, NO);
            assert(tenSdr.captureDynamicRange == SCCaptureDynamicRangeSDR);
            testConversion(NO, NO);
            testConversion(YES, NO);
            testConversion(YES, YES);
            testRGBConversion(NO);
            testRGBConversion(YES);
            testDenseRGBPrecision();
            testP3ColorBars();
#endif
        }
        dispatch_queue_t queue = dispatch_queue_create("deskport.synthetic-capture", DISPATCH_QUEUE_SERIAL);
        SCStream *unusedStream = (SCStream *)(id)[NSObject new];
        __block int frames = 0, ticks = 0;
        DPCapture *capture = session(queue, ^bool(CMSampleBufferRef s) {
            if (s) ++frames; else ++ticks;
            return true;
        });
        capture.diagnostics = NO;
        CMSampleBufferRef complete = sample(SCFrameStatusComplete);
        CMSampleBufferRef idle = sample(SCFrameStatusIdle);
        CMSampleBufferRef blank = sample(SCFrameStatusBlank);
        CMSampleBufferRef started = sample(SCFrameStatusStarted);
        dispatch_sync(queue, ^{
            [capture startHeartbeat];
            [capture stream:unusedStream didOutputSampleBuffer:idle ofType:SCStreamOutputTypeScreen];
            assert(frames == 0 && capture.latest == NULL);
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            assert(frames == 1 && capture.latest == complete);
            [capture stream:unusedStream didOutputSampleBuffer:idle ofType:SCStreamOutputTypeScreen];
            [capture stream:unusedStream didOutputSampleBuffer:blank ofType:SCStreamOutputTypeScreen];
            assert(frames == 1);
            // Native status is authoritative: never scan pixels, even equal ones.
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            [capture stream:unusedStream didOutputSampleBuffer:started ofType:SCStreamOutputTypeScreen];
            assert(frames == 3 && capture.latest == started);
            assert(capture.statsAt == 0 && capture.completeFrames == 0 && capture.idleFrames == 0);
        });
        // An idle heartbeat only services lifecycle. Replaying the retained surface
        // would make the encoder treat every tick as content and restart refinement.
        dispatch_semaphore_t delay = dispatch_semaphore_create(0);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 750 * NSEC_PER_MSEC), queue, ^{dispatch_semaphore_signal(delay);});
        assert(dispatch_semaphore_wait(delay, dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC)) == 0);
        dispatch_sync(queue, ^{
            assert(frames == 3 && ticks >= 2 && ticks <= 3 && capture.latest == started);
            // A fresh surface bypasses the idle interval and is delivered now.
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            assert(frames == 4 && capture.latest == complete);
            [capture finish];
            assert(capture.latest == NULL && capture.callback == nil && capture.timer == nil);
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            [capture deliver:complete];
            assert(frames == 4);
            [capture finish]; // Signals exactly once, even after duplicate stop/error.
        });
        assert(dispatch_semaphore_wait(capture.signal, DISPATCH_TIME_NOW) == 0);
        assert(dispatch_semaphore_wait(capture.signal, DISPATCH_TIME_NOW) != 0);
        // An idle client stop/reconfiguration is still observed within one tick.
        __block BOOL keepRunning = YES;
        DPCapture *idleStop = session(queue, ^bool(CMSampleBufferRef s) { return keepRunning; });
        dispatch_sync(queue, ^{
            [idleStop startHeartbeat];
            [idleStop stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            keepRunning = NO;
        });
        assert(dispatch_semaphore_wait(idleStop.signal, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)) == 0);
        dispatch_sync(queue, ^{ assert(idleStop.finished && !idleStop.failed && idleStop.callback == nil); });
        DPCapture *stop = session(queue, ^bool(CMSampleBufferRef s) { return false; });
        dispatch_sync(queue, ^{
            [stop stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            assert(stop.finished && stop.latest == NULL);
        });
        DPCapture *timeout = session(queue, ^bool(CMSampleBufferRef s) { return true; });
        dispatch_sync(queue, ^{ [timeout startHeartbeat]; timeout.started -= 6.0; });
        assert(dispatch_semaphore_wait(timeout.signal, dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC)) == 0);
        dispatch_sync(queue, ^{ assert(timeout.finished && timeout.failed && timeout.callback == nil); });
        DPCapture *failure = session(queue, ^bool(CMSampleBufferRef s) { return true; });
        [failure stream:unusedStream didStopWithError:[NSError errorWithDomain:@"synthetic" code:1 userInfo:nil]];
        assert(dispatch_semaphore_wait(failure.signal, dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC)) == 0);
        dispatch_sync(queue, ^{ assert(failure.failed && failure.finished); });
        CFRelease(complete); CFRelease(idle); CFRelease(blank); CFRelease(started);
        puts("PASS: native frame status, 2 Hz lifecycle-only heartbeat, immediate frames, idle stop, retained surface, late callback, timeout");
    }
    return 0;
}
