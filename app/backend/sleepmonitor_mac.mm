#include "sleepmonitor.h"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace {
struct Watch {
    NSMutableArray* workspace = [[NSMutableArray alloc] init];
    NSMutableArray* distributed = [[NSMutableArray alloc] init];
};
bool locked() {
    NSDictionary* session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
    // Lock notifications are supplemented with the current console state so a
    // worker starting while locked does not assume that input is permitted.
    return !session || ![session[(id)kCGSessionOnConsoleKey] boolValue] ||
        [session[@"CGSSessionScreenIsLocked"] boolValue];
}
}
void* deskportWatchSleep(SleepMonitor* monitor) {
    auto watch = new Watch;
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    [watch->workspace addObject:[center addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->prepareForSleep(true); }]];
    [watch->workspace addObject:[center addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->prepareForSleep(false); }]];
    return watch;
}
void* deskportWatchLock(ScreenLockMonitor* monitor) {
    auto watch = new Watch;
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    [watch->workspace addObject:[center addObserverForName:NSWorkspaceSessionDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->setLocked(true); }]];
    [watch->workspace addObject:[center addObserverForName:NSWorkspaceSessionDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->setLocked(locked()); }]];
    [watch->workspace addObject:[center addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->setLocked(locked()); }]];
    auto distributed = NSDistributedNotificationCenter.defaultCenter;
    [watch->distributed addObject:[distributed addObserverForName:@"com.apple.screenIsLocked" object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->setLocked(true); }]];
    [watch->distributed addObject:[distributed addObserverForName:@"com.apple.screenIsUnlocked" object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) { monitor->setLocked(locked()); }]];
    monitor->setLocked(locked());
    return watch;
}
void deskportUnwatchLifecycle(void* context) {
    auto watch = static_cast<Watch*>(context); if (!watch) return;
    for (id observer in watch->workspace) [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer];
    for (id observer in watch->distributed) [NSDistributedNotificationCenter.defaultCenter removeObserver:observer];
    [watch->workspace release]; [watch->distributed release]; delete watch;
}
