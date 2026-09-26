#pragma once

// Mesa's CPU topology table is initialized per driver-library load, but affected
// drivers do not free it on unload. Encoder probes otherwise repeat that leak.
// Keep one instance alive until host shutdown. This owns no logical device,
// queue, surface or encoder and does not change subsequent device selection.
#include <dlfcn.h>
#include <vulkan/vulkan.h>

namespace deskport::vulkan {
class driver_lifetime {
 public:
  driver_lifetime() noexcept {
    library_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library_) return;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(dlsym(library_, "vkCreateInstance"));
    destroy_ = reinterpret_cast<PFN_vkDestroyInstance>(dlsym(library_, "vkDestroyInstance"));
    if (create && destroy_) {
      VkApplicationInfo app {};
      app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
      app.pApplicationName = "DeskPort driver lifetime";
      app.apiVersion = VK_API_VERSION_1_0;
      VkInstanceCreateInfo info {};
      info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      info.pApplicationInfo = &app;
      if (create(&info, nullptr, &instance_) == VK_SUCCESS) return;
    }
    instance_ = VK_NULL_HANDLE;
    dlclose(library_);
    library_ = nullptr;
  }
  ~driver_lifetime() {
    if (instance_) destroy_(instance_, nullptr);
    if (library_) dlclose(library_);
  }
  driver_lifetime(const driver_lifetime&) = delete;
  driver_lifetime& operator=(const driver_lifetime&) = delete;
  bool ready() const noexcept { return instance_ != VK_NULL_HANDLE; }

 private:
  void* library_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  PFN_vkDestroyInstance destroy_ = nullptr;
};

inline bool keep_drivers_loaded() noexcept {
  // C++ static initialization serializes concurrent encoder probes. Failure is
  // best-effort: preserve the original encoder path and its fallback behavior.
  static const driver_lifetime lifetime;
  return lifetime.ready();
}
}  // namespace deskport::vulkan
