#!/usr/bin/env python3
"""Close pinned Linux capture's modifier and Wayland proxy ownership gaps."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) / 'src/platform/linux'

def edit(name, changes, marker):
    path = root / name
    text = path.read_text()
    if marker in text:
        return
    for old, new in changes:
        if text.count(old) != 1:
            raise SystemExit(f'{name}: ownership anchor mismatch: {old[:70]}')
        text = text.replace(old, new, 1)
    path.write_text(text)

edit('pipewire.cpp', [
    ('#include <fstream>', '#include <fstream>\n#include <vector>'),
    ('    void query_dmabuf_formats(EGLDisplay egl_display) {',
     '    void query_dmabuf_formats(EGLDisplay egl_display) {\n      n_dmabuf_infos = 0;'),
    ('''        dmabuf_infos[n_dmabuf_infos].modifiers =
          static_cast<uint64_t *>(g_memdup2(mods.data(), sizeof(uint64_t) * dmabuf_infos[n_dmabuf_infos].n_modifiers));''',
     '''        auto &owned = deskport_modifiers[n_dmabuf_infos];
        owned.assign(mods.begin(), mods.begin() + dmabuf_infos[n_dmabuf_infos].n_modifiers);
        dmabuf_infos[n_dmabuf_infos].modifiers = owned.data();'''),
    ('''    std::array<struct dmabuf_format_info_t, MAX_DMABUF_FORMATS> dmabuf_infos;
    int n_dmabuf_infos;''',
     '''    // PipeWire borrows these arrays; destroy its stream before this storage.
    std::array<std::vector<uint64_t>, MAX_DMABUF_FORMATS> deskport_modifiers;
    std::array<struct dmabuf_format_info_t, MAX_DMABUF_FORMATS> dmabuf_infos {};
    int n_dmabuf_infos = 0;'''),
], 'deskport_modifiers')

edit('wayland.h', [
    ('    explicit monitor_t(wl_output *output);',
     '''    explicit monitor_t(wl_output *output);
    ~monitor_t();
    void release_wayland_objects();'''),
    ('    wl_output_listener wl_listener;',
     '    zxdg_output_v1 *deskport_xdg_output {nullptr};\n    wl_output_listener wl_listener;'),
    ('    interface_t() noexcept;', '    interface_t() noexcept;\n    ~interface_t();'),
    ('    display_internal_t display_internal;',
     '''    display_internal_t display_internal;
    util::safe_ptr<wl_registry, wl_registry_destroy> deskport_registry;'''),
], 'deskport_registry')

edit('wayland.cpp', [
    ('    display_internal.reset(wl_display_connect(display_name));',
     '    deskport_registry.reset();\n    display_internal.reset(wl_display_connect(display_name));'),
    ('    return wl_display_get_registry(display_internal.get());',
     '''    if (!deskport_registry) {
      deskport_registry.reset(wl_display_get_registry(display_internal.get()));
    }
    return deskport_registry.get();'''),
    ('  inline void monitor_t::xdg_name(', '''  monitor_t::~monitor_t() {
    release_wayland_objects();
  }

  void monitor_t::release_wayland_objects() {
    if (deskport_xdg_output) {
      zxdg_output_v1_destroy(deskport_xdg_output);
      deskport_xdg_output = nullptr;
    }
    if (output) {
      wl_output_destroy(output); // Bound at version 2, before wl_output.release.
      output = nullptr;
    }
  }

  inline void monitor_t::xdg_name('''),
    ('''    auto xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);
    zxdg_output_v1_add_listener(xdg_output, &xdg_listener, this);''',
     '''    deskport_xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, output);
    zxdg_output_v1_add_listener(deskport_xdg_output, &xdg_listener, this);'''),
    ('  void interface_t::listen(wl_registry *registry) {', '''  interface_t::~interface_t() {
    monitors.clear();
    if (screencopy_manager) zwlr_screencopy_manager_v1_destroy(screencopy_manager);
    if (dmabuf_interface) zwp_linux_dmabuf_v1_destroy(dmabuf_interface);
    if (output_manager) zxdg_output_manager_v1_destroy(output_manager);
  }

  void interface_t::listen(wl_registry *registry) {'''),
    ('    return std::move(interface.monitors);', '''    // Return metadata only: the local connection is about to be disconnected.
    for (auto &monitor : interface.monitors) monitor->release_wayland_objects();
    return std::move(interface.monitors);'''),
], 'monitor_t::release_wayland_objects')
