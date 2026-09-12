#include "macdock.h"
#import <AppKit/AppKit.h>

void deskPortSetDockIconVisible(bool visible) {
    const NSApplicationActivationPolicy policy = visible
        ? NSApplicationActivationPolicyRegular
        : NSApplicationActivationPolicyAccessory;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (NSApp.activationPolicy != policy) [NSApp setActivationPolicy:policy];
    });
}

void deskPortActivateApplication() {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp activateIgnoringOtherApps:YES];
    });
}
