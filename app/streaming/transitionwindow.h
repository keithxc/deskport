#pragma once
#include <SDL.h>
#include <QString>
#include "waylandloading.h"

constexpr int DeskPortRecallWindow = 105;
inline void recallDesktopWindow(SDL_Window* window) {
    if (!window) return;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(window);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
}

// Owns the existing native window between streaming sessions. All calls are made
// by the current SDL event owner; ownership moves only after its thread exits.
class TransitionWindow {
public:
    TransitionWindow(SDL_Window* window, const QString& text);
    ~TransitionWindow();
    void pump();
    bool cancelled() const { return m_Cancelled; }
    SDL_Window* takeWindow();
    bool rendering() const { return bool(m_Renderer) || (m_Wayland && m_Wayland->valid()); }
    const QImage& frame() const { return m_Frame; }
    unsigned presentedFrames() const { return m_PresentedFrames; }
private:
    void clearRenderer();
    SDL_Window* m_Window;
    SDL_Renderer* m_Renderer = nullptr;
    std::unique_ptr<WaylandLoading> m_Wayland;
    QString m_Label;
    QImage m_Frame;
    bool m_Cancelled = false;
    unsigned m_PresentedFrames = 0;
};
