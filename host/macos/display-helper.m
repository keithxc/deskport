// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime declarations for the private CoreGraphics virtual-display API.
// No framebuffer capture or input injection is performed by this helper.
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import "session-topology.h"
#import <ApplicationServices/ApplicationServices.h>
#import <signal.h>
#import <time.h>
#import "text-caret.h"

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
static CGSize displayPhysicalSize;
static NSInteger displayCreatedScale;
static unsigned generation;
static int requestSequence;
static NSInteger requestedScale = 1;
static NSInteger lastWidth, lastHeight, lastScale = 1;
static BOOL rollingBack;
static BOOL sessionActive;
static dispatch_source_t caretTimer;
static unsigned caretGeneration;
static NSDictionary *lastCaret;
static uint64_t caretRefreshDue;
static NSString *sessionLayoutWarning;
static unsigned restoreAttempt;
static CFAbsoluteTime requestStarted;
static unsigned readyGeneration;
static BOOL displayRebuilt;
static void resetCaret(void) {
    lastCaret = nil;
    caretRefreshDue = 0;
}
static BOOL sendCaret(NSDictionary *value, uint64_t now) {
    // Invalid geometry needs only one notification. Refresh unchanged valid
    // geometry after 400 ms, safely before the client's one-second expiry.
    if (lastCaret && [value isEqualToDictionary:lastCaret] &&
        (![value[@"valid"] boolValue] || now < caretRefreshDue)) return NO;
    lastCaret = [value copy];
    caretRefreshDue = now + 400 * NSEC_PER_MSEC;
    NSData *data = [NSJSONSerialization dataWithJSONObject:@{@"caret":value} options:0 error:nil];
    fwrite(data.bytes, 1, data.length, stdout); fputc('\n', stdout); fflush(stdout);
    return YES;
}
static void setSessionActive(BOOL active) {
    sessionActive = active;
    if (!active) {
        ++caretGeneration;
        if (caretTimer) dispatch_source_cancel(caretTimer);
        caretTimer = nil;
        resetCaret();
        return;
    }
    if (caretTimer) return;
    resetCaret();
    const unsigned token = ++caretGeneration;
    caretTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    // Sample immediately for each lease and retain 200 ms change detection.
    dispatch_source_set_timer(caretTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
                              200 * NSEC_PER_MSEC, 40 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(caretTimer, ^{
        if (!sessionActive || token != caretGeneration) return;
        NSDictionary *value = textCaret(display.displayID);
        sendCaret(value, clock_gettime_nsec_np(CLOCK_MONOTONIC));
    });
    dispatch_resume(caretTimer);
}
static void configure(NSInteger width, NSInteger height, NSInteger scale, int sequence, BOOL session);
// WindowServer needs a few run-loop turns to register a freshly created virtual
// display. Until it does, CGDisplayMirrorsDisplay answers the 0xFFFFFFFF
// sentinel rather than 0, so a raw check reads an unregistered display as a
// mirror sink and the detach transaction fails with kCGErrorIllegalArgument. An
// unregistered display carries no topology; report it independent and let the
// bounded mode retries wait for registration instead.
static BOOL displayRegistered(CGDirectDisplayID ident) {
    return ident && ident != (CGDirectDisplayID)-1 && CGDisplayIsOnline(ident) > 0;
}
static CGDirectDisplayID mirrorSource(CGDirectDisplayID ident) {
    if (!displayRegistered(ident)) return kCGNullDirectDisplay;
    const CGDirectDisplayID source = CGDisplayMirrorsDisplay(ident);
    return source == (CGDirectDisplayID)-1 ? kCGNullDirectDisplay : source;
}
static void discardDisplay(void) {
    resetCaret();
    display = nil;
    displayPhysicalSize = CGSizeZero;
    displayCreatedScale = 0;
}
// Report native Retina (~220 PPI) or standard (~110 PPI) square-pixel physical
// dimensions so WindowServer marks the requested scale as the display's default
// mode instead of bringing a HiDPI workspace online in 1x first.
static CGSize physicalSizeForMode(NSInteger width, NSInteger height, NSInteger scale) {
    const double ppi = scale == 2 ? 220.0 : 110.0;
    const double mmPerPixel = 25.4 / ppi;
    return CGSizeMake(fmax(1.0, round((double)width * mmPerPixel)),
                      fmax(1.0, round((double)height * mmPerPixel)));
}
static BOOL physicalSizeCompatible(CGSize physicalMM, NSInteger width, NSInteger height, NSInteger scale) {
    if (physicalMM.width <= 0 || physicalMM.height <= 0 || width <= 0 || height <= 0) return NO;
    const double ppiX = (double)width / (physicalMM.width / 25.4);
    const double ppiY = (double)height / (physicalMM.height / 25.4);
    const double minPpi = scale == 2 ? 180.0 : 80.0;
    const double maxPpi = scale == 2 ? 280.0 : 140.0;
    if (ppiX < minPpi || ppiX > maxPpi || ppiY < minPpi || ppiY > maxPpi) return NO;
    const double ratio = ppiX / ppiY;
    return ratio >= 0.85 && ratio <= 1.18;
}
static BOOL shouldRecreateDisplay(NSInteger width, NSInteger height, NSInteger scale) {
    if (!display) return NO;
    if (scale != displayCreatedScale || !physicalSizeCompatible(displayPhysicalSize, width, height, scale)) return YES;
    // On primary-workspace policies there is no separate extended screen to
    // evacuate windows onto. Replacing the virtual display on geometry changes
    // keeps physical DPI exact and gives macOS system overlays that key off
    // display identity (e.g. macOS 27's Siri AI / Apps launcher) a fresh screen
    // transition so they recenter cleanly without relying on private OS hooks.
    return !isolatedPolicy() && sessionDisplayPolicy != DP_DISPLAY_EXTEND;
}
// Separate HiDPI (2x) and standard (1x) virtual display identities so
// WindowServer's permanent display store never replays a cached 1x mode onto a
// 2x workspace (or reuses legacy productID=1 entries saved with fixed mm).
static unsigned int virtualDisplayProductID(NSInteger scale) {
    if (isolatedDisplay()) return 2;
    return scale == 2 ? 3 : 4;
}
static CGVirtualDisplay *createVirtualDisplay(NSInteger width, NSInteger height, NSInteger scale) {
    CGVirtualDisplayDescriptor *descriptor = [CGVirtualDisplayDescriptor new];
    descriptor.queue = dispatch_get_main_queue();
    descriptor.name = isolatedDisplay() ? @"DeskPort Temporary Workspace" : @"DeskPort Workspace";
    descriptor.maxPixelsWide = 7680; descriptor.maxPixelsHigh = 4320;
    descriptor.sizeInMillimeters = physicalSizeForMode(width, height, scale);
    descriptor.vendorID = 0x4450; descriptor.productID = virtualDisplayProductID(scale);
    descriptor.serialNum = isolatedDisplay() ?
        (unsigned int)[NSProcessInfo.processInfo.environment[@"DESKPORT_DISPLAY_SERIAL"] longLongValue] : 1;
    CGVirtualDisplay *created = [[CGVirtualDisplay alloc] initWithDescriptor:descriptor];
    displayPhysicalSize = created ? descriptor.sizeInMillimeters : CGSizeZero;
    displayCreatedScale = created ? scale : 0;
    return created;
}
// A mirror set that refuses to detach must not wedge sharing for the lifetime of
// this helper. Discard the virtual display once per request so the retry builds
// a fresh one, instead of answering an error the host can only surface as a stop.
static BOOL rebuildDisplay(NSInteger width, NSInteger height, NSInteger scale, int sequence, BOOL session, CGError error) {
    if (displayRebuilt) return NO;
    displayRebuilt = YES;
    fprintf(stderr,"DeskPort rebuild seq=%d: dedicated display could not be separated (%d); recreating it\n",sequence,error);
    discardDisplay();
    const unsigned token = ++generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        if (token == generation) configure(width, height, scale, sequence, session);
    });
    return YES;
}
static NSArray *displayModes(NSInteger width, NSInteger height, NSInteger scale) {
    return @[[[CGVirtualDisplayMode alloc] initWithWidth:(unsigned)width / scale
        height:(unsigned)height / scale refreshRate:60.0]];
}
static BOOL applyMode(NSInteger width, NSInteger height, NSInteger scale) {
    if (!display) return NO;
    resetCaret();
    CGVirtualDisplaySettings *settings = [CGVirtualDisplaySettings new];
    settings.hiDPI = scale == 2;
    settings.modes = displayModes(width, height, scale);
    return [display applySettings:settings];
}
static void respond(NSDictionary *value) {
    fprintf(stderr,"DeskPort display seq=%d session=%d result=%s\n",requestSequence,sessionActive,[value description].UTF8String);
    NSMutableDictionary *response = [value mutableCopy];
    if (requestStarted) response[@"modeElapsedMs"] = @((long)((CFAbsoluteTimeGetCurrent() - requestStarted) * 1000));
    if (requestSequence) response[@"seq"] = @(requestSequence);
    NSData *json = [NSJSONSerialization dataWithJSONObject:response options:0 error:nil];
    fwrite(json.bytes, 1, json.length, stdout); fputc('\n', stdout); fflush(stdout);
}
// Persist fallback mode corrections so a stale WindowServer permanent entry
// (e.g. after a manual Display Settings change) heals itself on next connect.
static void persistDisplayMode(CGDirectDisplayID ident, CGDisplayModeRef mode) {
    CGDisplayConfigRef config = NULL;
    CGError result = CGBeginDisplayConfiguration(&config);
    if (result == kCGErrorSuccess) {
        result = CGConfigureDisplayWithDisplayMode(config, ident, mode, NULL);
        const CGConfigureOption option = isolatedDisplay() ? kCGConfigureForSession : kCGConfigurePermanently;
        if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(config, option);
        else CGCancelDisplayConfiguration(config);
    }
    if (result != kCGErrorSuccess) CGDisplaySetDisplayMode(ident, mode, NULL);
}
static void waitForMode(NSInteger width, NSInteger height, unsigned token, unsigned attempt) {
    if (token != generation) return;
    // Mirror membership can be restored asynchronously after applySettings.
    // Verify independence here before reporting a capture ID to Sunshine.
    CGDirectDisplayID source = mirrorSource(display.displayID);
    if (source) {
        CGDisplayConfigRef config;
        CGError result = CGBeginDisplayConfiguration(&config);
        if (result == kCGErrorSuccess) {
            result = CGConfigureDisplayMirrorOfDisplay(config, display.displayID, kCGNullDirectDisplay);
            if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(config, kCGConfigureForSession);
            else CGCancelDisplayConfiguration(config);
        }
        if (result != kCGErrorSuccess || attempt >= 30) {
            if (rebuildDisplay(width, height, requestedScale, requestSequence, sessionActive, result)) return;
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
    if (attempt%10==0) fprintf(stderr,"DeskPort mode attempt=%u requested=%ldx%ld@%ld actual=%zux%zu/%zux%zu active=%d main=%u\n",attempt,(long)width,(long)height,(long)requestedScale,current?CGDisplayModeGetWidth(current):0,current?CGDisplayModeGetHeight(current):0,current?CGDisplayModeGetPixelWidth(current):0,current?CGDisplayModeGetPixelHeight(current):0,CGDisplayIsActive(capture),CGMainDisplayID());
    if (attempt==10 && !ready) {
        // Name what the virtual display offers when the requested mode is missing.
        CFArrayRef offered = CGDisplayCopyAllDisplayModes(capture,
            (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes: @YES});
        for (CFIndex i = 0; offered && i < CFArrayGetCount(offered) && i < 16; ++i) {
            CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(offered, i);
            fprintf(stderr,"DeskPort mode offered=%zux%zu/%zux%zu\n",CGDisplayModeGetWidth(mode),CGDisplayModeGetHeight(mode),
                CGDisplayModeGetPixelWidth(mode),CGDisplayModeGetPixelHeight(mode));
        }
        if (offered) CFRelease(offered);
    }
    if (current) CFRelease(current);
    if (!ready && !source) {
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(display.displayID,
            (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayShowDuplicateLowResolutionModes: @YES});
        if (modes) {
            for (CFIndex i = 0; i < CFArrayGetCount(modes); ++i) {
                CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
                if (CGDisplayModeGetPixelWidth(mode) == width && CGDisplayModeGetPixelHeight(mode) == height &&
                    CGDisplayModeGetWidth(mode) == width / requestedScale && CGDisplayModeGetHeight(mode) == height / requestedScale) {
                    persistDisplayMode(display.displayID, mode);
                    break;
                }
            }
            CFRelease(modes);
        }
    }
    if (ready && sessionActive && !sessionLayoutWarning && !sessionTopologyReady(display.displayID)) {
        if (attempt >= 30 || !applySessionTopology(display.displayID)) {
            // Local mirror/primary/disable failures must not discard a verified
            // independent workspace. Retain recovery and describe the deviation.
            sessionLayoutWarning=@"The client workspace is ready, but the local display layout could not be fully applied";
            recordLayoutEvent(sessionLayoutWarning);
        } else ready=NO;
    }
    if (ready && readyGeneration != token) {
        // Confirm on another run-loop turn: mode/mirror changes are asynchronous.
        readyGeneration = token;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            waitForMode(width, height, token, attempt + 1);
        });
        return;
    }
    if (!ready) readyGeneration = 0;
    if (ready) {
        lastWidth = width; lastHeight = height; lastScale = requestedScale;
        displayRebuilt = NO;
        if (rollingBack) {
            rollingBack = NO;
            respond(@{@"error": @"Requested mode was rejected; the previous display mode was restored"}); return;
        }
        NSMutableDictionary *result=[@{@"displayId": @(capture), @"virtualDisplayId": @(display.displayID),
            @"mirrored": @(sessionActive && sessionDisplayPolicy==DP_DISPLAY_PRIMARY_MIRROR && sessionTopologyReady(capture)), @"scale": @(requestedScale), @"width": @(width), @"height": @(height)} mutableCopy];
        if (sessionLayoutWarning) result[@"layoutWarning"]=sessionLayoutWarning;
        respond(result);
    } else if (attempt < 30) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            waitForMode(width, height, token, attempt + 1);
        });
    } else {
        if (lastWidth && !rollingBack) {
            rollingBack = YES; requestedScale = lastScale;
            const unsigned restoreToken = ++generation;
            if (!physicalSizeCompatible(displayPhysicalSize, lastWidth, lastHeight, lastScale)) {
                discardDisplay();
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    if (restoreToken != generation) return;
                    display = createVirtualDisplay(lastWidth, lastHeight, lastScale);
                    if (!applyMode(lastWidth, lastHeight, lastScale)) {
                        rollingBack = NO;
                        respond(@{@"error": @"Requested mode was rejected; could not restore the previous display mode"});
                        return;
                    }
                    waitForMode(lastWidth, lastHeight, restoreToken, 0);
                });
                return;
            }
            applyMode(lastWidth, lastHeight, lastScale);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                waitForMode(lastWidth, lastHeight, restoreToken, 0);
            });
            return;
        }
        respond(@{@"error": source ? @"Mirroring is active: choose the mirror source's HiDPI resolution" :
            @"Virtual display did not reach the requested HiDPI mode"});
    }
}
static void configure(NSInteger width, NSInteger height, NSInteger scale, int sequence, BOOL session) {
    fprintf(stderr,"DeskPort configure seq=%d session=%d %ldx%ld@%ld\n",sequence,session,(long)width,(long)height,(long)scale);
    requestSequence = sequence; requestedScale = scale; rollingBack = NO; sessionLayoutWarning=nil;
    requestStarted = CFAbsoluteTimeGetCurrent();
    if (width < 640 || height < 360 || width > 7680 || height > 4320 || width % 2 || height % 2 || (scale != 1 && scale != 2)) {
        respond(@{@"error": @"Use an even pixel size between 640x360 and 7680x4320"}); return;
    }
    if (session && sessionDisplayPolicy==DP_DISPLAY_PRIMARY_ONLY && !enableDisplayFunction()) {
        respond(@{@"error": @"Disabling other screens is unavailable on this macOS version"}); return;
    }
    if (!session) {
        setSessionActive(NO);
        const unsigned token=++generation;
        // Preserve the journal through removal: removing a mirror source can
        // change the fallback physical display's mode after the first restore.
        preserveTopologyJournal=YES;
        restoreTopology(display.displayID);
        discardDisplay();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,500*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{
            if (token!=generation) return;
            preserveTopologyJournal=NO;
            if (!restoreTopology(0)) {
                if (++restoreAttempt < 30) { configure(width,height,scale,sequence,NO); return; }
                respond(@{@"active":@NO,@"error":@"Physical layout recovery is retained for repair"});
                return;
            }
            restoreAttempt=0;
            respond(@{@"active":@NO,@"width":@0,@"height":@0,@"scale":@1});
        });
        return;
    }
    preserveTopologyJournal=NO;
    restoreAttempt=0;
    if (session && savedTopology && !sessionActive) {
        // The authenticated new lease owns the workspace now. Cancel idle retry
        // callbacks, retain the original journal and verify this client's mode.
        ++generation;
        recordLayoutEvent(@"New client takes over the workspace; original local layout recovery is retained");
    }
    if (session && !snapshotTopology(display.displayID)) {
        respond(@{@"error": @"Could not save the original display layout"}); return;
    }
    setSessionActive(session);
    // Repeated requests still verify the actual OS mode; cached dimensions alone
    // are not proof that a display remains active, independent, and correctly scaled.
    if (display && displayRegistered(display.displayID) && CGDisplayIsActive(display.displayID) &&
        !mirrorSource(display.displayID)) {
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID);
        BOOL same = mode && CGDisplayModeGetPixelWidth(mode) == width && CGDisplayModeGetPixelHeight(mode) == height &&
            CGDisplayModeGetWidth(mode) == width / scale && CGDisplayModeGetHeight(mode) == height / scale;
        if (mode) CFRelease(mode);
        if (same) { waitForMode(width, height, ++generation, 0); return; }
    }
    if (shouldRecreateDisplay(width, height, scale)) {
        fprintf(stderr,"DeskPort recreate seq=%d: replacing virtual display for %ldx%ld@%ld\n",
            sequence,(long)width,(long)height,(long)scale);
        discardDisplay();
        const unsigned token = ++generation;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            if (token == generation) configure(width, height, scale, sequence, session);
        });
        return;
    }
    if (!display) {
        if (!savedTopology && !snapshotTopology(0)) {
            respond(@{@"error": @"Could not save the original display layout"}); return;
        }
        if (!NSClassFromString(@"CGVirtualDisplay")) {
            respond(@{@"error": @"Virtual displays are unavailable on this macOS version"}); return;
        }
        display = createVirtualDisplay(width, height, scale);
    }
    // The virtual display must be the source, not a sink in an old mirror set.
    // Physical displays join it only after the requested mode has settled.
    if (display && mirrorSource(display.displayID)) {
        CGDisplayConfigRef config;
        if (CGBeginDisplayConfiguration(&config) != kCGErrorSuccess) {
            respond(@{@"error": @"Could not configure the dedicated display"}); return;
        }
        CGError result = CGConfigureDisplayMirrorOfDisplay(config, display.displayID, kCGNullDirectDisplay);
        if (result == kCGErrorSuccess) result = CGCompleteDisplayConfiguration(config, kCGConfigureForSession);
        else CGCancelDisplayConfiguration(config);
        if (result != kCGErrorSuccess) {
            if (rebuildDisplay(width, height, scale, sequence, session, result)) return;
            respond(@{@"error": @"Could not separate the dedicated display"}); return;
        }
    }
    if (!applyMode(width, height, scale)) {
        respond(@{@"error": @"macOS rejected the virtual display mode"}); return;
    }
    const unsigned token = ++generation;
    // Check immediately, then retain the bounded 100 ms verification retries.
    waitForMode(width, height, token, 0);
}
static void finishHelper(void) {
    static BOOL stopping;
    if (stopping) return;
    stopping=YES; setSessionActive(NO); ++generation;
    if (display) snapshotTopology(display.displayID);
    preserveTopologyJournal=YES;
    restoreTopology(display.displayID);
    // Removing the virtual source can itself reset the fallback monitor's mode.
    // Keep the journal until removal is observed, then restore the physical mode.
    discardDisplay();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,500*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{
        preserveTopologyJournal=NO;
        restoreTopology(0);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,250*NSEC_PER_MSEC),dispatch_get_main_queue(), ^{ exit(restoreTopology(0) ? 0 : 1); });
    });
}
static void displayReconfigured(CGDirectDisplayID ident, CGDisplayChangeSummaryFlags flags, void *context) {
    // Subscribe to WindowServer changes so CoreGraphics refreshes its mode cache
    // after mirror/layout transactions, including changes made by this process.
}
int main(int argc, const char *argv[]) {
    @autoreleasepool {
        signal(SIGPIPE,SIG_IGN);
        if (argc == 2 && !strcmp(argv[1], "--probe")) {
            respond(@{@"available": @(NSClassFromString(@"CGVirtualDisplay") != nil)}); return 0;
        }
        if (argc != 3) return 2;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        CGDisplayRegisterReconfigurationCallback(displayReconfigured,NULL);
        NSData *recovery=isolatedDisplay() ? nil : [NSData dataWithContentsOfFile:topologyPath()];
        if (recovery) {
            id entries=[NSJSONSerialization JSONObjectWithData:recovery options:0 error:nil];
            if (![entries isKindOfClass:NSArray.class]) return 1;
            if ([entries count]>64) return 1;
            for (id entry in entries) {
                if (![entry isKindOfClass:NSDictionary.class] || ![entry[@"uuid"] isKindOfClass:NSString.class] ||
                    ![entry[@"mirror"] isKindOfClass:NSString.class]) return 1;
                if (entry[@"enabled"] && ![@[@YES,@NO] containsObject:entry[@"enabled"]]) return 1;
                for (NSString *key in @[@"main",@"x",@"y",@"width",@"height",@"pixelsW",@"pixelsH",@"mode",@"hz"])
                    if (![entry[key] isKindOfClass:NSNumber.class] || !isfinite([entry[key] doubleValue])) return 1;
            }
            savedTopology=entries;
            BOOL restored=NO;
            for (unsigned attempt=0;attempt<30 && !restored;attempt++) {
                restored=restoreTopology(0);
                if (!restored) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
            }
            if (!restored) recordLayoutEvent(@"Startup local layout recovery is pending; keeping the host available for new clients");
        }
        // A running helper is ready to accept leases, not an active display.
        // Startup recovery above may restore an old journal but creates nothing.
        respond(@{@"ready":@YES,@"active":@NO});
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            char *line = NULL; size_t length = 0;
            while (getline(&line, &length, stdin) != -1) {
                NSData *data = [[NSString stringWithUTF8String:line] dataUsingEncoding:NSUTF8StringEncoding];
                id request = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
                if ([request isKindOfClass:[NSDictionary class]]) {
                    if ([request[@"parked"] boolValue]) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            if (sessionActive) {
                                ++caretGeneration;
                                if (caretTimer) dispatch_source_cancel(caretTimer);
                                caretTimer = nil;
                                resetCaret();
                            }
                        });
                        continue;
                    }
                    NSInteger width = [request[@"width"] integerValue], height = [request[@"height"] integerValue];
                    NSInteger scale = [request[@"scale"] integerValue];
                    int sequence = [request[@"seq"] intValue];
                    dispatch_async(dispatch_get_main_queue(), ^{
                        id value=request[@"displayPolicy"];
                        int policy=[value isKindOfClass:NSNumber.class] ? [value intValue] : DP_DISPLAY_PRIMARY_MIRROR;
                        BOOL session=[request[@"session"] boolValue];
                        if (isolatedPolicy()) policy=DP_DISPLAY_EXTEND;
                        if ((value && (![value isKindOfClass:NSNumber.class] || ![@[@0,@1,@2] containsObject:value])) ||
                            (session && sessionActive && policy!=sessionDisplayPolicy)) {
                            requestSequence=sequence; respond(@{@"error":@"Invalid or changed session display policy"}); return;
                        }
                        if (session) sessionDisplayPolicy=isolatedPolicy() ? DP_DISPLAY_EXTEND : policy;
                        displayRebuilt=NO;
                        configure(width, height, scale ?: 1, sequence, session);
                    });
                }
            }
            free(line); dispatch_async(dispatch_get_main_queue(), ^{ finishHelper(); });
        });
        // Idle topology recovery uses configure's bounded retries and the startup
        // recovery loop above; it does not require a permanent caret polling timer.
        signal(SIGTERM,SIG_IGN); signal(SIGINT,SIG_IGN);
        dispatch_source_t terminate=dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,SIGTERM,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(terminate, ^{ finishHelper(); }); dispatch_resume(terminate);
        dispatch_source_t interrupt=dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL,SIGINT,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(interrupt, ^{ finishHelper(); }); dispatch_resume(interrupt);
        [NSApp run];
    }
    return 0;
}
