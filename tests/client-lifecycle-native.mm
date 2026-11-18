#import <AppKit/AppKit.h>
void injectNativeSleep(bool sleeping) {
    // Process-local notification only: no OS sleep request or distributed post.
    [NSWorkspace.sharedWorkspace.notificationCenter
        postNotificationName:(sleeping ? NSWorkspaceWillSleepNotification : NSWorkspaceDidWakeNotification)
        object:NSWorkspace.sharedWorkspace];
}
void injectNativeLock() {
    [NSWorkspace.sharedWorkspace.notificationCenter
        postNotificationName:NSWorkspaceSessionDidResignActiveNotification object:NSWorkspace.sharedWorkspace];
}
