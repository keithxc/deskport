#!/usr/bin/env python3
"""Own PipeWire probe/dummy pixels without deleting borrowed capture buffers."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
header = root / 'src/platform/linux/graphics.h'
source = root / 'src/platform/linux/pipewire.cpp'
h, s = header.read_text(), source.read_text()
# The newer portable host already supplies its own PipeWire image owner.
if 'struct img_descriptor_t: public egl::img_descriptor_t' in s and 'if (data && data_owned)' in s:
    raise SystemExit(0)
marker = 'std::unique_ptr<std::uint8_t[]> deskport_dummy_pixels;'
if marker in h:
    if 'img->data = descriptor->deskport_dummy_pixels.get();' not in s:
        raise SystemExit('Partial PipeWire memory overlay')
    raise SystemExit(0)
old = '    void reset() {\n      for (auto x = 0; x < 4; ++x) {'
new = '''    // Capture pixels can be borrowed from PipeWire. Only dummy/probe pixels
    // belong to this descriptor; never delete the generic img_t::data pointer.
    std::unique_ptr<std::uint8_t[]> deskport_dummy_pixels;

    void reset() {
      if (deskport_dummy_pixels) {
        if (data == deskport_dummy_pixels.get()) data = nullptr;
        deskport_dummy_pixels.reset();
      }
      for (auto x = 0; x < 4; ++x) {'''
if h.count(old) != 1: raise SystemExit('PipeWire descriptor reset anchor mismatch')
h = h.replace(old, new)
h = h.replace('#include <optional>', '#include <memory>\n#include <optional>', 1)
old = '      img->data = new std::uint8_t[img->height * img->row_pitch];'
new = '''      auto *descriptor = static_cast<egl::img_descriptor_t *>(img);
      descriptor->deskport_dummy_pixels = std::make_unique<std::uint8_t[]>(
        static_cast<std::size_t>(img->height) * img->row_pitch);
      img->data = descriptor->deskport_dummy_pixels.get();'''
if s.count(old) != 1: raise SystemExit('PipeWire dummy allocation anchor mismatch')
header.write_text(h)
source.write_text(s.replace(old, new))
