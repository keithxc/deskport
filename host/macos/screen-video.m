// SPDX-License-Identifier: GPL-3.0-or-later
// Compiled with ARC. All session state and callbacks run on one serial queue.
#import "screen-video.h"
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>
#import <VideoToolbox/VideoToolbox.h>
#include <stdlib.h>
#include <stdio.h>
// Route capture diagnostics through the parent pipe, never the system NSLog store.
#define DP_CAPTURE_LOG(format, ...) fprintf(stderr, "%s\n", [[NSString stringWithFormat:format, ##__VA_ARGS__] UTF8String])
#include <string.h>
#include "admitted-display.h"
#import "color-conversion.h"

static NSString *const DPCaptureLifecycleNotification = @"DeskPortCaptureLifecycle";

void DPNudgeCaptureLifecycle(void) {
    [[NSNotificationCenter defaultCenter] postNotificationName:DPCaptureLifecycleNotification object:nil];
}

// HDR is an active source property, independent of a 10-bit encoder profile.
// Managed virtual outputs currently publish SDR modes; never infer HDR from P010.
static BOOL dp_source_hdr(CGDirectDisplayID display) {
#if defined(__arm64__)
    if (@available(macOS 15.0, *)) {
        if (getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) return NO;
        for (NSScreen *screen in [NSScreen screens]) {
            if ([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == display)
                return screen.maximumExtendedDynamicRangeColorComponentValue > 1.0;
        }
    }
#endif
    return NO;
}

static SCStreamConfiguration *dp_capture_configuration(OSType format, BOOL hdr) {
    SCStreamConfiguration *config = [SCStreamConfiguration new];
    if (format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange) {
#if defined(__arm64__)
        if (@available(macOS 15.0, *)) {
            // SCK xf44 may omit its YCbCr matrix even on current systems.
            // RGB half-float preserves precision and its native CGColorSpace
            // describes it completely, without guessing a source YUV matrix.
            if (hdr) config = [SCStreamConfiguration streamConfigurationWithPreset:SCStreamConfigurationPresetCaptureHDRStreamCanonicalDisplay];
            config.pixelFormat = kCVPixelFormatType_64RGBAHalf;
            config.captureDynamicRange = hdr ? SCCaptureDynamicRangeHDRCanonicalDisplay : SCCaptureDynamicRangeSDR;
            config.colorSpaceName = hdr ? kCGColorSpaceDisplayP3_PQ : kCGColorSpaceSRGB;
            // Metal performs the RGB/gamut/transfer conversion to the encoder's
            // explicit video-range BT.709 or BT.2020/PQ P010 contract.
            return config;
        }
#endif
        return nil; // Older OS/Intel: probe fails synchronously, then use SDR8.
    }
    if (format != kCVPixelFormatType_32BGRA && format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) return nil;
    config.pixelFormat = format;
    config.colorSpaceName = kCGColorSpaceITUR_709;
    config.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
    return config;
}

static void dp_set_output_color(CVPixelBufferRef pixels, BOOL hdr) {
    CVBufferRemoveAttachment(pixels, kCVImageBufferICCProfileKey);
    CVBufferRemoveAttachment(pixels, kCVImageBufferCGColorSpaceKey);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(hdr ? kCGColorSpaceITUR_2100_PQ : kCGColorSpaceITUR_709);
    if (space) {
        CVBufferSetAttachment(pixels, kCVImageBufferCGColorSpaceKey, space, kCVAttachmentMode_ShouldPropagate);
        CFRelease(space);
    }
    CVBufferSetAttachment(pixels, kCVImageBufferColorPrimariesKey, hdr ? kCVImageBufferColorPrimaries_ITU_R_2020 : kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferTransferFunctionKey, hdr ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ : kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(pixels, kCVImageBufferYCbCrMatrixKey, hdr ? kCVImageBufferYCbCrMatrix_ITU_R_2020 : kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
}

// Conversion resources have no capture, queue or callback reference. Buffered
// frames can therefore survive capture cancellation/reconfiguration safely.
@interface DPFrameConverter : NSObject
@property(nonatomic) OSType targetFormat;
@property(nonatomic) BOOL hdr;
@property(nonatomic) VTPixelTransferSessionRef transfer;
@property(nonatomic) CVPixelBufferPoolRef pool;
@property(nonatomic, strong) DPColorConverter *rgbConverter;
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample CF_RETURNS_RETAINED;
@end

@implementation DPFrameConverter
- (void)dealloc {
    if (_transfer) { VTPixelTransferSessionInvalidate(_transfer); CFRelease(_transfer); }
    if (_pool) CFRelease(_pool);
}
// Keep precision and transfer function through the format/range conversion.
// Reject missing/wrong HDR attachments rather than relabelling SDR pixels.
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample {
    if (self.targetFormat != kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)
        return (CMSampleBufferRef)CFRetain(sample);
    CVPixelBufferRef source = CMSampleBufferGetImageBuffer(sample);
    OSType input = CVPixelBufferGetPixelFormatType(source);
    const BOOL rgbHalf = input == kCVPixelFormatType_64RGBAHalf;
    if (!rgbHalf && input != kCVPixelFormatType_444YpCbCr10BiPlanarFullRange && input != self.targetFormat) {
        DP_CAPTURE_LOG(@"DeskPort capture: expected 10-bit surface, received %u", input);
        return NULL;
    }
    CFStringRef primaries = self.hdr ? kCVImageBufferColorPrimaries_ITU_R_2020 : kCVImageBufferColorPrimaries_ITU_R_709_2;
    CFStringRef transfer = self.hdr ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ : kCVImageBufferTransferFunction_ITU_R_709_2;
    CFStringRef matrix = self.hdr ? kCVImageBufferYCbCrMatrix_ITU_R_2020 : kCVImageBufferYCbCrMatrix_ITU_R_709_2;
    const CFStringRef keys[] = {kCVImageBufferColorPrimariesKey, kCVImageBufferTransferFunctionKey, kCVImageBufferYCbCrMatrixKey};
    const CFStringRef values[] = {primaries, transfer, matrix};
    const CFStringRef formatKeys[] = {kCMFormatDescriptionExtension_ColorPrimaries, kCMFormatDescriptionExtension_TransferFunction, kCMFormatDescriptionExtension_YCbCrMatrix};
    // SCK can carry gamut/transfer solely as a native CGColorSpace attachment.
    // Decode only named, unambiguous spaces; the YCbCr matrix still needs its
    // own native declaration. Never infer HDR from bit depth or a missing tag.
    CFStringRef nativePrimaries = NULL, nativeTransfer = NULL;
    DPColorSource rgbMode = DPColorSourceUnsupported;
    CFTypeRef rgb = CVBufferCopyAttachment(source, kCVImageBufferCGColorSpaceKey, NULL);
    if (rgb && CFGetTypeID(rgb) == CGColorSpaceGetTypeID()) {
        CFStringRef name = CGColorSpaceGetName((CGColorSpaceRef)rgb);
        if (name && (CFEqual(name, kCGColorSpaceSRGB) || CFEqual(name, kCGColorSpaceExtendedSRGB))) {
            nativePrimaries = kCVImageBufferColorPrimaries_ITU_R_709_2;
            nativeTransfer = kCVImageBufferTransferFunction_sRGB;
            rgbMode = DPColorSourceSRGB;
        } else if (name && (CFEqual(name, kCGColorSpaceLinearSRGB) || CFEqual(name, kCGColorSpaceExtendedLinearSRGB))) {
            nativePrimaries = kCVImageBufferColorPrimaries_ITU_R_709_2;
            nativeTransfer = kCVImageBufferTransferFunction_Linear;
            rgbMode = DPColorSourceLinear709;
        } else if (name && CFEqual(name, kCGColorSpaceITUR_709)) {
            nativePrimaries = kCVImageBufferColorPrimaries_ITU_R_709_2;
            nativeTransfer = kCVImageBufferTransferFunction_ITU_R_709_2;
        } else if (name && (CFEqual(name, kCGColorSpaceDisplayP3_PQ) || CFEqual(name, kCGColorSpaceITUR_2100_PQ))) {
            nativePrimaries = CFEqual(name, kCGColorSpaceDisplayP3_PQ) ? kCVImageBufferColorPrimaries_P3_D65 : kCVImageBufferColorPrimaries_ITU_R_2020;
            nativeTransfer = kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ;
            rgbMode = CFEqual(name, kCGColorSpaceDisplayP3_PQ) ? DPColorSourceP3PQ : DPColorSource2020PQ;
        }
    }
    if (rgb) CFRelease(rgb);
    if (rgbHalf && (!rgbMode || (self.hdr != (rgbMode == DPColorSourceP3PQ || rgbMode == DPColorSource2020PQ)))) return NULL;
    for (int i = 0; i < 3; ++i) {
        if (rgbHalf && i == 2) continue; // RGB has no source YCbCr matrix.
        CFTypeRef value = CVBufferCopyAttachment(source, keys[i], NULL);
        if (!value) {
            CFTypeRef nativeValue = CMFormatDescriptionGetExtension(CMSampleBufferGetFormatDescription(sample), formatKeys[i]);
            if (nativeValue) value = CFRetain(nativeValue);
        }
        if (!value && i == 0 && nativePrimaries) value = CFRetain(nativePrimaries);
        if (!value && i == 1 && nativeTransfer) value = CFRetain(nativeTransfer);
        BOOL matches = value && CFEqual(value, rgbHalf ? (i == 0 ? nativePrimaries : nativeTransfer) : values[i]);
        if (self.hdr && value) {
            // SCK's canonical HDR preset uses P3/PQ with a 709 YCbCr matrix.
            // These are genuine HDR samples; VT performs the gamut/matrix
            // conversion, rather than merely changing attachment strings.
            if (i == 0 && CFEqual(value, kCVImageBufferColorPrimaries_P3_D65)) matches = YES;
            if (i == 2 && CFEqual(value, kCVImageBufferYCbCrMatrix_ITU_R_709_2)) matches = YES;
        }
        if (value && ((i == 0 && nativePrimaries && !CFEqual(value, nativePrimaries)) ||
                      (i == 1 && nativeTransfer && !CFEqual(value, nativeTransfer)))) matches = NO;
        if (!matches) {
            CFTypeRef extension = CMFormatDescriptionGetExtension(CMSampleBufferGetFormatDescription(sample), formatKeys[i]);
            DP_CAPTURE_LOG(@"DeskPort capture: color key %@, image=%@, sample=%@, expected=%@",
                (__bridge NSString *)keys[i], value ? (__bridge id)value : @"missing",
                extension ? (__bridge id)extension : @"missing", (__bridge NSString *)values[i]);
            CFTypeRef rgb = CVBufferCopyAttachment(source, kCVImageBufferCGColorSpaceKey, NULL);
            if (rgb && CFGetTypeID(rgb) == CGColorSpaceGetTypeID()) {
                DP_CAPTURE_LOG(@"DeskPort capture: source RGB space %@", (__bridge id)CGColorSpaceGetName((CGColorSpaceRef)rgb));
            }
            if (rgb) CFRelease(rgb);
            if (value) CFRelease(value);
            return NULL;
        }
        // Some SCK releases put colorimetry only in the native format description.
        // Propagate those verified values to VT's image input, without guessing.
        CVBufferSetAttachment(source, keys[i], value, kCVAttachmentMode_ShouldPropagate);
        if (value) CFRelease(value);
    }
    if (!rgbHalf && !_transfer) {
        if (VTPixelTransferSessionCreate(NULL, &_transfer) != noErr) return NULL;
        const CFStringRef properties[] = {kVTPixelTransferPropertyKey_DestinationColorPrimaries,
            kVTPixelTransferPropertyKey_DestinationTransferFunction, kVTPixelTransferPropertyKey_DestinationYCbCrMatrix};
        for (int i = 0; i < 3; ++i)
            if (VTSessionSetProperty(_transfer, properties[i], values[i]) != noErr) return NULL;
    }
    if (!_pool) {
        NSDictionary *attributes = @{(id)kCVPixelBufferWidthKey: @(CVPixelBufferGetWidth(source)),
            (id)kCVPixelBufferHeightKey: @(CVPixelBufferGetHeight(source)),
            (id)kCVPixelBufferPixelFormatTypeKey: @(self.targetFormat), (id)kCVPixelBufferIOSurfacePropertiesKey: @{}, (id)kCVPixelBufferMetalCompatibilityKey: @YES};
        if (CVPixelBufferPoolCreate(NULL, NULL, (__bridge CFDictionaryRef)attributes, &_pool) != kCVReturnSuccess) return NULL;
    }
    CVPixelBufferRef destination = NULL;
    if (CVPixelBufferPoolCreatePixelBuffer(NULL, _pool, &destination) != kCVReturnSuccess) return NULL;
    // Establish the target description before native conversion; only checked
    // source colorimetry is accepted, and source tags are never used as output tags.
    dp_set_output_color(destination, self.hdr);
    OSStatus status;
    if (rgbHalf) {
        if (!self.rgbConverter) self.rgbConverter = [DPColorConverter new];
        status = self.rgbConverter && [self.rgbConverter convert:source toP010:destination sourceColor:rgbMode] ? noErr : -1;
    } else status = VTPixelTransferSessionTransferImage(_transfer, source, destination);
    CMSampleBufferRef result = NULL;
    CMVideoFormatDescriptionRef description = NULL;
    if (status == noErr) {
        CVBufferPropagateAttachments(source, destination);
        // Source ICC/CGColorSpace must not replace the converted output contract.
        dp_set_output_color(destination, self.hdr);
        status = CMVideoFormatDescriptionCreateForImageBuffer(NULL, destination, &description);
        CMSampleTimingInfo timing;
        if (status == noErr) status = CMSampleBufferGetSampleTimingInfo(sample, 0, &timing);
        if (status == noErr) status = CMSampleBufferCreateReadyWithImageBuffer(NULL, destination, description, &timing, &result);
    }
    if (description) CFRelease(description);
    CFRelease(destination);
    return result;
}
@end

static const CFStringRef DPDeferredConversionKey = CFSTR("DeskPortDeferredP010");
@interface DPDeferredConversion : NSObject
@property(nonatomic, strong) DPFrameConverter *converter;
@property(nonatomic) CMSampleBufferRef result;
@property(nonatomic) BOOL attempted;
@end
@implementation DPDeferredConversion
- (void)dealloc {
    if (_result) CFRelease(_result);
}
@end

CMSampleBufferRef DPCopyCaptureSampleForEncoder(CMSampleBufferRef sample) {
    if (!sample) return NULL;
    DPDeferredConversion *pending = (__bridge DPDeferredConversion *)CMGetAttachment(sample, DPDeferredConversionKey, NULL);
    if (!pending) return (CMSampleBufferRef)CFRetain(sample);
    // One result per raw sample, shared by all viewers. Different raw frames
    // serialize on the same warmed converter as eager capture. Neither
    // lock ever dispatches to the capture queue or waits for an encoder callback.
    @synchronized (pending) {
        if (!pending.attempted) {
            pending.attempted = YES;
            @synchronized (pending.converter) {
                pending.result = [pending.converter convertSample:sample];
            }
        }
        return pending.result ? (CMSampleBufferRef)CFRetain(pending.result) : NULL;
    }
}

@interface DPCapture : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, strong) dispatch_semaphore_t signal;
@property(nonatomic, strong) dispatch_source_t timer;
@property(nonatomic, strong) id lifecycleObserver;
@property(nonatomic, strong) SCStream *stream;
@property(nonatomic, copy) FrameCallbackBlock callback;
@property(nonatomic, assign) CMSampleBufferRef latest;
@property(nonatomic) double started;
@property(nonatomic) BOOL finished;
@property(nonatomic) BOOL failed;
@property(nonatomic) BOOL diagnostics;
@property(nonatomic) double statsAt;
@property(nonatomic) NSUInteger completeFrames;
@property(nonatomic) NSUInteger idleFrames;
@property(nonatomic) NSUInteger emptyDamageFrames;
@property(nonatomic) OSType targetFormat;
@property(nonatomic) BOOL hdr;
@property(nonatomic, copy) BOOL (^conversionPaused)(void);
@property(nonatomic, strong) DPFrameConverter *frameConverter;
- (DPFrameConverter *)conversionContext;
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample CF_RETURNS_RETAINED;
- (CMSampleBufferRef)bufferSample:(CMSampleBufferRef)sample CF_RETURNS_RETAINED;
- (void)finish;
- (void)startDisplay:(CGDirectDisplayID)display configuration:(SCStreamConfiguration *)config;
@end

@implementation DPCapture
- (instancetype)init {
    if ((self = [super init])) {
        const char *diagnostics = getenv("DESKPORT_CAPTURE_DIAGNOSTICS");
        self.diagnostics = diagnostics && strcmp(diagnostics, "1") == 0;
    }
    return self;
}
- (void)dealloc {
    if (_lifecycleObserver) [[NSNotificationCenter defaultCenter] removeObserver:_lifecycleObserver];
    if (_latest) CFRelease(_latest);
}
- (void)finish {
    if (self.finished) return;
    self.finished = YES;
    self.callback = nil; // Release C++ captures before waking their owning thread.
    self.conversionPaused = nil;
    self.frameConverter = nil;
    if (self.lifecycleObserver) [[NSNotificationCenter defaultCenter] removeObserver:self.lifecycleObserver];
    self.lifecycleObserver = nil;
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
    if (!self.callback(sample)) [self finish];
}
- (void)startHeartbeat {
    self.started = CACurrentMediaTime();
    __weak DPCapture *weakSelf = self;
    dispatch_queue_t queue = self.queue;
    void (^enqueueLifecycle)(void) = ^{
        // Notification delivery never joins an SCK queue or retains the capture.
        dispatch_async(queue, ^{
            DPCapture *s = weakSelf;
            if (s && !s.finished) [s deliver:NULL];
        });
    };
    self.lifecycleObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:DPCaptureLifecycleNotification object:nil queue:nil
        usingBlock:^(NSNotification *notification) { (void)notification; enqueueLifecycle(); }];
    // A context may have arrived before observer registration. Service the
    // already-enqueued contexts immediately instead of losing that first nudge.
    enqueueLifecycle();
    // Idle capture must still observe shutdown/reconfiguration. These are lifecycle
    // ticks, never new pictures: the capture loop seeds a new encoder separately
    // from its retained last real image, without rearming existing encoders.
    self.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, self.queue);
    const uint64_t interval = (self.latest ? 500 : 100) * NSEC_PER_MSEC;
    dispatch_source_set_timer(self.timer, dispatch_time(DISPATCH_TIME_NOW, interval),
                              interval, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(self.timer, ^{
        DPCapture *s = weakSelf;
        if (!s || s.finished) return;
        double now = CACurrentMediaTime();
        if (!s.latest && now - s.started >= 5.0) {
            DP_CAPTURE_LOG(@"DeskPort ScreenCaptureKit: first-frame timeout");
            s.failed = YES;
            [s finish];
            return;
        }
        [s deliver:NULL];
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
                DP_CAPTURE_LOG(@"DeskPort ScreenCaptureKit: display unavailable (%@)", error);
                self.failed = YES;
                [self finish];
                return;
            }
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:selected excludingWindows:@[]];
            self.stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
            NSError *outputError = nil;
            if (![self.stream addStreamOutput:self type:SCStreamOutputTypeScreen
                          sampleHandlerQueue:self.queue error:&outputError]) {
                DP_CAPTURE_LOG(@"DeskPort ScreenCaptureKit: output failed (%@)", outputError);
                self.failed = YES;
                [self finish];
                return;
            }
            [self.stream startCaptureWithCompletionHandler:^(NSError *startError) {
                if (startError) dispatch_async(self.queue, ^{
                    if (self.finished) return;
                    DP_CAPTURE_LOG(@"DeskPort ScreenCaptureKit: start failed (%@)", startError);
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
        DP_CAPTURE_LOG(@"DeskPort ScreenCaptureKit: capture stopped (%@)", error);
        self.failed = YES;
        [self finish];
    });
}
- (DPFrameConverter *)conversionContext {
    if (!self.frameConverter) {
        self.frameConverter = [DPFrameConverter new];
        self.frameConverter.targetFormat = self.targetFormat;
        self.frameConverter.hdr = self.hdr;
    }
    return self.frameConverter;
}
- (CMSampleBufferRef)convertSample:(CMSampleBufferRef)sample {
    if (self.targetFormat != kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange)
        return (CMSampleBufferRef)CFRetain(sample);
    DPFrameConverter *converter = [self conversionContext];
    // Reuse warmed native resources after a pause; never compile a second
    // Metal pipeline merely because the consuming encoder resumes first.
    @synchronized (converter) {
        return [converter convertSample:sample];
    }
}
- (CMSampleBufferRef)bufferSample:(CMSampleBufferRef)sample {
    // A shallow sample copy keeps the same IOSurface/timestamps/attachments,
    // without modifying SCK's sample or retaining the capture from its metadata.
    CMSampleBufferRef buffered = NULL;
    if (CMSampleBufferCreateCopy(NULL, sample, &buffered) != noErr) return NULL;
    DPDeferredConversion *pending = [DPDeferredConversion new];
    pending.converter = [self conversionContext];
    CMSetAttachment(buffered, DPDeferredConversionKey, (__bridge CFTypeRef)pending, kCMAttachmentMode_ShouldNotPropagate);
    return buffered;
}
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    if (self.finished || type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample)) return;
    NSArray *attachments = (__bridge NSArray *)CMSampleBufferGetSampleAttachmentsArray(sample, false);
    NSNumber *status = attachments.firstObject[SCStreamFrameInfoStatus];
    if (self.diagnostics) {
        double now = CACurrentMediaTime();
        if (!self.statsAt) self.statsAt = now;
        if (status && status.integerValue == SCFrameStatusComplete) ++self.completeFrames;
        if (status && status.integerValue == SCFrameStatusIdle) ++self.idleFrames;
        NSArray *dirty = attachments.firstObject[SCStreamFrameInfoDirtyRects];
        if ([dirty isKindOfClass:[NSArray class]] && dirty.count == 0) ++self.emptyDamageFrames;
        if (now - self.statsAt >= 5.0) {
            DP_CAPTURE_LOG(@"DeskPort capture updates: complete=%lu idle=%lu empty_damage=%lu", (unsigned long)self.completeFrames,
                  (unsigned long)self.idleFrames, (unsigned long)self.emptyDamageFrames);
            self.completeFrames = self.idleFrames = self.emptyDamageFrames = 0;
            self.statsAt = now;
        }
    }
    // Idle samples have no new surface. Never map or compare their pixels.
    if (!status || (status.integerValue != SCFrameStatusComplete && status.integerValue != SCFrameStatusStarted) ||
        !CMSampleBufferGetImageBuffer(sample)) return;
    const BOOL deferred = self.targetFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange &&
        self.conversionPaused && self.conversionPaused();
    CMSampleBufferRef converted = deferred ? [self bufferSample:sample] : [self convertSample:sample];
    if (!converted) {
        DP_CAPTURE_LOG(@"DeskPort capture: pixel format/color contract failed (HDR=%d)", self.hdr);
        self.failed = YES;
        [self finish];
        return;
    }
    // Once a real surface exists, lifecycle polling needs only 2 Hz. Real frames
    // still reach the callback immediately; ticks never replay the retained image.
    if (!self.latest && self.timer)
        dispatch_source_set_timer(self.timer, dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC),
                                  500 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    if (self.latest) CFRelease(self.latest);
    self.latest = converted;
    [self deliver:converted];
}
@end

@interface DPScreenVideo ()
@property(nonatomic, readwrite) BOOL hdrAvailable;
@property(nonatomic, strong) dispatch_queue_t captureQueue;
@property(nonatomic, strong) NSMutableDictionary<NSValue *, DPCapture *> *captures;
@end

@implementation DPScreenVideo
- (id)initWithDisplay:(CGDirectDisplayID)displayID frameRate:(int)frameRate {
    self = [super init]; // Do not start the legacy AVFoundation session.
    if (!self) return nil;
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
    int width = 0, height = 0, scale = 0;
    if (getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
        if (!dp_admitted_display_mode(displayID, &width, &height, &scale)) {
            if (mode) CFRelease(mode);
            return nil;
        }
    } else if (mode) {
        width = (int)CGDisplayModeGetPixelWidth(mode);
        height = (int)CGDisplayModeGetPixelHeight(mode);
    } else return nil;
    self.displayID = displayID;
    self.hdrAvailable = dp_source_hdr(displayID);
    self.pixelFormat = kCVPixelFormatType_32BGRA;
    self.frameWidth = width;
    self.frameHeight = height;
    self.minFrameDuration = CMTimeMake(1, MAX(1, frameRate));
    if (mode) CFRelease(mode);
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
    return [self capture:callback conversionPaused:nil];
}
- (dispatch_semaphore_t)capture:(FrameCallbackBlock)callback conversionPaused:(BOOL (^)(void))paused {
    SCStreamConfiguration *config = dp_capture_configuration(self.pixelFormat, self.hdrAvailable && self.hdrCaptureAllowed);
    if (!config) {
        DP_CAPTURE_LOG(@"DeskPort capture: unsupported pixel format/OS %u", self.pixelFormat);
        return nil;
    }
    config.width = self.frameWidth;
    config.height = self.frameHeight;
    config.minimumFrameInterval = self.minFrameDuration;
    config.queueDepth = 3;
    config.showsCursor = YES;
    config.capturesAudio = NO;
    config.preservesAspectRatio = YES;
    DPCapture *capture = [[DPCapture alloc] init];
    capture.targetFormat = self.pixelFormat;
    capture.hdr = self.hdrAvailable && self.hdrCaptureAllowed && self.pixelFormat == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
    capture.queue = self.captureQueue;
    capture.signal = dispatch_semaphore_create(0);
    capture.callback = callback;
    capture.conversionPaused = paused;
    dispatch_sync(self.captureQueue, ^{
        self.captures[[NSValue valueWithPointer:(__bridge const void *)capture.signal]] = capture;
        [capture startDisplay:self.displayID configuration:config];
    });
    DP_CAPTURE_LOG(@"DeskPort capture: ScreenCaptureKit native updates, no CPU pixel comparison");
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
        }
    });
}
@end
