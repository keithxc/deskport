#include <QtGlobal>
#include "windowvisibility.h"
#include "canvasbridge.h"
#ifdef Q_OS_WIN
#include <SDL_syswm.h>
#include <windows.h>
#include <dwmapi.h>
#endif

struct WindowVisibility::Native {
    std::shared_ptr<std::atomic<bool>> occluded = std::make_shared<std::atomic<bool>>(false);
    bool supported = false;
#ifdef Q_OS_MACOS
    void* observer = nullptr;
    ~Native() { stopObservingWindowOcclusion(observer); }
#elif defined(Q_OS_WIN)
    HWND window = nullptr;
    HWINEVENTHOOK hook = nullptr;
    Uint32 id = 0;
    static constexpr auto Property = L"DeskPort.WindowVisibility";
    bool refresh() {
        DWORD cloaked = 0;
        // One query at registration and one per OS notification; no polling or
        // foreign-window enumeration. Failed queries conservatively keep video.
        const bool available = SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)));
        occluded->store(available && cloaked);
        return available;
    }
    static void CALLBACK changed(HWINEVENTHOOK, DWORD, HWND window, LONG, LONG, DWORD, DWORD) {
        if (!window) return;
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        // System/DWM can generate cloak events. Restrict the target before
        // reading a property that another worker could set in its own process.
        if (process != GetCurrentProcessId()) return;
        auto self = reinterpret_cast<Native*>(GetPropW(window, Property));
        if (self) { self->supported = self->refresh(); wakeWindowVisibility(self->id); }
    }
    ~Native() {
        if (hook) UnhookWinEvent(hook);
        if (window && GetPropW(window, Property) == reinterpret_cast<HANDLE>(this)) RemovePropW(window, Property);
    }
#endif
};

WindowVisibility::WindowVisibility(SDL_Window* window) : m_Window(window), m_Native(new Native) {
    if (CanvasBridge::owns(window)) return;
#ifdef Q_OS_MACOS
    m_Native->observer = observeWindowOcclusion(window, m_Native->occluded);
    m_Native->supported = m_Native->observer != nullptr;
#elif defined(Q_OS_WIN)
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
        auto& n = *m_Native; n.window = info.info.win.window; n.id = SDL_GetWindowID(window);
        if (SetPropW(n.window, Native::Property, reinterpret_cast<HANDLE>(&n))) {
            n.hook = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr,
                                    Native::changed, 0, 0, WINEVENT_OUTOFCONTEXT);
            n.supported = n.hook != nullptr;
            if (n.supported) n.supported = n.refresh();
        }
    }
#endif
}
WindowVisibility::~WindowVisibility() = default;
bool WindowVisibility::unavailable() const {
    return !m_Window || (CanvasBridge::flags(m_Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) ||
        m_Native->occluded->load();
}
const char* WindowVisibility::capability() const {
    if (CanvasBridge::owns(m_Window)) return "shell-hide+minimize;occlusion-unavailable";
#ifdef Q_OS_MACOS
    if (m_Native->supported) return "cocoa-occlusion+hide+minimize";
#elif defined(Q_OS_WIN)
    if (m_Native->supported) return "dwm-cloak+hide+minimize;overlap-unavailable";
#endif
    if (SDL_GetCurrentVideoDriver() && SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
        return "hide-only;minimize+occlusion+activity-unavailable";
    return "hide+minimize;occlusion+activity-unavailable";
}
