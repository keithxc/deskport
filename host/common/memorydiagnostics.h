#pragma once
// Diagnostic builds opt in explicitly. Production builds contain only no-ops.
#if defined(DESKPORT_ENABLE_MEMORY_DIAGNOSTICS) && DESKPORT_ENABLE_MEMORY_DIAGNOSTICS
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace deskport_memory {
enum class kind { session, encoder, capture, pipewire_stream, image, dummy_pixels, video_frame, egl_context, count };
struct totals { std::uint64_t created{}, destroyed{}, live{}, bytes{}, peak_bytes{}; };
inline std::array<totals, static_cast<unsigned>(kind::count)> counters{};
inline std::uint64_t accounting_errors{};
inline std::mutex mutex;
inline std::mutex snapshot_mutex;
inline const char* output() {
  static const char* path = std::getenv("DESKPORT_MEMORY_DIAGNOSTICS");
  return path && path[0] == '/' ? path : nullptr;
}
inline void acquire(kind k, std::uint64_t bytes = 0) {
  if (!output()) return;
  std::lock_guard lock(mutex);
  auto& t = counters[static_cast<unsigned>(k)];
  ++t.created; ++t.live; t.bytes += bytes;
  if (t.bytes > t.peak_bytes) t.peak_bytes = t.bytes;
}
inline void release(kind k, std::uint64_t bytes = 0) {
  if (!output()) return;
  std::lock_guard lock(mutex);
  auto& t = counters[static_cast<unsigned>(k)];
  ++t.destroyed;
  if (!t.live || bytes > t.bytes) ++accounting_errors;
  if (t.live > 0) --t.live;
  t.bytes = bytes <= t.bytes ? t.bytes - bytes : 0;
}
struct lifetime {
  kind category;
  explicit lifetime(kind k) : category(k) { acquire(k); }
  lifetime(const lifetime& other) : category(other.category) { acquire(category); }
  lifetime(lifetime&& other) noexcept : category(other.category) { acquire(category); }
  lifetime& operator=(const lifetime&) = delete;
  ~lifetime() { release(category); }
};
template<class T> T* allocated_frame(T* p) { if (p) acquire(kind::video_frame); return p; }
inline void snapshot(const char* phase) {
  if (!output()) return;
  std::lock_guard serialize(snapshot_mutex);
  std::array<totals, static_cast<unsigned>(kind::count)> copy;
  std::uint64_t errors;
  { std::lock_guard lock(mutex); copy = counters; errors = accounting_errors; }
  const int fd = open(output(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return;
  struct stat info {};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size >= 4 * 1024 * 1024) { close(fd); return; }
  FILE* file = fdopen(fd, "a");
  if (!file) { close(fd); return; }
  static constexpr const char* names[] = {"session", "encoder", "capture", "pipewire_stream", "image", "dummy_pixels", "video_frame", "egl_context"};
  std::fprintf(file, "{\"phase\":\"%s\",\"pid\":%ld,\"resources\":{", phase, static_cast<long>(getpid()));
  for (unsigned i = 0; i < copy.size(); ++i) {
    const auto& t = copy[i];
    std::fprintf(file, "%s\"%s\":{\"created\":%llu,\"destroyed\":%llu,\"live\":%llu,\"bytes\":%llu,\"peak_bytes\":%llu}", i ? "," : "", names[i],
      static_cast<unsigned long long>(t.created), static_cast<unsigned long long>(t.destroyed), static_cast<unsigned long long>(t.live),
      static_cast<unsigned long long>(t.bytes), static_cast<unsigned long long>(t.peak_bytes));
  }
  std::fprintf(file, "}");
#ifdef __GLIBC__
  const auto heap = mallinfo2();
  std::fprintf(file, ",\"allocator\":{\"arena_bytes\":%zu,\"allocated_bytes\":%zu,\"free_bytes\":%zu,\"mapped_bytes\":%zu}",
    heap.arena, heap.uordblks, heap.fordblks, heap.hblkhd);
#endif
  std::fprintf(file, ",\"accounting_errors\":%llu}\n", static_cast<unsigned long long>(errors));
  std::fclose(file);
}
struct session_lifetime {
  session_lifetime() { acquire(kind::session); }
  session_lifetime(const session_lifetime&) = delete;
  ~session_lifetime() {
    release(kind::session);
    snapshot("session-destroyed");
  }
};
}

#else
#include <cstdint>
namespace deskport_memory {
enum class kind { session, encoder, capture, pipewire_stream, image, dummy_pixels, video_frame, egl_context, count };
inline void acquire(kind, std::uint64_t = 0) {}
inline void release(kind, std::uint64_t = 0) {}
struct lifetime { explicit lifetime(kind) {} };
template<class T> T* allocated_frame(T* p) { return p; }
inline void snapshot(const char*) {}
struct session_lifetime {};
}
#endif
