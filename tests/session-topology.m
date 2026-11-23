// SPDX-License-Identifier: GPL-3.0-or-later
// Run the production topology adapter against an isolated in-memory WindowServer.
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>
#import <dlfcn.h>
#import <assert.h>
static NSMutableDictionary *screens, *pending;
static NSString *testHome;
static BOOL rejectCommit, enableAvailable=YES;
static unsigned modeWrites, sessionModeFallbacks;
static CGConfigureOption lastCommitOption;
static NSMutableDictionary *screen(unsigned i) { return screens[@(i)]; }
static CGError listScreens(uint32_t max, CGDirectDisplayID *ids, uint32_t *count) {
    *count=0; for (NSNumber *n in [[screens allKeys] sortedArrayUsingSelector:@selector(compare:)]) if (*count<max) ids[(*count)++]=n.unsignedIntValue;
    return kCGErrorSuccess;
}
static CGError setEnabled(CGDisplayConfigRef c, CGDirectDisplayID i, bool enabled) { pending[@(i)][@"enabled"]=@(enabled); return 0; }
static void *symbol(void *h,const char *name) {
    if (!strcmp(name,"CGSConfigureDisplayEnabled")) return enableAvailable ? (void *)setEnabled : NULL;
    if (!strcmp(name,"CGSGetDisplayList")) return (void *)listScreens;
    return NULL;
}
static CFUUIDRef fakeUuid(unsigned i) {
    if (!screen(i) || [screen(i)[@"noUuid"] boolValue]) return NULL;
    unsigned stable=[screen(i)[@"identity"] unsignedIntValue] ?: i;
    return CFUUIDCreateFromString(NULL,(__bridge CFStringRef)[NSString stringWithFormat:@"00000000-0000-0000-0000-%012u",stable]);
}
static CGDisplayModeRef fakeMode(unsigned i) { return screen(i) ? (CGDisplayModeRef)CFBridgingRetain([screen(i) copy]) : NULL; }
static NSDictionary *modeData(CGDisplayModeRef m) { return (__bridge NSDictionary *)m; }
static CFArrayRef fakeModes(unsigned i, CFDictionaryRef opts) {
    NSArray *modes=screen(i)[@"nativeModes"];
    if ([screen(i)[@"mirror"] unsignedIntValue]) modes=nil;
    return CFBridgingRetain(modes ?: @[[screen(i) copy]]);
}
static CGError begin(CGDisplayConfigRef *c) { pending=[NSMutableDictionary new]; for (NSNumber *n in screens) pending[n]=[screens[n] mutableCopy]; *c=(CGDisplayConfigRef)1; return 0; }
static CGError mirror(CGDisplayConfigRef c,unsigned i,unsigned source) { pending[@(i)][@"mirror"]=@(source); return 0; }
static CGError origin(CGDisplayConfigRef c,unsigned i,int x,int y) {
    pending[@(i)][@"x"]=@(x); pending[@(i)][@"y"]=@(y);
    if (!x && !y) for (NSNumber *n in pending) pending[n][@"main"]=@([n unsignedIntValue]==i);
    return 0;
}
static CGError setMode(CGDisplayConfigRef c,unsigned i,CGDisplayModeRef m,CFDictionaryRef opts) {
    modeWrites++;
    for (NSString *key in @[@"width",@"height",@"pixelsW",@"pixelsH"]) {
        if (modeData(m)[key]) pending[@(i)][key]=modeData(m)[key];
        else [pending[@(i)] removeObjectForKey:key];
    }
    return 0;
}
static CGError selectMode(unsigned i,CGDisplayModeRef m,CFDictionaryRef opts) {
    sessionModeFallbacks++;
    for (NSString *key in @[@"width",@"height",@"pixelsW",@"pixelsH"]) {
        if (modeData(m)[key]) screen(i)[key]=modeData(m)[key];
        else [screen(i) removeObjectForKey:key];
    }
    return 0;
}
static unsigned mainDisplay(void) { for (NSNumber *n in screens) if ([screen(n.unsignedIntValue)[@"main"] boolValue]) return n.unsignedIntValue; return 0; }
static CGError complete(CGDisplayConfigRef c,CGConfigureOption option) {
    lastCommitOption=option;
    if (rejectCommit) return kCGErrorFailure;
    // Mirrored sinks share the source's origin but do not become the main source.
    for (NSNumber *ident in pending) {
        unsigned source=[pending[ident][@"mirror"] unsignedIntValue];
        if (source && [pending[ident][@"main"] boolValue]) {
            pending[ident][@"main"]=@NO; pending[@(source)][@"main"]=@YES;
        }
    }
    screens=pending; pending=nil; return 0;
}
static CGError cancel(CGDisplayConfigRef c) { pending=nil; return 0; }
static CGRect fakeBounds(unsigned i) { return CGRectMake([screen(i)[@"x"] intValue],[screen(i)[@"y"] intValue],1920,1080); }
#define dlsym symbol
#define NSHomeDirectory() testHome
#define CGGetOnlineDisplayList listScreens
#define CGDisplayCreateUUIDFromDisplayID fakeUuid
#define CGDisplayCopyDisplayMode fakeMode
#define CGDisplayCopyAllDisplayModes fakeModes
#define CGDisplayModeGetWidth(m) [modeData(m)[@"width"] unsignedIntegerValue]
#define CGDisplayModeGetHeight(m) [modeData(m)[@"height"] unsignedIntegerValue]
#define CGDisplayModeGetPixelWidth(m) [(modeData(m)[@"pixelsW"] ?: modeData(m)[@"width"]) unsignedIntegerValue]
#define CGDisplayModeGetPixelHeight(m) [(modeData(m)[@"pixelsH"] ?: modeData(m)[@"height"]) unsignedIntegerValue]
#define CGDisplayModeGetIODisplayModeID(m) 1
#define CGDisplayModeGetRefreshRate(m) 60.0
#define CGDisplayModeRetain(m) ((CGDisplayModeRef)CFRetain(m))
#define CGDisplayIsMain(i) [screen(i)[@"main"] boolValue]
#define CGDisplayIsOnline(i) ([screen(i)[@"online"] boolValue] || [screen(i)[@"enabled"] boolValue])
#define CGDisplayIsActive(i) [screen(i)[@"enabled"] boolValue]
#define CGDisplayMirrorsDisplay(i) [screen(i)[@"mirror"] unsignedIntValue]
#define CGDisplayBounds fakeBounds
#define CGBeginDisplayConfiguration begin
#define CGConfigureDisplayMirrorOfDisplay mirror
#define CGConfigureDisplayOrigin origin
#define CGConfigureDisplayWithDisplayMode setMode
#define CGCompleteDisplayConfiguration complete
#define CGCancelDisplayConfiguration cancel
#define CGDisplaySetDisplayMode selectMode
#define CGMainDisplayID mainDisplay
#define CGVirtualDisplay DPTestVirtualDisplay
#define CGVirtualDisplayMode DPTestVirtualDisplayMode
#define CGVirtualDisplaySettings DPTestVirtualDisplaySettings
#define CGVirtualDisplayDescriptor DPTestVirtualDisplayDescriptor
// Caret timer tests must never query the user's accessibility tree.
static unsigned caretQueries;
static BOOL caretTrusted(void) { ++caretQueries; return NO; }
#define AXIsProcessTrusted caretTrusted
#define main unusedDisplayHelperMain
#include "../host/macos/display-helper.m"
#undef main
static unsigned virtualDisplayCreations, lastDescriptorProductID;
static CGSize lastDescriptorSizeMM;
static BOOL rejectApplySettings;
@interface DPTestVirtualDisplayMode ()
@property unsigned testWidth, testHeight;
@end
@implementation DPTestVirtualDisplayMode
- (instancetype)initWithWidth:(unsigned)width height:(unsigned)height refreshRate:(double)rate {
    if ((self=[super init])) { self.testWidth=width; self.testHeight=height; } return self;
}
@end
@implementation DPTestVirtualDisplaySettings
@end
@implementation DPTestVirtualDisplayDescriptor
@end
@implementation DPTestVirtualDisplay
- (instancetype)initWithDescriptor:(DPTestVirtualDisplayDescriptor *)descriptor {
    if ((self=[super init])) {
        virtualDisplayCreations++;
        lastDescriptorSizeMM = descriptor.sizeInMillimeters;
        lastDescriptorProductID = descriptor.productID;
    }
    return self;
}
- (unsigned)displayID { return 9; }
- (BOOL)applySettings:(DPTestVirtualDisplaySettings *)settings {
    if (rejectApplySettings) return NO;
    DPTestVirtualDisplayMode *mode=settings.modes.firstObject;
    screen(9)[@"width"]=@(mode.testWidth); screen(9)[@"height"]=@(mode.testHeight);
    screen(9)[@"pixelsW"]=@(mode.testWidth*(settings.hiDPI ? 2 : 1));
    screen(9)[@"pixelsH"]=@(mode.testHeight*(settings.hiDPI ? 2 : 1));
    return YES;
}
@end
static BOOL recover(void) {
    for (unsigned i=0;i<5;i++) if (restoreTopology(9)) return YES;
    return NO;
}
static NSMutableDictionary *entry(BOOL enabled,BOOL main,int x) {
    return [@{@"enabled":@(enabled),@"main":@(main),@"mirror":@0,@"x":@(x),@"y":@0,@"width":@1920,@"height":@1080} mutableCopy];
}
static void reset(void) {
    savedTopology=nil; preserveTopologyJournal=NO; recoveryOwnedDisplay=0; rejectCommit=NO; enableAvailable=YES; modeWrites=0; sessionModeFallbacks=0; lastCommitOption=0;
    discardDisplay(); setSessionActive(NO); restoreAttempt=0; lastWidth=0; lastHeight=0; lastScale=1; ++generation;
    virtualDisplayCreations=0; lastDescriptorProductID=0; lastDescriptorSizeMM=CGSizeZero; rejectApplySettings=NO;
    screens=[@{@1:entry(YES,YES,0),@2:entry(YES,NO,1920),@3:entry(NO,NO,3840),@9:entry(YES,NO,5760)} mutableCopy];
    [NSFileManager.defaultManager removeItemAtPath:topologyPath() error:nil];
}
static void topologyFixtures(NSString *path) {
    NSData *data=[NSData dataWithContentsOfFile:path]; assert(data);
    NSArray *fixtures=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    assert([fixtures isKindOfClass:NSArray.class] && fixtures.count);
    for (NSDictionary *fixture in fixtures) for (int policy=0;policy<=2;policy++) {
        reset(); sessionDisplayPolicy=policy;
        [screens removeAllObjects];
        for (NSDictionary *item in fixture[@"screens"]) {
            NSMutableDictionary *value=[item mutableCopy]; NSNumber *ident=value[@"id"];
            [value removeObjectForKey:@"id"]; screens[ident]=value;
        }
        screens[@9]=entry(YES,NO,12000);
        assert(snapshotTopology(9));
        NSArray *original=savedTopology;
        assert(applySessionTopology(9) && sessionTopologyReady(9));
        // Resize/takeover may capture again, but must retain the first lease's baseline.
        assert(snapshotTopology(9) && savedTopology==original);
        // Simulate WindowServer assigning fresh transient IDs after reconnect.
        NSMutableDictionary *renumbered=[NSMutableDictionary new];
        for (NSNumber *ident in screens) {
            NSMutableDictionary *value=screens[ident];
            if (ident.unsignedIntValue==9) { renumbered[ident]=value; continue; }
            value[@"identity"]=value[@"identity"] ?: ident;
            unsigned source=[value[@"mirror"] unsignedIntValue];
            if (source && source!=9) value[@"mirror"]=@(source+100);
            renumbered[@(ident.unsignedIntValue+100)]=value;
        }
        screens=renumbered;
        // Extend can already match the baseline: perturb an origin to exercise
        // an actual rejected recovery transaction in every policy.
        screen(101)[@"x"]=@111;
        // A newly attached screen is outside the saved lease and stays untouched.
        screens[@200]=entry(YES,NO,14000); NSDictionary *hotplug=[screen(200) copy];
        rejectCommit=YES; assert(!recover());
        assert(savedTopology==original && [NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
        rejectCommit=NO;
        // Reload the exact on-disk journal, as after a helper crash.
        savedTopology=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:topologyPath()] options:0 error:nil];
        assert(recover() && !savedTopology && restoredTopologyReady());
        assert([screen(200) isEqual:hotplug]);
        for (NSDictionary *item in original) {
            unsigned ident=findDisplay(item[@"uuid"]); assert(ident);
            assert((BOOL)(CGDisplayIsActive(ident) || CGDisplayMirrorsDisplay(ident))==wasEnabled(item));
            if (!wasEnabled(item)) continue;
            assert(CGDisplayMirrorsDisplay(ident)==findDisplay(item[@"mirror"]));
            assert([screen(ident)[@"width"] isEqual:item[@"width"]]);
            assert([screen(ident)[@"height"] isEqual:item[@"height"]]);
            if ([item[@"main"] boolValue]) assert(CGDisplayIsMain(ident));
            assert([(screen(ident)[@"pixelsW"] ?: screen(ident)[@"width"]) isEqual:item[@"pixelsW"]]);
            assert([(screen(ident)[@"pixelsH"] ?: screen(ident)[@"height"]) isEqual:item[@"pixelsH"]]);
            if (![item[@"mirror"] length]) {
                assert([screen(ident)[@"x"] isEqual:item[@"x"]]);
                assert([screen(ident)[@"y"] isEqual:item[@"y"]]);
            }
        }
        assert(![NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
        printf("PASS fixture: %s policy=%d, UUID reconnect, hotplug, failed transaction and journal reload\n",[fixture[@"name"] UTF8String],policy);
    }
}
static void caretFixtures(void) {
    const uint64_t start = 100 * NSEC_PER_SEC;
    const uint64_t ms = NSEC_PER_MSEC;
    NSDictionary *invalid = @{@"valid":@NO};
    NSDictionary *valid = @{@"valid":@YES,@"x":@0.25,@"y":@0.5};
    resetCaret();
    assert(sendCaret(invalid, start));
    assert(!sendCaret([invalid mutableCopy], start + 200 * ms));
    assert(!sendCaret(invalid, start + 10000 * ms)); // Invalid has no keepalive.
    assert(sendCaret(valid, start + 10200 * ms)); // Validity changes immediately.
    assert(!sendCaret([valid mutableCopy], start + 10400 * ms));
    assert(!sendCaret(valid, start + 10599 * ms));
    assert(sendCaret(valid, start + 10600 * ms)); // Exact 400 ms deadline.
    assert(!sendCaret(valid, start + 10800 * ms));
    assert(sendCaret(valid, start + 11000 * ms)); // Deadline follows each send.
    NSMutableDictionary *moved = [valid mutableCopy];
    moved[@"x"] = @0.250001;
    assert(sendCaret(moved, start + 11001 * ms)); // No coordinate rounding.
    moved[@"y"] = @0.500001;
    assert(sendCaret(moved, start + 11002 * ms)); // Cached dictionary is a copy.
    assert(!sendCaret([moved copy], start + 11202 * ms));
    assert(sendCaret(moved, start + 11402 * ms));
    assert(sendCaret(invalid, start + 11403 * ms)); // Clear stale geometry now.
    assert(!sendCaret(invalid, start + 21403 * ms));
    setSessionActive(YES);
    assert(!lastCaret && !caretRefreshDue);
    assert(sendCaret(invalid, start + 21404 * ms));
    setSessionActive(NO);
    assert(!lastCaret && !caretRefreshDue);
    setSessionActive(YES);
    assert(sendCaret(valid, start + 21405 * ms));
    dispatch_source_t timer = caretTimer;
    setSessionActive(YES);
    assert(caretTimer == timer && !sendCaret(valid, start + 21406 * ms));
    discardDisplay();
    assert(!lastCaret && !caretRefreshDue);
    assert(sendCaret(valid, start + 21407 * ms)); // Same value on a new display.
    screens = [@{@9:entry(YES,NO,0)} mutableCopy];
    display = [DPTestVirtualDisplay new];
    assert(applyMode(1920, 1080, 1));
    assert(!lastCaret && !caretRefreshDue);
    assert(sendCaret(valid, start + 21408 * ms)); // Same value after a mode change.
    assert(sendCaret(valid, start + 22008 * ms)); // A delayed poll sends once.
    assert(!sendCaret(valid, start + 22009 * ms)); // No catch-up burst.
    discardDisplay();
    setSessionActive(NO);
    puts("PASS: exact caret changes, single invalid notification, monotonic 400 ms keepalive and transition resets");
}
int main(int argc, const char *argv[]) { @autoreleasepool {
    testHome=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    caretFixtures();
    // Starting, releasing and rapidly replacing a lease must own exactly one
    // caret timer. The AX trust stub counts samples without reading any apps.
    assert(!sessionActive && !caretTimer);
    setSessionActive(YES);
    setSessionActive(NO); // Cancel a first sample that has not run yet.
    setSessionActive(YES);
    dispatch_source_t currentCaretTimer = caretTimer;
    setSessionActive(YES); // Mode verification must not start another timer.
    assert(caretTimer == currentCaretTimer);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    assert(caretQueries == 1); // Each new session samples before the 200 ms tick.
    setSessionActive(NO);
    assert(!caretTimer && !sessionActive);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.3, false);
    assert(caretQueries == 1); // No idle poll or stale cancelled-source callback.
    setSessionActive(YES);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    assert(caretQueries == 2);
    setSessionActive(NO);
    puts("PASS: immediate per-session caret sample, single active timer, no idle accessibility queries");
    if (isolatedDisplay()) {
        reset(); sessionDisplayPolicy=DP_DISPLAY_PRIMARY_MIRROR;
        NSDictionary *first=[screen(1) copy], *second=[screen(2) copy];
        NSString *expected=[NSProcessInfo.processInfo.environment[@"DESKPORT_DISPLAY_STATE_DIR"] stringByAppendingPathComponent:@"display-recovery.json"];
        assert([topologyPath() isEqualToString:expected]);
        assert(snapshotTopology(9) && !savedTopology);
        screen(9)[@"mirror"]=@1;
        assert(applySessionTopology(9));
        assert(![screen(9)[@"mirror"] unsignedIntValue]);
        assert([screen(1) isEqual:first] && [screen(2) isEqual:second]);
        assert(restoreTopology(9));
        assert([screen(1) isEqual:first] && [screen(2) isEqual:second]);
        assert(modeWrites==0 && !savedTopology);
        assert(virtualDisplayProductID(2)==2 && virtualDisplayProductID(1)==2);
        NSDictionary *mode=@{@"width":@786,@"height":@800,@"pixelsW":@1572,@"pixelsH":@1600};
        persistDisplayMode(9,(CGDisplayModeRef)(__bridge void *)mode);
        assert(lastCommitOption==kCGConfigureForSession && sessionModeFallbacks==0);
        assert(![NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
        puts("PASS: isolated helper only changes its own extended display, no shared journal or physical restore");
        return 0;
    }
    assert(argc==2); topologyFixtures([NSString stringWithUTF8String:argv[1]]);
    for (int policy=0;policy<=2;policy++) {
        reset(); sessionDisplayPolicy=policy;
        assert(snapshotTopology(9)); NSArray *before=savedTopology;
        screens[@4]=entry(YES,NO,7680); // Added after lease acquisition.
        assert(applySessionTopology(9)); assert(sessionTopologyReady(9));
        assert(savedTopology==before); assert([screen(4)[@"enabled"] boolValue]); assert(![screen(4)[@"mirror"] intValue]);
        assert(![screen(3)[@"enabled"] boolValue]);
        if (policy==0) assert([screen(1)[@"mirror"] intValue]==9);
        if (policy==1) assert(![screen(1)[@"enabled"] boolValue] && ![screen(2)[@"enabled"] boolValue]);
        if (policy==2) assert([screen(1)[@"main"] boolValue] && [screen(2)[@"x"] intValue]==1920);
        assert(snapshotTopology(9) && savedTopology==before); // Resize keeps original snapshot.
        assert(recover()); assert(!savedTopology);
        assert([screen(1)[@"main"] boolValue] && [screen(1)[@"enabled"] boolValue]);
        assert([screen(2)[@"enabled"] boolValue] && ![screen(3)[@"enabled"] boolValue]);
    }
    reset(); screens[@5]=entry(NO,NO,0); screens[@5][@"noUuid"]=@YES;
    assert(snapshotTopology(9)); assert(savedTopology.count==3);
    assert(![screen(3)[@"enabled"] boolValue]); // Identifiable disabled panels stay in the snapshot.
    // A disconnected connector has no UUID. An empty mirror UUID means no
    // source and must never resolve to that connector during local restoration.
    assert(findDisplay(nil)==0 && findDisplay(@"")==0);
    assert(restorationSource(savedTopology[0])==0);
    assert(restoredTopologyReady());
    assert(recover() && !savedTopology);
    reset(); screens[@1][@"noUuid"]=@YES;
    assert(!snapshotTopology(9)); assert(!savedTopology);
    reset(); screens[@3][@"noUuid"]=@YES; screens[@3][@"online"]=@YES;
    assert(!snapshotTopology(9)); assert(!savedTopology);
    reset(); sessionDisplayPolicy=1; assert(snapshotTopology(9)); enableAvailable=NO;
    assert(!applySessionTopology(9)); assert([screen(1)[@"main"] boolValue]);
    enableAvailable=YES; assert(applySessionTopology(9)); rejectCommit=YES;
    assert(!restoreTopology(9)); assert(savedTopology && [NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
    rejectCommit=NO;
    [screens removeObjectForKey:@2]; // Detached display is skipped during recovery.
    assert(!restoreTopology(9)); assert(recover()); assert(!savedTopology);
    reset(); sessionDisplayPolicy=0; assert(snapshotTopology(9)); assert(applySessionTopology(9));
    savedTopology=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:topologyPath()] options:0 error:nil];
    assert(!restoreTopology(9)); assert(recover()); // Simulated helper crash/restart.
    // A mirror sink exposes only constrained modes until a completed detach.
    reset(); sessionDisplayPolicy=0; assert(snapshotTopology(9));
    screen(1)[@"nativeModes"]=@[[screen(1) copy]];
    screen(1)[@"mirror"]=@9; screen(1)[@"width"]=@1280;
    assert(!restoreTopology(9)); assert(modeWrites==0);
    assert(![screen(1)[@"mirror"] unsignedIntValue]);
    assert(recover()); assert([screen(1)[@"width"] intValue]==1920);

    // Older journals can refer to our own virtual source. Do not recreate it.
    reset(); screen(1)[@"mirror"]=@9; assert(snapshotTopology(9));
    assert([savedTopology[0][@"mirror"] length]==0);
    NSMutableArray *legacy=[savedTopology mutableCopy];
    NSMutableDictionary *legacyEntry=[legacy[0] mutableCopy];
    legacyEntry[@"mirror"]=displayUUID(9); legacy[0]=legacyEntry; savedTopology=legacy;
    assert(recover()); assert(![screen(1)[@"mirror"] unsignedIntValue]);

    // Missing physical modes retain recovery, but cannot poison the next lease.
    for (int policy=0;policy<=2;policy++) {
        reset(); sessionDisplayPolicy=policy; assert(snapshotTopology(9));
        NSArray *original=savedTopology;
        screen(1)[@"width"]=@1280;
        assert(!recover()); assert(savedTopology==original);
        assert([NSFileManager.defaultManager fileExistsAtPath:topologyPath()]);
        display=[DPTestVirtualDisplay new];
        configure(2560,1440,1,6,NO); // Leaves an idle restore callback queued.
        configure(1920,1888,2,7,YES);
        for (unsigned i=0;i<40;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
        assert(sessionActive && (sessionTopologyReady(9) || sessionLayoutWarning));
        assert(lastWidth==1920 && lastHeight==1888 && lastScale==2);
        assert(savedTopology==original);
        assert(CGDisplayModeGetPixelWidth((CGDisplayModeRef)(__bridge void *)screen(9))==1920);
        assert(CGDisplayModeGetPixelHeight((CGDisplayModeRef)(__bridge void *)screen(9))==1888);
        // A queued idle callback must not restore over the new session.
        if (policy!=DP_DISPLAY_EXTEND) assert(CGDisplayIsMain(9));
    }
    // Local transaction rejection is a recorded warning, not bad-size video.
    reset(); sessionDisplayPolicy=DP_DISPLAY_PRIMARY_MIRROR; assert(snapshotTopology(9));
    display=[DPTestVirtualDisplay new]; rejectCommit=YES;
    configure(1920,1888,2,9,YES);
    for (unsigned i=0;i<15;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(sessionActive && sessionLayoutWarning);
    assert(lastWidth==1920 && lastHeight==1888 && lastScale==2);
    NSString *events=[topologyPath().stringByDeletingLastPathComponent stringByAppendingPathComponent:@"display-layout-events.jsonl"];
    NSString *logged=[NSString stringWithContentsOfFile:events encoding:NSUTF8StringEncoding error:nil];
    assert([logged containsString:@"local display layout could not be fully applied"]);
    assert([logged containsString:@"original"] && [logged containsString:@"observed"] && [logged containsString:@"time"]);

    // Physical size matches Retina (~220 PPI) / 1x (~110 PPI) square pixels so
    // WindowServer picks the requested scale as default without a 1x bounce.
    {
        CGSize mm2x=physicalSizeForMode(2828,1676,2);
        assert(mm2x.width==327 && mm2x.height==194);
        assert(physicalSizeCompatible(mm2x,2828,1676,2));
        assert(!physicalSizeCompatible(mm2x,2828,1676,1));
        assert(!physicalSizeCompatible(CGSizeMake(600,340),2828,1676,2));
        CGSize mmPortrait=physicalSizeForMode(1668,2420,2);
        assert(mmPortrait.width==193 && mmPortrait.height==279);
        assert(physicalSizeCompatible(mmPortrait,1668,2420,2));
        assert(!physicalSizeCompatible(mmPortrait,2420,1668,2));
        CGSize mm1x=physicalSizeForMode(1920,1080,1);
        assert(mm1x.width==443 && mm1x.height==249);
        assert(physicalSizeCompatible(mm1x,1920,1080,1));
        assert(!physicalSizeCompatible(mm1x,1920,1080,2));
    }

    // Primary-mirror sessions create with matching DPI, reuse identical modes,
    // and replace the virtual display when geometry or scale changes.
    reset(); sessionDisplayPolicy=DP_DISPLAY_PRIMARY_MIRROR; assert(snapshotTopology(9));
    configure(5104,2776,2,10,YES);
    for (unsigned i=0;i<10;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==1 && lastDescriptorProductID==3 && lastDescriptorSizeMM.width==589 && lastDescriptorSizeMM.height==321);
    assert(lastWidth==5104 && lastHeight==2776 && lastScale==2);
    configure(5104,2776,2,11,YES); // Same mode stays in-place.
    for (unsigned i=0;i<10;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==1);
    configure(2828,1676,2,12,YES); // Smaller workspace replaces display with fresh DPI & ID.
    for (unsigned i=0;i<15;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==2 && lastDescriptorProductID==3 && lastDescriptorSizeMM.width==327 && lastDescriptorSizeMM.height==194);
    assert(lastWidth==2828 && lastHeight==1676 && lastScale==2);

    // Extended workspaces keep minor compatible resizes in-place to avoid
    // evacuating windows, while recreating on scale flips or orientation shifts.
    reset(); sessionDisplayPolicy=DP_DISPLAY_EXTEND; assert(snapshotTopology(9));
    configure(2560,1440,2,20,YES);
    for (unsigned i=0;i<10;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==1 && lastDescriptorProductID==3 && lastDescriptorSizeMM.width==296 && lastDescriptorSizeMM.height==166);
    configure(2400,1350,2,21,YES); // Compatible Retina PPI (~206 PPI): stays in-place.
    for (unsigned i=0;i<10;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==1 && lastWidth==2400 && lastHeight==1350 && lastScale==2);
    configure(1668,2420,2,22,YES); // Portrait rotation: recreates with square-pixel portrait mm.
    for (unsigned i=0;i<15;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==2 && lastDescriptorProductID==3 && lastDescriptorSizeMM.width==193 && lastDescriptorSizeMM.height==279);
    assert(lastWidth==1668 && lastHeight==2420 && lastScale==2);
    configure(1920,1200,1,23,YES); // Scale flip 2x -> 1x: recreates with 110 PPI mm & 1x productID.
    for (unsigned i=0;i<15;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(virtualDisplayCreations==3 && lastDescriptorProductID==4 && lastDescriptorSizeMM.width==443 && lastDescriptorSizeMM.height==277);
    assert(lastWidth==1920 && lastHeight==1200 && lastScale==1);

    // Mode fallback heals WindowServer's permanent store, falling back to
    // session-only mode switch when permanent commit is rejected.
    reset();
    assert(virtualDisplayProductID(2)==3 && virtualDisplayProductID(1)==4);
    {
        NSDictionary *mode=@{@"width":@786,@"height":@800,@"pixelsW":@1572,@"pixelsH":@1600};
        persistDisplayMode(9,(CGDisplayModeRef)(__bridge void *)mode);
        assert(lastCommitOption==kCGConfigurePermanently && sessionModeFallbacks==0);
        assert([screen(9)[@"width"] intValue]==786 && [screen(9)[@"pixelsW"] intValue]==1572);
        rejectCommit=YES;
        NSDictionary *other=@{@"width":@1414,@"height":@838,@"pixelsW":@2828,@"pixelsH":@1676};
        persistDisplayMode(9,(CGDisplayModeRef)(__bridge void *)other);
        assert(lastCommitOption==kCGConfigurePermanently && sessionModeFallbacks==1);
        assert([screen(9)[@"width"] intValue]==1414 && [screen(9)[@"pixelsW"] intValue]==2828);
    }

    [NSFileManager.defaultManager removeItemAtPath:testHome error:nil];
    puts("PASS: three policies, mode-list recovery, journal retention, queued restore takeover, degraded local layout, diagnostic events, DPI sizing and display recreation");
} }
