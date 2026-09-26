#pragma once
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace deskport {
// Retain devices up to peak simultaneous use. Each lease is exclusive; callers
// must clear input state before returning it, or reset a device that cannot be
// cleared. Releasing a lease allocates nothing, including during stack unwinding.
template<class Device> class reusable_device_pool {
  struct slot { std::unique_ptr<Device> device; bool leased = false; };
  std::mutex mutex;
  std::vector<std::unique_ptr<slot>> slots;
public:
  class lease {
    friend class reusable_device_pool;
    reusable_device_pool* pool = nullptr;
    slot* entry = nullptr;
    lease(reusable_device_pool* p, slot* s) : pool(p), entry(s) {}
    void release() {
      if (!entry) return;
      std::lock_guard lock(pool->mutex);
      entry->leased = false;
      entry = nullptr;
    }
  public:
    lease() = default;
    lease(const lease&) = delete;
    lease& operator=(const lease&) = delete;
    lease(lease&& other) noexcept : pool(other.pool), entry(std::exchange(other.entry, nullptr)) {}
    lease& operator=(lease&& other) noexcept {
      if (this != &other) { release(); pool = other.pool; entry = std::exchange(other.entry, nullptr); }
      return *this;
    }
    lease& operator=(std::unique_ptr<Device> device) { entry->device = std::move(device); return *this; }
    ~lease() { release(); }
    explicit operator bool() const { return entry && bool(entry->device); }
    Device* operator->() const { return entry->device.get(); }
    void reset() { if (entry) entry->device.reset(); }
  };
  lease acquire() {
    std::lock_guard lock(mutex);
    for (auto& entry : slots) if (!entry->leased) {
      entry->leased = true;
      return lease(this, entry.get());
    }
    auto entry = std::make_unique<slot>();
    entry->leased = true;
    auto* result = entry.get();
    slots.push_back(std::move(entry));
    return lease(this, result);
  }
};
}
