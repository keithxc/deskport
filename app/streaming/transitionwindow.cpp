#include "transitionwindow.h"
#include <QImage>
#include <QPainter>
#include <QFontMetrics>
#include <cmath>

TransitionWindow::TransitionWindow(SDL_Window* window, const QString& text) : m_Window(window) {
    // Keep SDL video alive after the old session releases its reference.
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_SetWindowGrab(window, SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    m_Label = text;
    if (qstrcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        m_Wayland = std::make_unique<WaylandLoading>(window);
    }
    else m_Renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!rendering()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Loading renderer unavailable: %s", SDL_GetError());
    pump();
}
void TransitionWindow::clearRenderer() {
    if (m_Renderer) SDL_DestroyRenderer(m_Renderer);
    m_Renderer = nullptr;
    m_Wayland.reset();
#if SDL_VERSION_ATLEAST(2, 28, 0)
    if (m_Window && SDL_HasWindowSurface(m_Window)) SDL_DestroyWindowSurface(m_Window);
#endif
}
TransitionWindow::~TransitionWindow() {
    clearRenderer();
    if (m_Window) SDL_DestroyWindow(m_Window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
SDL_Window* TransitionWindow::takeWindow() {
    SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEWHEEL);
    clearRenderer();
    auto window = m_Window; m_Window = nullptr;
    return window;
}
void TransitionWindow::pump() {
    if (!m_Window) return;
    SDL_PumpEvents();
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_USEREVENT, SDL_USEREVENT) > 0)
        if (event.user.code == DeskPortRecallWindow) recallDesktopWindow(m_Window);
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT) > 0)
        if (event.window.windowID == SDL_GetWindowID(m_Window) && event.window.event == SDL_WINDOWEVENT_CLOSE) m_Cancelled = true;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) m_Cancelled = true;
    // Never replay keystrokes or clicks made while the stream was unavailable.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_MOUSEWHEEL) > 0)
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) m_Cancelled = true;
    if (m_Cancelled) { SDL_HideWindow(m_Window); return; }
    SDL_FlushEvents(SDL_CONTROLLERAXISMOTION, SDL_CONTROLLERBUTTONUP);
    SDL_FlushEvents(SDL_FINGERDOWN, SDL_MULTIGESTURE);
    if (!rendering() || (SDL_GetWindowFlags(m_Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))) return;
    int w, h;
#if SDL_VERSION_ATLEAST(2, 26, 0)
    SDL_GetWindowSizeInPixels(m_Window, &w, &h);
#else
    SDL_GetWindowSize(m_Window, &w, &h);
#endif
    if (m_Frame.size() != QSize(w, h)) m_Frame = QImage(w, h, QImage::Format_ARGB32);
    m_Frame.fill(QColor(24, 27, 33));
    QPainter painter(&m_Frame); painter.setRenderHint(QPainter::Antialiasing);
    const int phase = (SDL_GetTicks() / 80) % 12;
    for (int i = 0; i < 12; ++i) {
        const double angle = i * 6.283185307 / 12;
        const int light = 55 + ((i - phase + 12) % 12) * 16;
        painter.setPen(Qt::NoPen); painter.setBrush(QColor(int(light * 0.65), int(light * 0.85), light));
        painter.drawEllipse(QPointF(w / 2 + std::cos(angle) * 22, h / 2 - 28 + std::sin(angle) * 22), 3, 3);
    }
    QFont font; font.setPixelSize(20); painter.setFont(font); painter.setPen(QColor(230, 233, 238));
    painter.drawText(QRect(0, h / 2 + 16, w, 48), Qt::AlignHCenter | Qt::AlignTop, m_Label);
    painter.end();
    if (m_Wayland) { if (m_Wayland->present(m_Frame)) ++m_PresentedFrames; return; }
    auto surface = SDL_CreateRGBSurfaceWithFormatFrom(m_Frame.bits(), w, h, 32, m_Frame.bytesPerLine(), SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return;
    auto texture = SDL_CreateTextureFromSurface(m_Renderer, surface); SDL_FreeSurface(surface);
    if (!texture) return;
    SDL_RenderCopy(m_Renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(m_Renderer); SDL_DestroyTexture(texture); ++m_PresentedFrames;
}
