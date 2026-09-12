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

@interface DeskPortStatusMenuTarget : NSObject
@property (nonatomic, assign) NSInteger chosen;
@end

@implementation DeskPortStatusMenuTarget
- (void)itemSelected:(NSMenuItem*)sender { self.chosen = sender.tag; }
@end

int deskPortShowStatusMenu(const QStringList& titles) {
    DeskPortStatusMenuTarget* target = [[DeskPortStatusMenuTarget alloc] init];
    target.chosen = -1;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"DeskPort"];
    // The items carry no validation target of their own.
    menu.autoenablesItems = NO;
    for (int index = 0; index < titles.size(); ++index) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:titles.at(index).toNSString()
                                                      action:@selector(itemSelected:)
                                               keyEquivalent:@""];
        item.target = target;
        item.tag = index;
        item.enabled = YES;
        [menu addItem:item];
        [item release];
    }
    // Tracking is modal and the action is sent before this returns, so the caller
    // runs the chosen item outside the menu's event loop.
    [menu popUpMenuPositioningItem:nil atLocation:NSEvent.mouseLocation inView:nil];
    const int chosen = int(target.chosen);
    [menu release];
    [target release];
    return chosen;
}
