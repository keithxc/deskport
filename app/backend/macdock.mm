#include "macdock.h"
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <functional>
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

void deskPortAfterStatusItemTracking(QObject* context, std::function<void()> callback) {
    const QPointer<QObject> owner(context);
    // A queued Qt call, zero timer or main dispatch block can run in AppKit's
    // nested tracking loop. A default-mode-only block lets the real mouseUp
    // finish that loop before another native menu starts consuming events.
    [[NSRunLoop mainRunLoop] performInModes:@[NSDefaultRunLoopMode] block:^{
        if (owner) callback();
    }];
}

@interface DeskPortStatusMenuTarget : NSObject
@property (nonatomic, assign) NSInteger chosen;
@end

@implementation DeskPortStatusMenuTarget
- (void)itemSelected:(NSMenuItem*)sender { self.chosen = sender.tag; }
@end

QAction* deskPortShowStatusMenu(QMenu* source) {
    DeskPortStatusMenuTarget* target = [[DeskPortStatusMenuTarget alloc] init];
    target.chosen = -1;
    QList<QPointer<QAction>> actions;
    std::function<NSMenu*(QMenu*)> build = [&](QMenu* input) {
        NSMenu* menu = [[NSMenu alloc] initWithTitle:input->title().toNSString()];
        menu.autoenablesItems = NO;
        for (auto action : input->actions()) {
            if (!action->isVisible()) continue;
            if (action->isSeparator()) { [menu addItem:NSMenuItem.separatorItem]; continue; }
            NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:action->text().toNSString()
                action:@selector(itemSelected:) keyEquivalent:@""];
            item.target = target; item.tag = actions.size(); actions.append(action);
            item.enabled = action->isEnabled();
            item.state = action->isChecked() ? NSControlStateValueOn : NSControlStateValueOff;
            if (action->menu()) {
                NSMenu* submenu = build(action->menu()); item.submenu = submenu; [submenu release];
            }
            [menu addItem:item]; [item release];
        }
        return menu;
    };
    NSMenu* menu = build(source);
    [menu popUpMenuPositioningItem:nil atLocation:NSEvent.mouseLocation inView:nil];
    const int chosen = int(target.chosen);
    [menu release]; [target release];
    return chosen >= 0 && chosen < actions.size() ? actions.at(chosen).data() : nullptr;
}
