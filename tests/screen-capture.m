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
int main(void) {
    @autoreleasepool {
        dispatch_queue_t queue = dispatch_queue_create("deskport.synthetic-capture", DISPATCH_QUEUE_SERIAL);
        SCStream *unusedStream = (SCStream *)(id)[NSObject new];
        __block int frames = 0, ticks = 0;
        DPCapture *capture = session(queue, ^bool(CMSampleBufferRef s) {
            if (s) ++frames; else ++ticks;
            return true;
        });
        CMSampleBufferRef complete = sample(SCFrameStatusComplete);
        CMSampleBufferRef idle = sample(SCFrameStatusIdle);
        CMSampleBufferRef blank = sample(SCFrameStatusBlank);
        CMSampleBufferRef started = sample(SCFrameStatusStarted);
        dispatch_sync(queue, ^{
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
            [capture startHeartbeat];
            capture.delivered = CACurrentMediaTime() - 2.0;
        });
        // A static stream publishes its retained surface, then lifecycle-only ticks.
        dispatch_semaphore_t delay = dispatch_semaphore_create(0);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 450 * NSEC_PER_MSEC), queue, ^{dispatch_semaphore_signal(delay);});
        assert(dispatch_semaphore_wait(delay, dispatch_time(DISPATCH_TIME_NOW, 2*NSEC_PER_SEC)) == 0);
        dispatch_sync(queue, ^{
            assert(frames == 4 && ticks >= 1);
            [capture finish];
            assert(capture.latest == NULL && capture.callback == nil && capture.timer == nil);
            [capture stream:unusedStream didOutputSampleBuffer:complete ofType:SCStreamOutputTypeScreen];
            [capture deliver:complete];
            assert(frames == 4);
            [capture finish]; // Signals exactly once, even after duplicate stop/error.
        });
        assert(dispatch_semaphore_wait(capture.signal, DISPATCH_TIME_NOW) == 0);
        assert(dispatch_semaphore_wait(capture.signal, DISPATCH_TIME_NOW) != 0);
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
        puts("PASS: native frame status, static heartbeat, retained surface, stop, late callback, timeout");
    }
    return 0;
}
