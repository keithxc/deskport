#include <QGuiApplication>
#include <QImage>
#include <SDL.h>
#include <SDL_syswm.h>
#include <cassert>
#include <cstdio>
#include "transitionwindow.h"

static void* nativeWindow(SDL_Window* window) {
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return nullptr;
#if defined(SDL_VIDEO_DRIVER_COCOA)
    if (info.subsystem == SDL_SYSWM_COCOA) return info.info.cocoa.window;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
    if (info.subsystem == SDL_SYSWM_WAYLAND) return info.info.wl.surface;
#endif
    return nullptr;
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    auto window = SDL_CreateWindow("DeskPort transition test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 400, SDL_WINDOW_RESIZABLE | (qEnvironmentVariableIsSet("DESKPORT_TRANSITION_METAL") ? SDL_WINDOW_METAL : 0)
        | (qEnvironmentVariableIsSet("DESKPORT_TRANSITION_GL") ? SDL_WINDOW_OPENGL : 0)
        | (qEnvironmentVariableIsSet("DESKPORT_TRANSITION_VULKAN") ? SDL_WINDOW_VULKAN : 0));
    assert(window);
    // Let the compositor apply its initial placement/tiling policy before taking
    // the baseline; requested dimensions are not guaranteed on Wayland.
    const auto initialNative = nativeWindow(window);
    const auto graphicsFlags = SDL_GetWindowFlags(window) & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN | SDL_WINDOW_METAL);
    {
        TransitionWindow warmup(window, "Preparing transition test");
        assert(warmup.rendering());
        for (int i = 0; i < 15; ++i) { warmup.pump(); SDL_Delay(20); }
        assert(warmup.takeWindow() == window);
    }
    assert(nativeWindow(window) == initialNative);
    assert((SDL_GetWindowFlags(window) & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN | SDL_WINDOW_METAL)) == graphicsFlags);
    int expectedWidth, expectedHeight; SDL_GetWindowSize(window, &expectedWidth, &expectedHeight);
    const auto id = SDL_GetWindowID(window);
    const auto native = nativeWindow(window);
    for (int cycle = 0; cycle < 30; ++cycle) {
        TransitionWindow transition(window, QString::fromUtf8("正在调整分辨率…"));
        assert(transition.rendering());
        assert(SDL_GetWindowFromID(id) == window);
        assert(nativeWindow(window) == native);
        int width, height; SDL_GetWindowSize(window, &width, &height);
        assert(width == expectedWidth && height == expectedHeight);
        SDL_Event click {}; click.type = SDL_MOUSEBUTTONDOWN; SDL_PushEvent(&click);
        transition.pump();
        if (cycle == 0) {
            const QImage before = transition.frame().copy();
            SDL_Delay(110); transition.pump();
            const QImage after = transition.frame().copy();
            assert(!before.isNull() && before != after);
            for (int i = 0; i < 15; ++i) { transition.pump(); SDL_Delay(30); }
            assert(transition.presentedFrames() > 3); // Buffer release must keep animation flowing.
            const auto snapshot = qEnvironmentVariable("DESKPORT_TRANSITION_SNAPSHOT");
            if (!snapshot.isEmpty()) assert(after.save(snapshot));
            const int duration = qEnvironmentVariableIntValue("DESKPORT_TRANSITION_PREVIEW_MS");
            const auto start = SDL_GetTicks();
            while (SDL_GetTicks() - start < unsigned(duration)) { transition.pump(); SDL_Delay(20); }
        }
        assert(!SDL_HasEvent(SDL_MOUSEBUTTONDOWN));
        assert(!transition.cancelled());
        assert(!(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN));
        assert(transition.takeWindow() == window);
        assert(!SDL_GetRenderer(window));
        assert(SDL_GetWindowFromID(id) == window);
    }
    {
        TransitionWindow transition(window, "Adjusting resolution…");
        SDL_Event close {}; close.type = SDL_WINDOWEVENT; close.window.windowID = id; close.window.event = SDL_WINDOWEVENT_CLOSE;
        SDL_PushEvent(&close); transition.pump();
        assert(!transition.cancelled());
        assert(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN);
        SDL_Event recall {}; recall.type = SDL_USEREVENT; recall.user.code = DeskPortRecallWindow;
        SDL_PushEvent(&recall); transition.pump();
        assert(!(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN));
        SDL_Event end {}; end.type = SDL_USEREVENT; end.user.code = DeskPortEndSession;
        SDL_PushEvent(&end); transition.pump();
        assert(transition.cancelled());
    }
    assert(SDL_GetWindowFromID(id) == nullptr);
    SDL_Quit();
    puts("PASS: 30 transitions retain native window, consume waiting input, and hide/recall on close and clean up on explicit disconnect");
}
