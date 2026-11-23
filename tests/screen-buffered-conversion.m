// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic samples and controlled converters only. Never start a screen stream.
#import "host/macos/screen-video.m"
#include <assert.h>

@interface CountingConversion : DPFrameConverter
@property(nonatomic) NSUInteger calls;
@property(nonatomic) BOOL reject;
@end
@implementation CountingConversion
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample {
    ++self.calls;
    if (self.reject) return NULL;
    CMSampleTimingInfo timing;
    assert(CMSampleBufferGetSampleTimingInfo(sample, 0, &timing) == noErr);
    CMSampleBufferRef output = NULL;
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, CMSampleBufferGetImageBuffer(sample),
        CMSampleBufferGetFormatDescription(sample), &timing, &output) == noErr);
    return output;
}
@end

@interface BlockingConversion : CountingConversion
@property(nonatomic, strong) dispatch_semaphore_t entered;
@property(nonatomic, strong) dispatch_semaphore_t proceed;
@property(nonatomic) BOOL blockNext;
@end
@implementation BlockingConversion
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample {
    dispatch_semaphore_signal(self.entered);
    if (self.blockNext) {
        self.blockNext = NO;
        assert(dispatch_semaphore_wait(self.proceed, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
    }
    return [super convertSample:sample];
}
@end

@interface CountingCapture : DPCapture
@property(nonatomic) NSUInteger eagerCalls;
@end
@implementation CountingCapture
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample {
    ++self.eagerCalls;
    return [super convertSample:sample];
}
@end

static CMSampleBufferRef sample(int frame) {
    CVPixelBufferRef pixels = NULL;
    assert(CVPixelBufferCreate(NULL, 16, 16, kCVPixelFormatType_32BGRA, NULL, &pixels) == kCVReturnSuccess);
    CMVideoFormatDescriptionRef format = NULL;
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &format) == noErr);
    CMSampleTimingInfo timing = {CMTimeMake(1, 60), CMTimeMake(frame, 60), kCMTimeInvalid};
    CMSampleBufferRef result = NULL;
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, format, &timing, &result) == noErr);
    NSMutableArray *attachments = (__bridge NSMutableArray *)CMSampleBufferGetSampleAttachmentsArray(result, true);
    attachments[0][SCStreamFrameInfoStatus] = @(SCFrameStatusComplete);
    CFRelease(format);
    CFRelease(pixels);
    return result;
}

static CountingCapture *capture(FrameCallbackBlock callback) {
    CountingCapture *result = [CountingCapture new];
    result.queue = dispatch_queue_create("deskport.buffered-test", DISPATCH_QUEUE_SERIAL);
    result.signal = dispatch_semaphore_create(0);
    result.callback = callback;
    result.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    result.frameConverter = [CountingConversion new];
    return result;
}

static void testNativeConversion(void) {
    // Compare eager and deferred execution of the unchanged native VT converter.
    // Synthetic P010 requires neither screen permission nor a running capture.
    CVPixelBufferRef pixels = NULL;
    NSDictionary *attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    assert(CVPixelBufferCreate(NULL, 16, 16, kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange,
        (__bridge CFDictionaryRef)attributes, &pixels) == kCVReturnSuccess);
    dp_set_output_color(pixels, NO);
    assert(CVPixelBufferLockBaseAddress(pixels, 0) == kCVReturnSuccess);
    for (size_t plane = 0; plane < 2; ++plane) {
        for (size_t row = 0; row < CVPixelBufferGetHeightOfPlane(pixels, plane); ++row) {
            uint16_t *values = (uint16_t *)((char *)CVPixelBufferGetBaseAddressOfPlane(pixels, plane) +
                row * CVPixelBufferGetBytesPerRowOfPlane(pixels, plane));
            for (size_t x = 0; x < 16; ++x) values[x] = (plane ? 512 : 64 + row * 8 + x) << 6;
        }
    }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    CMVideoFormatDescriptionRef format = NULL;
    assert(CMVideoFormatDescriptionCreateForImageBuffer(NULL, pixels, &format) == noErr);
    CMSampleTimingInfo timing = {CMTimeMake(1, 60), CMTimeMake(33, 60), kCMTimeInvalid};
    CMSampleBufferRef raw = NULL;
    assert(CMSampleBufferCreateReadyWithImageBuffer(NULL, pixels, format, &timing, &raw) == noErr);
    CFRelease(format);
    CFRelease(pixels);
    DPCapture *source = [DPCapture new];
    source.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    CMSampleBufferRef eager = [source convertSample:raw];
    DPFrameConverter *warmed = source.frameConverter;
    CVPixelBufferPoolRef pool = warmed.pool;
    VTPixelTransferSessionRef transfer = warmed.transfer;
    assert(eager && pool && transfer);
    CMSampleBufferRef buffered = [source bufferSample:raw];
    assert(source.frameConverter.targetFormat == source.targetFormat && !source.frameConverter.hdr);
    DPDeferredConversion *pending = (__bridge DPDeferredConversion *)CMGetAttachment(buffered, DPDeferredConversionKey, NULL);
    assert(pending.converter == warmed);
    CMSampleBufferRef deferred = DPCopyCaptureSampleForEncoder(buffered);
    assert(source.frameConverter == warmed && warmed.pool == pool && warmed.transfer == transfer);
    assert(eager && deferred && !CMGetAttachment(deferred, DPDeferredConversionKey, NULL));
    assert(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(deferred), timing.presentationTimeStamp) == 0);
    CVPixelBufferRef a = CMSampleBufferGetImageBuffer(eager), b = CMSampleBufferGetImageBuffer(deferred);
    assert(CVPixelBufferGetPixelFormatType(a) == CVPixelBufferGetPixelFormatType(b));
    assert(CVPixelBufferLockBaseAddress(a, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess);
    assert(CVPixelBufferLockBaseAddress(b, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess);
    for (size_t plane = 0; plane < 2; ++plane)
        for (size_t row = 0; row < CVPixelBufferGetHeightOfPlane(a, plane); ++row)
            assert(!memcmp((char *)CVPixelBufferGetBaseAddressOfPlane(a, plane) + row * CVPixelBufferGetBytesPerRowOfPlane(a, plane),
                (char *)CVPixelBufferGetBaseAddressOfPlane(b, plane) + row * CVPixelBufferGetBytesPerRowOfPlane(b, plane), 16 * sizeof(uint16_t)));
    CVPixelBufferUnlockBaseAddress(a, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(b, kCVPixelBufferLock_ReadOnly);
    CFRelease(deferred);
    CFRelease(buffered);
    CFRelease(eager);
    CFRelease(raw);
    puts("PASS: native eager/deferred P010 pixels and timing match");
}

static void testSerializedConversion(void) {
    DPCapture *source = [DPCapture new];
    source.targetFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    BlockingConversion *converter = [BlockingConversion new];
    converter.entered = dispatch_semaphore_create(0);
    converter.proceed = dispatch_semaphore_create(0);
    converter.blockNext = YES;
    source.frameConverter = converter;
    CMSampleBufferRef raw = sample(300);
    CMSampleBufferRef buffered = [source bufferSample:raw];
    dispatch_group_t consumers = dispatch_group_create();
    dispatch_queue_t queue = dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0);
    dispatch_group_async(consumers, queue, ^{
        CMSampleBufferRef output = DPCopyCaptureSampleForEncoder(buffered);
        assert(output);
        CFRelease(output);
    });
    assert(dispatch_semaphore_wait(converter.entered, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
    dispatch_semaphore_t eagerStarted = dispatch_semaphore_create(0);
    dispatch_group_async(consumers, queue, ^{
        dispatch_semaphore_signal(eagerStarted);
        CMSampleBufferRef output = [source convertSample:raw];
        assert(output);
        CFRelease(output);
    });
    assert(dispatch_semaphore_wait(eagerStarted, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
    // The eager route cannot enter resources still in use by deferred conversion.
    assert(dispatch_semaphore_wait(converter.entered, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC)) != 0);
    dispatch_semaphore_signal(converter.proceed);
    assert(dispatch_group_wait(consumers, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
    assert(converter.calls == 2);
    CFRelease(buffered);
    CFRelease(raw);
    puts("PASS: eager and deferred samples serialize on one converter");
}

int main(void) {
    @autoreleasepool {
        testNativeConversion();
        testSerializedConversion();
        SCStream *unusedStream = (SCStream *)(id)[NSObject new];
        __block BOOL paused = NO;
        __block CMSampleBufferRef retained = NULL;
        CountingConversion *converter = [CountingConversion new];
        CountingCapture *source = capture(^bool(CMSampleBufferRef current) {
            if (current) {
                if (retained) CFRelease(retained);
                retained = (CMSampleBufferRef)CFRetain(current);
            }
            return true;
        });
        source.conversionPaused = ^BOOL { return paused; };
        source.frameConverter = converter;
        // Warm through the actual eager wrapper before entering the paused path.
        CMSampleBufferRef warmup = sample(-1);
        [source stream:unusedStream didOutputSampleBuffer:warmup ofType:SCStreamOutputTypeScreen];
        CFRelease(warmup);
        assert(source.eagerCalls == 1 && converter.calls == 1);
        paused = YES;
        __weak DPCapture *weakCapture = source;
        __weak DPDeferredConversion *firstPending = nil;
        for (int frame = 0; frame < 200; ++frame) {
            @autoreleasepool {
                CMSampleBufferRef input = sample(frame);
                [source stream:unusedStream didOutputSampleBuffer:input ofType:SCStreamOutputTypeScreen];
                if (!frame) firstPending = (__bridge DPDeferredConversion *)CMGetAttachment(source.latest, DPDeferredConversionKey, NULL);
                // Buffering must neither convert nor attach state to SCK's sample.
                assert(!CMGetAttachment(input, DPDeferredConversionKey, NULL));
                CFRelease(input);
            }
        }
        assert(source.eagerCalls == 1 && converter.calls == 1);
        assert(!firstPending); // Replaced raw frames do not accumulate in capture.
        assert(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(retained), CMTimeMake(199, 60)) == 0);
        CMSampleBufferRef latest = retained;
        retained = NULL;
        [source deliver:NULL];
        assert(!retained && source.eagerCalls == 1 && converter.calls == 1);
        paused = NO;
        CMSampleBufferRef resumed = sample(200);
        [source stream:unusedStream didOutputSampleBuffer:resumed ofType:SCStreamOutputTypeScreen];
        CFRelease(resumed);
        assert(source.eagerCalls == 2 && converter.calls == 2 && source.frameConverter == converter);
        assert(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(retained), CMTimeMake(200, 60)) == 0);
        CFRelease(retained);
        retained = NULL;
        // Captured images can outlive cancellation/reinit. No capture-queue sync
        // or strong reference to the real capture may be needed to resolve one.
        [source finish];
        source = nil;
        assert(!weakCapture);
        dispatch_group_t viewers = dispatch_group_create();
        for (int viewer = 0; viewer < 8; ++viewer) {
            dispatch_group_async(viewers, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
                @autoreleasepool {
                    CMSampleBufferRef output = DPCopyCaptureSampleForEncoder(latest);
                    assert(output && CMSampleBufferGetImageBuffer(output) == CMSampleBufferGetImageBuffer(latest));
                    CFRelease(output);
                }
            });
        }
        assert(dispatch_group_wait(viewers, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0);
        assert(converter.calls == 3);
        __weak DPFrameConverter *weakConverter = converter;
        converter = nil;
        CFRelease(latest);
        assert(!weakConverter);

        // Normal first frame, dummy-style capture (no gate), and 8-bit capture
        // keep their eager path, even when a process-wide pause could be stale.
        CountingCapture *normal = capture(^bool(CMSampleBufferRef current) { return true; });
        CMSampleBufferRef input = sample(201);
        normal.conversionPaused = ^BOOL { return NO; };
        [normal stream:unusedStream didOutputSampleBuffer:input ofType:SCStreamOutputTypeScreen];
        normal.conversionPaused = nil;
        [normal stream:unusedStream didOutputSampleBuffer:input ofType:SCStreamOutputTypeScreen];
        __block NSUInteger eightBitGateCalls = 0;
        normal.conversionPaused = ^BOOL { ++eightBitGateCalls; return YES; };
        normal.targetFormat = kCVPixelFormatType_32BGRA;
        [normal stream:unusedStream didOutputSampleBuffer:input ofType:SCStreamOutputTypeScreen];
        assert(normal.eagerCalls == 3 && ((CountingConversion *)normal.frameConverter).calls == 2 && !eightBitGateCalls);
        CMSampleBufferRef eager = DPCopyCaptureSampleForEncoder(normal.latest);
        assert(eager == normal.latest);
        CFRelease(eager);
        [normal finish];
        CFRelease(input);

        // Conversion failure is cached and reported to every consuming viewer.
        // Cancellation still releases the bounded retained raw frame.
        CountingCapture *failed = capture(^bool(CMSampleBufferRef current) { return true; });
        CountingConversion *reject = [CountingConversion new];
        reject.reject = YES;
        failed.frameConverter = reject;
        failed.conversionPaused = ^BOOL { return YES; };
        input = sample(202);
        [failed stream:unusedStream didOutputSampleBuffer:input ofType:SCStreamOutputTypeScreen];
        CFRelease(input);
        assert(!DPCopyCaptureSampleForEncoder(failed.latest));
        assert(!DPCopyCaptureSampleForEncoder(failed.latest));
        assert(reject.calls == 1);
        [failed finish];
        assert(!failed.latest);
        puts("PASS: paused raw buffering, newest frame, eager first frame/probe/SDR8, concurrent viewers, failure, cancel/reinit lifetime");
    }
    return 0;
}
