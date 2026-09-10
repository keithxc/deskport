#include "waylandloading.h"
#if defined(HAS_WAYLAND)
#include <QTemporaryFile>
#include <SDL_syswm.h>
#include <wayland-client.h>
#include <sys/mman.h>
#include <array>
#include <cstring>

struct WaylandLoading::State {
    struct Buffer {
        wl_buffer* buffer = nullptr;
        void* pixels = MAP_FAILED;
        size_t bytes = 0;
        QSize size;
        bool busy = false;
        ~Buffer() { clear(); }
        void clear() {
            if (buffer) wl_buffer_destroy(buffer);
            if (pixels != MAP_FAILED) munmap(pixels, bytes);
            buffer = nullptr; pixels = MAP_FAILED; busy = false;
        }
    };
    wl_display* display = nullptr;
    wl_surface* surface = nullptr;
    wl_shm* shm = nullptr;
    std::array<Buffer, 3> buffers;
    ~State() {
        for (auto& buffer : buffers) buffer.clear();
        if (shm) wl_shm_destroy(shm);
    }
    static void global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
        auto self = static_cast<State*>(data);
        if (std::strcmp(interface, wl_shm_interface.name) == 0)
        {
            self->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
            static const wl_shm_listener listener {[](void*, wl_shm*, uint32_t) {}};
            wl_shm_add_listener(self->shm, &listener, nullptr);
        }
    }
    static void removed(void*, wl_registry*, uint32_t) {}
    static void released(void* data, wl_buffer*) { static_cast<Buffer*>(data)->busy = false; }
};
WaylandLoading::WaylandLoading(SDL_Window* window) : m_State(std::make_unique<State>()) {
    SDL_SysWMinfo info {}; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WAYLAND) return;
    m_State->display = info.info.wl.display; m_State->surface = info.info.wl.surface;
    auto registry = wl_display_get_registry(m_State->display);
    static const wl_registry_listener listener {State::global, State::removed};
    wl_registry_add_listener(registry, &listener, m_State.get());
    wl_display_roundtrip(m_State->display);
    wl_registry_destroy(registry);
}
WaylandLoading::~WaylandLoading() = default;
bool WaylandLoading::valid() const { return m_State->shm && m_State->surface; }
bool WaylandLoading::present(const QImage& image) {
    if (!valid()) return false;
    for (auto& buffer : m_State->buffers) {
        if (buffer.busy) continue;
        if (!buffer.buffer || buffer.size != image.size()) {
            buffer.clear();
            QTemporaryFile file;
            const size_t bytes = image.sizeInBytes();
            if (!file.open() || !file.resize(bytes)) return false;
            buffer.pixels = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, file.handle(), 0);
            if (buffer.pixels == MAP_FAILED) return false;
            buffer.bytes = bytes; buffer.size = image.size();
            auto pool = wl_shm_create_pool(m_State->shm, file.handle(), bytes);
            buffer.buffer = wl_shm_pool_create_buffer(pool, 0, image.width(), image.height(), image.bytesPerLine(), WL_SHM_FORMAT_ARGB8888);
            wl_shm_pool_destroy(pool);
            static const wl_buffer_listener listener {State::released};
            wl_buffer_add_listener(buffer.buffer, &listener, &buffer);
        }
        std::memcpy(buffer.pixels, image.constBits(), buffer.bytes);
        buffer.busy = true;
        wl_surface_attach(m_State->surface, buffer.buffer, 0, 0);
        wl_surface_damage(m_State->surface, 0, 0, INT32_MAX, INT32_MAX);
        wl_surface_commit(m_State->surface);
        wl_display_flush(m_State->display);
        return true;
    }
    return false; // Compositor still owns all buffers; skip instead of overwriting.
}
#else
struct WaylandLoading::State {};
WaylandLoading::WaylandLoading(SDL_Window*) {}
WaylandLoading::~WaylandLoading() = default;
bool WaylandLoading::valid() const { return false; }
bool WaylandLoading::present(const QImage&) { return false; }
#endif
