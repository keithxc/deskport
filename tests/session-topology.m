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
static unsigned modeWrites;
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
static CGError selectMode(unsigned i,CGDisplayModeRef m,CFDictionaryRef opts) { return 0; }
static unsigned mainDisplay(void) { for (NSNumber *n in screens) if ([screen(n.unsignedIntValue)[@"main"] boolValue]) return n.unsignedIntValue; return 0; }
static CGError complete(CGDisplayConfigRef c,CGConfigureOption option) {
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
#define main unusedDisplayHelperMain
#include "../host/macos/display-helper.m"
#undef main
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
- (instancetype)initWithDescriptor:(DPTestVirtualDisplayDescriptor *)descriptor { return [super init]; }
- (unsigned)displayID { return 9; }
- (BOOL)applySettings:(DPTestVirtualDisplaySettings *)settings {
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
    savedTopology=nil; preserveTopologyJournal=NO; recoveryOwnedDisplay=0; rejectCommit=NO; enableAvailable=YES; modeWrites=0;
    display=nil; sessionActive=NO; restoreAttempt=0; ++generation;
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
int main(int argc, const char *argv[]) { @autoreleasepool {
    testHome=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
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
    for (unsigned i=0;i<5;i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,false);
    assert(sessionActive && sessionLayoutWarning);
    assert(lastWidth==1920 && lastHeight==1888 && lastScale==2);
    NSString *events=[topologyPath().stringByDeletingLastPathComponent stringByAppendingPathComponent:@"display-layout-events.jsonl"];
    NSString *logged=[NSString stringWithContentsOfFile:events encoding:NSUTF8StringEncoding error:nil];
    assert([logged containsString:@"local display layout could not be fully applied"]);
    assert([logged containsString:@"original"] && [logged containsString:@"observed"] && [logged containsString:@"time"]);
    [NSFileManager.defaultManager removeItemAtPath:testHome error:nil];
    puts("PASS: three policies, mode-list recovery, journal retention, queued restore takeover, degraded local layout and diagnostic events");
} }
