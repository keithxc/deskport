#!/usr/bin/env python3
"""Instrument the pinned Linux host's real resource owners (disabled by default)."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
header = Path(sys.argv[2])
marker = root/'deskport-memory-diagnostics.applied'
if marker.exists():
    if (root/'src/deskport/common/memorydiagnostics.h').read_bytes() != header.read_bytes():
        raise SystemExit('Memory diagnostics header changed: use a fresh source tree')
    raise SystemExit(0)
changes = {}
def edit(file, old, new, count=1):
    p=root/'src'/file
    s=changes.get(p,p.read_text())
    if s.count(old)!=count: raise SystemExit(f'Memory diagnostics anchor mismatch: {file}: {old!r}')
    changes[p]=s.replace(old,new)
def include(file):
    edit(file, '// local includes', '#include "src/deskport/common/memorydiagnostics.h"\n\n// local includes')
for file in ('stream.cpp','video.cpp','platform/linux/graphics.h','platform/linux/graphics.cpp','platform/linux/pipewire.cpp'):
    include(file)
edit('stream.cpp','  struct session_t {','  struct session_t {\n    deskport_memory::lifetime memory_lifetime{deskport_memory::kind::session};\n    ~session_t() { deskport_memory::snapshot("session-destroying"); }')
edit('stream.cpp','      BOOST_LOG(debug) << "Session ended"sv;','      deskport_memory::snapshot("session-joined");\n      BOOST_LOG(debug) << "Session ended"sv;')
edit('video.cpp','    avcodec_encode_session_t() = default;','    deskport_memory::lifetime memory_lifetime{deskport_memory::kind::encoder};\n    avcodec_encode_session_t() = default;')
edit('video.cpp','av_frame_alloc()', 'deskport_memory::allocated_frame(av_frame_alloc())',4)
edit('video.cpp','    av_frame_free(&frame);','    if (frame) deskport_memory::release(deskport_memory::kind::video_frame);\n    av_frame_free(&frame);')
edit('platform/linux/graphics.cpp','  av_frame_free(&frame);','  if (frame) deskport_memory::release(deskport_memory::kind::video_frame);\n  av_frame_free(&frame);')
edit('platform/linux/pipewire.cpp','  class pipewire_display_t: public platf::display_t {\n  public:', '  class pipewire_display_t: public platf::display_t {\n  public:\n    deskport_memory::lifetime memory_lifetime{deskport_memory::kind::capture};')
edit('platform/linux/pipewire.cpp','        stream_data.stream = pw_stream_new(core, "Sunshine Video Capture", props);','        stream_data.stream = pw_stream_new(core, "Sunshine Video Capture", props);\n        if (stream_data.stream) deskport_memory::acquire(deskport_memory::kind::pipewire_stream);')
edit('platform/linux/pipewire.cpp','          pw_stream_destroy(stream_data.stream);','          pw_stream_destroy(stream_data.stream);\n          deskport_memory::release(deskport_memory::kind::pipewire_stream);')
edit('platform/linux/graphics.h','    ~img_descriptor_t() {','    deskport_memory::lifetime memory_lifetime{deskport_memory::kind::image};\n    ~img_descriptor_t() {')
edit('platform/linux/graphics.h','    std::unique_ptr<std::uint8_t[]> deskport_dummy_pixels;','    std::unique_ptr<std::uint8_t[]> deskport_dummy_pixels;\n    std::size_t deskport_dummy_bytes = 0;')
edit('platform/linux/graphics.h','        deskport_dummy_pixels.reset();','        deskport_dummy_pixels.reset();\n        deskport_memory::release(deskport_memory::kind::dummy_pixels, deskport_dummy_bytes);\n        deskport_dummy_bytes = 0;')
edit('platform/linux/pipewire.cpp','''      descriptor->deskport_dummy_pixels = std::make_unique<std::uint8_t[]>(
        static_cast<std::size_t>(img->height) * img->row_pitch);''','''      const auto bytes = static_cast<std::size_t>(img->height) * img->row_pitch;
      auto pixels = std::make_unique<std::uint8_t[]>(bytes);
      if (descriptor->deskport_dummy_pixels)
        deskport_memory::release(deskport_memory::kind::dummy_pixels, descriptor->deskport_dummy_bytes);
      descriptor->deskport_dummy_pixels = std::move(pixels);
      descriptor->deskport_dummy_bytes = bytes;
      deskport_memory::acquire(deskport_memory::kind::dummy_pixels, bytes);''')
edit('platform/linux/graphics.cpp','    ctx_t ctx {display, raw_ctx};','    deskport_memory::acquire(deskport_memory::kind::egl_context);\n    ctx_t ctx {display, raw_ctx};')
edit('platform/linux/graphics.h','      eglDestroyContext(disp, ctx);','      eglDestroyContext(disp, ctx);\n      deskport_memory::release(deskport_memory::kind::egl_context);')
# Inspect after all session members (queues/broadcast references) are released.
# The first member is destroyed last, so use a dedicated session lifetime owner.
edit('stream.cpp','deskport_memory::lifetime memory_lifetime{deskport_memory::kind::session};\n    ~session_t() { deskport_memory::snapshot("session-destroying"); }','deskport_memory::session_lifetime memory_lifetime;')
for p,s in changes.items(): p.write_text(s)
target=root/'src/deskport/common/memorydiagnostics.h';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(header.read_bytes())
marker.write_text('Linux resource counters v1\n')
