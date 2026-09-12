// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled with ARC. All session state and callbacks run on one serial queue.
#import "screen-video.h"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stdlib.h>
#include <string.h>

@interface DPCapture : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, strong) dispatch_semaphore_t signal;
@property(nonatomic, strong) dispatch_source_t timer;
@property(nonatomic, strong) SCStream *stream;
@property(nonatomic, copy) FrameCallbackBlock callback;
@property(nonatomic, assign) CMSampleBufferRef latest;
@property(nonatomic) double delivered;
@property(nonatomic) double started;
@property(nonatomic) BOOL finished;
@property(nonatomic) BOOL failed;
- (void)finish;
- (void)startDisplay:(CGDirectDisplayID)display configuration:(SCStreamConfiguration *)config;
@end

@implementation DPCapture
- (void)dealloc {
    if (_latest) CFRelease(_latest);
}
- (void)finish {
    if (self.finished) return;
    self.finished = YES;
    self.callback = nil; // Release C++ captures before waking their owning thread.
    if (self.timer) dispatch_source_cancel(self.timer);
    self.timer = nil;
    SCStream *stream = self.stream;
    self.stream = nil;
    [stream stopCaptureWithCompletionHandler:^(NSError *error) {
        // Keep the stream alive until asynchronous shutdown completes.
        (void)stream;
        (void)error;
    }];
    if (self.latest) CFRelease(self.latest);
    self.latest = NULL;
    dispatch_semaphore_signal(self.signal);
}
- (void)deliver:(CMSampleBufferRef)sample {
    if (self.finished) return;
    if (sample) self.delivered = CACurrentMediaTime();
    if (!self.callback(sample)) [self finish];
}
- (void)startHeartbeat {
    self.started = CACurrentMediaTime();
    // Idle capture must still observe shutdown/reconfiguration. Reuse one retained
    // surface once a second so a newly joined encoder receives an initial image.
    self.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.queue);
    dispatch_source_set_timer(self.timer, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                              100 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    __weak DPCapture *weakSelf = self;
    dispatch_source_set_event_handler(self.timer, ^{
        DPCapture *s = weakSelf;
        if (!s || s.finished) return;
        double now = CACurrentMediaTime();
        if (!s.latest && now - s.started >= 5.0) {
            NSLog(@"DeskPort ScreenCaptureKit: first-frame timeout");
            s.failed = YES;
            [s finish];
            return;
        }
        [s deliver:(s.latest && now - s.delivered >= 1.0) ? s.latest : NULL];
    });
    dispatch_resume(self.timer);
}
- (void)startDisplay:(CGDirectDisplayID)display configuration:(SCStreamConfiguration *)config {
    [self startHeartbeat];
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:NO
        completionHandler:^(SCShareableContent *content, NSError *error) {
        dispatch_async(self.queue, ^{
            if (self.finished) return;
            SCDisplay *selected = nil;
            for (SCDisplay *candidate in content.displays) {
                if (candidate.displayID == display) { selected = candidate; break; }
            }
            if (error || !selected) {
                NSLog(@"DeskPort ScreenCaptureKit: display unavailable (%@)", error);
                self.failed = YES;
                [self finish];
                return;
            }
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:selected excludingWindows:@[]];
            self.stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
            NSError *outputError = nil;
            if (![self.stream addStreamOutput:self type:SCStreamOutputTypeScreen
                          sampleHandlerQueue:self.queue error:&outputError]) {
                NSLog(@"DeskPort ScreenCaptureKit: output failed (%@)", outputError);
                self.failed = YES;
                [self finish];
                return;
            }
            [self.stream startCaptureWithCompletionHandler:^(NSError *startError) {
                if (startError) dispatch_async(self.queue, ^{
                    if (self.finished) return;
                    NSLog(@"DeskPort ScreenCaptureKit: start failed (%@)", startError);
                    self.failed = YES;
                    [self finish];
                });
            }];
        });
    }];
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    dispatch_async(self.queue, ^{
        if (self.finished) return;
        NSLog(@"DeskPort ScreenCaptureKit: capture stopped (%@)", error);
        self.failed = YES;
        [self finish];
    });
}
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    if (self.finished || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample)) return;
    NSArray *attachments = (__bridge NSArray *)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSNumber *status = attachments.firstObject[SCStreamFrameInfoStatus];
    // Idle samples have no new surface. Never map or compare their pixels.
    if (!status || (status.integerValue != SCFrameStatusComplete && status.integerValue != SCFrameStatusStarted) ||
        !CMSampleBufferGetImageBuffer(sample)) return;
    if (self.latest) CFRelease(self.latest);
    self.latest = (CMSampleBufferRef)CFRetain(sample);
    [self deliver:sample];
}
@end

@interface DPScreenVideo ()
@property(nonatomic, strong) dispatch_queue_t captureQueue;
@property(nonatomic, strong) NSMutableDictionary<NSValue *, DPCapture *> *captures;
@property(nonatomic, strong) AVVideo *legacy;
@end

@implementation DPScreenVideo
- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
    self = [super init]; // Do not start the legacy AVFoundation session.
    if (!self) return nil;
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
    if (!mode) return nil;
    self.displayID = displayID;
    self.pixelFormat = kCVPixelFormatType_32BGRA;
    self.frameWidth = (int)CGDisplayModeGetPixelWidth(mode);
    self.frameHeight = (int)CGDisplayModeGetPixelHeight(mode);
    self.minFrameDuration = CMTimeMake(1, MAX(1, frameRate));
    CFRelease(mode);
    self.captureQueue = dispatch_queue_create("deskport.screen-capture", DISPATCH_QUEUE_SERIAL);
    self.captures = [NSMutableDictionary dictionary];
    return self;
}
- (void)dealloc {
    // No session retains its owner. Stop and drain callbacks before releasing it.
    NSMutableDictionary *captures = _captures;
    if (!_captureQueue) return;
    dispatch_sync(_captureQueue, ^{
        for (DPCapture *capture in captures.allValues) [capture finish];
        [captures removeAllObjects];
    });
}
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)callback {
    // ScreenCaptureKit does not expose P010 among its documented output formats.
    // Preserve upstream 10-bit behavior without silently changing bit depth.
    if (self.pixelFormat != kCVPixelFormatType_32BGRA &&
        self.pixelFormat != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        if (!self.legacy) self.legacy = [[AVVideo alloc] initWithDisplay:self.displayID
            frameRate:(int)(1.0 / CMTimeGetSeconds(self.minFrameDuration))];
        self.legacy.pixelFormat = self.pixelFormat;
        [self.legacy setFrameWidth:self.frameWidth frameHeight:self.frameHeight];
        NSLog(@"DeskPort capture: AVFoundation compatibility format %u (no CPU pixel comparison)", self.pixelFormat);
        return [self.legacy capture:callback];
    }
    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
    config.width = self.frameWidth;
    config.height = self.frameHeight;
    config.pixelFormat = self.pixelFormat;
    config.minimumFrameInterval = self.minFrameDuration;
    config.queueDepth = 8;
    config.showsCursor = YES;
    config.capturesAudio = NO;
    config.preservesAspectRatio = YES;
    DPCapture *capture = [[DPCapture alloc] init];
    capture.queue = self.captureQueue;
    capture.signal = dispatch_semaphore_create(0);
    capture.callback = callback;
    dispatch_sync(self.captureQueue, ^{
        self.captures[[NSValue valueWithPointer:(__bridge const void *)capture.signal]] = capture;
        [capture startDisplay:self.displayID configuration:config];
    });
    NSLog(@"DeskPort capture: ScreenCaptureKit native updates, no CPU pixel comparison");
    return capture.signal;
}
- (BOOL)captureFailed:(dispatch_semaphore_t)signal {
    __block BOOL failed = NO;
    dispatch_sync(self.captureQueue, ^{
        failed = self.captures[[NSValue valueWithPointer:(__bridge const void *)signal]].failed;
    });
    return failed;
}
- (void)cancelCapture:(dispatch_semaphore_t)signal {
    dispatch_sync(self.captureQueue, ^{
        NSValue *key = [NSValue valueWithPointer:(__bridge const void *)signal];
        DPCapture *capture = self.captures[key];
        if (capture) {
            [capture finish];
            [self.captures removeObjectForKey:key];
        } else {
            [self.legacy cancelCapture:signal];
        }
    });
}
@end
