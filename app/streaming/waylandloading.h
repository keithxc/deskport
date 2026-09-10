#pragma once
#include <SDL.h>
#include <QImage>
#include <memory>

// Present a loading image on the existing Wayland surface. SDL's software
// renderer otherwise converts Vulkan windows to OpenGL and recreates them.
class WaylandLoading {
public:
    explicit WaylandLoading(SDL_Window* window);
    ~WaylandLoading();
    bool valid() const;
    bool present(const QImage& image);
private:
    struct State;
    std::unique_ptr<State> m_State;
};
