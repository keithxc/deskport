#!/usr/bin/env python3
"""Reuse only the Wayland connection used for DMA-BUF capability queries."""
from pathlib import Path
import sys

source = Path(sys.argv[1])/'src/platform/linux/pipewire.cpp'
text = source.read_text()
marker = '// DeskPort stable native identity for EGL modifier queries.'
if marker in text:
    if 'egl::make_display(query_display->get())' not in text:
        raise SystemExit('Partial EGL query lifetime overlay')
    raise SystemExit(0)
old = '''    int get_dmabuf_modifiers() {
      if (wl_display.init() < 0) {
        return -1;
      }

      auto egl_display = egl::make_display(wl_display.get());'''
new = '''    int get_dmabuf_modifiers() {
      // DeskPort stable native identity for EGL modifier queries.
      // EGL keeps identity mappings after eglTerminate. Serialize the short
      // query, keep one native connection, and still refresh formats each time.
      static std::mutex query_mutex;
      static std::unique_ptr<wl::display_t> query_display;
      static std::pair<std::string, std::string> query_route;
      const auto environment = [](const char *name) {
        const char *value = std::getenv(name);
        return std::string(value ? value : "");
      };
      const auto route = std::make_pair(environment("XDG_RUNTIME_DIR"),
                                        environment("WAYLAND_DISPLAY"));
      std::lock_guard query_lock(query_mutex);
      if (query_display && (query_route != route ||
                            wl_display_roundtrip(query_display->get()) < 0)) {
        query_display.reset();
      }
      if (!query_display) {
        auto candidate = std::make_unique<wl::display_t>();
        if (candidate->init() < 0) return -1;
        query_display = std::move(candidate);
        query_route = route;
      }
      // The local EGL owner terminates before query_lock releases the connection.
      auto egl_display = egl::make_display(query_display->get());'''
if text.count(old) != 1:
    raise SystemExit('EGL query lifetime source anchor changed')
text = text.replace(old, new)
anchor = '#include <fstream>'
if text.count(anchor) != 1:
    raise SystemExit('EGL query lifetime include anchor changed')
text = text.replace(anchor, '#include <cstdlib>\n'+anchor+'\n#include <memory>\n#include <mutex>\n#include <string>\n#include <utility>')
source.write_text(text)
