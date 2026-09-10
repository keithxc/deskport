// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime declarations for the private CoreGraphics virtual-display API.
// No framebuffer capture or input injection is performed by this helper.
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

@interface CGVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(unsigned int)width height:(unsigned int)height refreshRate:(double)rate;
@end
@interface CGVirtualDisplayDescriptor : NSObject
@property(nonatomic, retain) dispatch_queue_t queue;
@property(nonatomic, copy) NSString *name;
@property(nonatomic) unsigned int maxPixelsWide;
@property(nonatomic) unsigned int maxPixelsHigh;
@property(nonatomic) CGSize sizeInMillimeters;
@property(nonatomic) unsigned int productID;
@property(nonatomic) unsigned int vendorID;
@property(nonatomic) unsigned int serialNum;
@end
@interface CGVirtualDisplaySettings : NSObject
@property(nonatomic) unsigned int hiDPI;
@property(nonatomic, retain) NSArray *modes;
@end
@interface CGVirtualDisplay : NSObject
- (instancetype)initWithDescriptor:(CGVirtualDisplayDescriptor *)descriptor;
- (BOOL)applySettings:(CGVirtualDisplaySettings *)settings;
@property(nonatomic, readonly) unsigned int displayID;
@end

static CGVirtualDisplay *display;
static unsigned generation;
static int requestSequence;
static NSInteger requestedScale = 2;
static NSInteger lastWidth, lastHeight, lastScale = 2;
static NSArray *displayModes(NSInteger width, NSInteger height, NSInteger scale) {
    return @[[[CGVirtualDisplayMode alloc] initWithWidth:(unsigned)width / scale
        height:(unsigned)height / scale refreshRate:60.0]];
}
static void applyMode(NSInteger width, NSInteger height, NSInteger scale) {
    CGVirtualDisplaySettings *settings = [CGVirtualDisplaySettings new];
    settings.hiDPI = scale == 2;
    settings.modes = displayModes(width, height, scale);
    [display applySettings:settings];
}
static void respond(NSDictionary *value) {
    NSMutableDictionary *response = [value mutableCopy];
    if (requestSequence) response[@"seq"] = @(requestSequence);
    NSData *json = [NSJSONSerialization dataWithJSONObject:response options:0 error:nil];
    fwrite(json.bytes, 1, json.length, stdout); fputc('\n', stdout); fflush(stdout);
}
static void waitForMode(NSInteger width, NSInteger height, unsigned token, unsigned attempt) {
    if (token != generation) return;
    // Mirror membership can be restored asynchronously after applySettings.
    // Verify independence here before reporting a capture ID to Sunshine.
    CGDirectDisplayID source = CGDisplayMirrorsDisplay(display.displayID);
    if (source) {
        CGDisplayConfigRef config;
        CGError result = CGBeginDisplayConfiguration(&config);
        if (result == kCGErrorSuccess) {
            result = CGConfigureDisplayMirrorOfDisplay(config, display.displayID, kCGNullDirectDisplay);
            if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(config, kCGConfigureForSession);
            else CGCancelDisplayConfiguration(config);
        }
        if (result != kCGErrorSuccess || attempt >= 30) {
            respond(@{@"error": @"Could not separate the dedicated display"}); return;
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            waitForMode(width, height, token, attempt + 1);
        });
        return;
    }
    CGDirectDisplayID capture = display.displayID;
    CGDisplayModeRef current = CGDisplayCopyDisplayMode(capture);
    BOOL ready = current && CGDisplayIsActive(capture) &&
        CGDisplayModeGetPixelWidth(current) == width && CGDisplayModeGetPixelHeight(current) == height &&
        CGDisplayModeGetWidth(current) == width / requestedScale && CGDisplayModeGetHeight(current) == height / requestedScale;
    if (current) CFRelease(current);
    if (!ready && !source) {
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(display.displayID,
            (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes: @YES});
        if (modes) {
            for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
                CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
                if (CGDisplayModeGetPixelWidth(mode) == width && CGDisplayModeGetPixelHeight(mode) == height &&
                    CGDisplayModeGetWidth(mode) == width / requestedScale && CGDisplayModeGetHeight(mode) == height / requestedScale) {
                    CGDisplaySetDisplayMode(display.displayID, mode, NULL);
                    break;
                }
            }
            CFRelease(modes);
        }
    }
    if (ready) {
        lastWidth = width; lastHeight = height; lastScale = requestedScale;
        respond(@{@"displayId": @(capture), @"virtualDisplayId": @(display.displayID),
            @"mirrored": @(source != 0), @"width": @(width), @"height": @(height)});
    } else if (attempt < 30) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            waitForMode(width, height, token, attempt + 1);
        });
    } else {
        if (lastWidth) applyMode(lastWidth, lastHeight, lastScale);
        respond(@{@"error": source ? @"Mirroring is active: choose the mirror source's HiDPI resolution" :
            @"Virtual display did not reach the requested HiDPI mode"});
    }
}
static void configure(NSInteger width, NSInteger height, NSInteger scale, int sequence) {
    requestSequence = sequence; requestedScale = scale;
    if (width < 640 || height < 360 || width > 3840 || height > 2160 || width % 2 || height % 2 || (scale != 1 && scale != 2)) {
        respond(@{@"error": @"Use an even pixel size between 640x360 and 3840x2160"}); return;
    }
    if (!display) {
        if (!NSClassFromString(@"CGVirtualDisplay")) {
            respond(@{@"error": @"Virtual displays are unavailable on this macOS version"}); return;
        }
        CGVirtualDisplayDescriptor *descriptor = [CGVirtualDisplayDescriptor new];
        descriptor.queue = dispatch_get_main_queue();
        descriptor.name = @"DeskPort Workspace";
        descriptor.maxPixelsWide = 3840; descriptor.maxPixelsHigh = 2160;
        descriptor.sizeInMillimeters = CGSizeMake(600, 340);
        descriptor.vendorID = 0x4450; descriptor.productID = 1; descriptor.serialNum = 1;
        display = [[CGVirtualDisplay alloc] initWithDescriptor:descriptor];
    }
    // This dedicated display must be its own capture source. Detach only our
    // virtual sink if macOS remembered membership in a third-party mirror set.
    // Never resize or reconfigure BetterDisplay's source or a physical display.
    if (display && CGDisplayMirrorsDisplay(display.displayID)) {
        CGDisplayConfigRef config;
        if (CGBeginDisplayConfiguration(&config) != kCGErrorSuccess) {
            respond(@{@"error": @"Could not configure the dedicated display"}); return;
        }
        CGError result = CGConfigureDisplayMirrorOfDisplay(config, display.displayID, kCGNullDirectDisplay);
        if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(config, kCGConfigureForSession);
        else CGCancelDisplayConfiguration(config);
        if (result != kCGErrorSuccess) { respond(@{@"error": @"Could not separate the dedicated display"}); return; }
    }
    CGVirtualDisplaySettings *settings = [CGVirtualDisplaySettings new];
    settings.hiDPI = scale == 2;
    settings.modes = displayModes(width, height, scale);
    if (!display || ![display applySettings:settings]) {
        respond(@{@"error": @"macOS rejected the virtual display mode"}); return;
    }
    const unsigned token = ++generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        waitForMode(width, height, token, 0);
    });
}
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc == 2 && !strcmp(argv[1], "--probe")) {
            respond(@{@"available": @(NSClassFromString(@"CGVirtualDisplay") != nil)}); return 0;
        }
        if (argc != 3) return 2;
        configure(atoi(argv[1]), atoi(argv[2]), 2, 0);
        if (!display) return 1;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            char *line = NULL; size_t length = 0;
            while (getline(&line, &length, stdin) != -1) {
                NSData *data = [[NSString stringWithUTF8String:line] dataUsingEncoding:NSUTF8StringEncoding];
                id request = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
                if ([request isKindOfClass:[NSDictionary class]]) {
                    NSInteger width = [request[@"width"] integerValue], height = [request[@"height"] integerValue];
                    NSInteger scale = [request[@"scale"] integerValue];
                    int sequence = [request[@"seq"] intValue];
                    dispatch_async(dispatch_get_main_queue(), ^{ configure(width, height, scale ?: 2, sequence); });
                }
            }
            free(line); exit(0);
        });
        [[NSRunLoop mainRunLoop] run];
    }
    return 0;
}
