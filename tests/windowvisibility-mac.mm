#include <SDL.h>
#include <SDL_syswm.h>
#import <AppKit/AppKit.h>

// Test-only opaque native cover. It uses the fixture's own Cocoa frame so Qt
// backing-store transparency or coordinate conversion cannot fake occlusion.
void* createVisibilityCover(SDL_Window* window) {
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window,&info)) return nullptr;
    NSWindow* target=info.info.cocoa.window;
    NSWindow* cover=[[NSWindow alloc] initWithContentRect:NSInsetRect(target.frame,-50,-50)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    cover.releasedWhenClosed=NO;
    cover.opaque=YES; cover.backgroundColor=NSColor.blackColor;
    cover.level=NSFloatingWindowLevel;
    return cover;
}
void showVisibilityCover(void* value, bool shown) {
    NSWindow* cover=(NSWindow*)value;
    if(shown) [cover orderFrontRegardless]; else [cover orderOut:nil];
}
void destroyVisibilityCover(void* value) {
    NSWindow* cover=(NSWindow*)value;
    [cover orderOut:nil]; [cover close]; [cover release];
}
bool nativeVisibilityOccluded(SDL_Window* window) {
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version); SDL_GetWindowWMInfo(window,&info);
    return !(info.info.cocoa.window.occlusionState & NSWindowOcclusionStateVisible);
}
