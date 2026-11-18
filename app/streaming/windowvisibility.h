#pragma once
#include <QtGlobal>
#include <SDL.h>
#include <atomic>
#include <memory>

// Notification-backed native visibility, scoped to exactly one SDL window.
// Unsupported desktop/activity/overlap detection stays explicitly unavailable.
class WindowVisibility {
public:
    explicit WindowVisibility(SDL_Window* window);
    ~WindowVisibility();
    WindowVisibility(const WindowVisibility&) = delete;
    WindowVisibility& operator=(const WindowVisibility&) = delete;
    bool unavailable() const;
    const char* capability() const;
private:
    struct Native;
    SDL_Window* m_Window;
    std::unique_ptr<Native> m_Native;
};

// No object pointers enter the SDL event queue, so a queued wake is harmless
// after a window/observer is destroyed during a continuation.
constexpr int DeskPortVisibilityChanged = 115; // 114 is the existing shortcut-toggle event.
inline void wakeWindowVisibility(Uint32 windowId) {
    SDL_Event event {}; event.type = SDL_USEREVENT;
    event.user.code = DeskPortVisibilityChanged; event.user.windowID = windowId;
    SDL_PushEvent(&event);
}

#ifdef Q_OS_MACOS
void* observeWindowOcclusion(SDL_Window*, const std::shared_ptr<std::atomic<bool>>&);
void stopObservingWindowOcclusion(void*);
#endif
