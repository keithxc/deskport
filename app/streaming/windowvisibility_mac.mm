#include <QtGlobal>
#include "windowvisibility.h"
#include <SDL_syswm.h>
#import <AppKit/AppKit.h>

void* observeWindowOcclusion(SDL_Window* window, const std::shared_ptr<std::atomic<bool>>& state) {
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA) return nullptr;
    NSWindow* native = info.info.cocoa.window;
    const Uint32 windowId = SDL_GetWindowID(window);
    state->store(!(native.occlusionState & NSWindowOcclusionStateVisible));
    // The block retains shared state, so a notification queued before removal
    // cannot dereference a destroyed C++ monitor or a new continuation's window.
    const auto retainedState = state;
    id observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidChangeOcclusionStateNotification object:native queue:nil
        usingBlock:^(NSNotification* notification) {
            NSWindow* changed = notification.object;
            retainedState->store(!(changed.occlusionState & NSWindowOcclusionStateVisible));
            wakeWindowVisibility(windowId);
        }];
    return [observer retain];
}
void stopObservingWindowOcclusion(void* value) {
    if (!value) return;
    id observer = (id)value;
    [[NSNotificationCenter defaultCenter] removeObserver:observer];
    [observer release];
}
