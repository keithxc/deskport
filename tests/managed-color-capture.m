// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in native test: only the caller's isolated virtual output is captured.
#import "host/macos/screen-video.m"
#import <AppKit/AppKit.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
@interface DPColorCard : NSView
- (BOOL)renderGradient:(CGSize)size;
@end
@implementation DPColorCard
- (BOOL)renderGradient:(CGSize)size {
    CAMetalLayer *layer = [CAMetalLayer layer]; layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatRGBA16Float; layer.framebufferOnly = NO;
    layer.wantsExtendedDynamicRangeContent = YES;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
    layer.colorspace = space; CFRelease(space);
    layer.frame = self.bounds; layer.contentsScale = size.width / self.bounds.size.width; layer.drawableSize = size;
    self.layer = layer; self.wantsLayer = YES;
    NSString *source = @"#include <metal_stdlib>\nusing namespace metal; kernel void gradient(texture2d<half,access::write> target [[texture(0)]],uint2 p [[thread_position_in_grid]]) { if(p.x>=target.get_width()||p.y>=target.get_height())return; half v=half(float(p.x)/float(target.get_width()-1));target.write(half4(v,v,v,half(1.0)),p);}";
    NSError *error = nil;
    id<MTLLibrary> library = [layer.device newLibraryWithSource:source options:nil error:&error];
    id<MTLComputePipelineState> pipeline = [layer.device newComputePipelineStateWithFunction:[library newFunctionWithName:@"gradient"] error:&error];
    id<CAMetalDrawable> drawable = [layer nextDrawable]; if (!pipeline || !drawable) return NO;
    id<MTLCommandQueue> queue = [layer.device newCommandQueue]; id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline]; [encoder setTexture:drawable.texture atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(((NSUInteger)size.width+15)/16,((NSUInteger)size.height+15)/16,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
    [encoder endEncoding]; [command presentDrawable:drawable]; [command commit]; [command waitUntilCompleted];
    return command.status == MTLCommandBufferStatusCompleted;
}
@end
static void pump(double seconds) {
    double end = CACurrentMediaTime() + seconds;
    while (CACurrentMediaTime() < end) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]
            inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
}
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 1) {
            CGDirectDisplayID ids[128]; uint32_t count = 0;
            if (CGGetActiveDisplayList(128, ids, &count) != kCGErrorSuccess) return 1;
            NSMutableArray *topology = [NSMutableArray array];
            for (unsigned i = 0; i < count; ++i) {
                // WindowServer can re-enumerate the headless fallback display
                // when the last virtual screen disappears. Compare its stable
                // display identity and the complete observed geometry.
                CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(ids[i]);
                if (!uuid) return 1;
                NSString *identity = CFBridgingRelease(CFUUIDCreateString(NULL, uuid));
                CFRelease(uuid);
                CGRect bounds = CGDisplayBounds(ids[i]); CGDisplayModeRef mode = CGDisplayCopyDisplayMode(ids[i]);
                [topology addObject:@[identity, @(CGDisplayIsMain(ids[i])), @(bounds.origin.x), @(bounds.origin.y), @(bounds.size.width), @(bounds.size.height),
                    @(mode ? CGDisplayModeGetPixelWidth(mode) : 0), @(mode ? CGDisplayModeGetPixelHeight(mode) : 0)]];
                if (mode) CFRelease(mode);
            }
            NSData *json = [NSJSONSerialization dataWithJSONObject:topology options:0 error:NULL];
            puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            return 0;
        }
        if (argc != 4 || !getenv("DESKPORT_CAPTURE_DISPLAY_FILE") || !CGPreflightScreenCaptureAccess()) return 77;
        CGDirectDisplayID ident = (CGDirectDisplayID)strtoul(argv[1], NULL, 10);
        unsigned width = (unsigned)strtoul(argv[2], NULL, 10), height = (unsigned)strtoul(argv[3], NULL, 10);
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; [NSApp finishLaunching];
        NSScreen *selected = nil;
        for (int retry = 0; retry < 100 && !selected; ++retry) {
            for (NSScreen *screen in NSScreen.screens)
                if ([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == ident) { selected = screen; break; }
            if (!selected) pump(0.03);
        }
        if (!selected) return 1;
        NSWindow *window = [[NSWindow alloc] initWithContentRect:selected.frame styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO; window.opaque = YES; window.backgroundColor = NSColor.blackColor;
        window.level = NSScreenSaverWindowLevel; window.ignoresMouseEvents = YES;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary;
        window.contentView = [[DPColorCard alloc] initWithFrame:NSMakeRect(0, 0, selected.frame.size.width, selected.frame.size.height)];
        [window orderFrontRegardless]; pump(0.1);
        if (![(DPColorCard *)window.contentView renderGradient:CGSizeMake(width, height)]) { [window close]; return 1; }
        pump(0.5);
        DPScreenVideo *capture = [[DPScreenVideo alloc] initWithDisplay:ident frameRate:30];
        if (!capture || capture.hdrAvailable) { [window close]; return 1; }
        capture.hdrCaptureAllowed = YES;
        capture.pixelFormat = kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange;
        [capture setFrameWidth:width frameHeight:height];
        __block NSDictionary *result = nil;
        dispatch_semaphore_t signal = [capture capture:^bool(CMSampleBufferRef sample) {
            if (!sample) return true;
            CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
            BOOL valid = CVPixelBufferGetPixelFormatType(pixels) == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange &&
                CVPixelBufferGetWidth(pixels) == width && CVPixelBufferGetHeight(pixels) == height;
            unsigned unique = 0, fractional = 0; bool seen[1024] = {false};
            if (valid && CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixels, 0) + (height/2) * CVPixelBufferGetBytesPerRowOfPlane(pixels, 0));
                for (unsigned x = 0; x < width; ++x) { unsigned y = row[x] >> 6; if (!seen[y]) { seen[y] = true; ++unique; } fractional += (y & 3) != 0; }
                CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
            }
            result = @{ @"validP010": @(valid), @"width": @(width), @"height": @(height), @"uniqueLumaCodes": @(unique),
                @"fractionalLumaSamples": @(fractional), @"activeHDR": @NO };
            return false;
        }];
        BOOL completed = NO;
        if (signal) for (int attempt = 0; attempt < 600; ++attempt) {
            if (dispatch_semaphore_wait(signal, DISPATCH_TIME_NOW) == 0) { completed = YES; break; }
            pump(0.02);
        }
        BOOL failed = !signal || !completed || [capture captureFailed:signal];
        if (signal) [capture cancelCapture:signal];
        [window close]; pump(0.05);
        if (!result || failed) return 1;
        NSData *json = [NSJSONSerialization dataWithJSONObject:result options:0 error:NULL];
        puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
        return [result[@"validP010"] boolValue] && [result[@"uniqueLumaCodes"] unsignedIntValue] > 256 ? 0 : 1;
    }
}
