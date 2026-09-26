#!/usr/bin/env python3
"""Reuse synthetic pointer devices after clearing each client's input state."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
target = Path(sys.argv[1])
header = target/'src/deskport/windows/reusable-device-pool.h'
header.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(root/'host/windows/reusable-device-pool.h', header)

def replace(source, old, new):
    if source.count(old) != 1:
        raise SystemExit('Windows pointer lifetime anchor changed: '+old[:80])
    return source.replace(old, new)

path = target/'src/platform/virtualhid_input.h'
source = path.read_text()
if '// DeskPort reusable Windows pointer devices' not in source:
    source = replace(source, '#include "src/platform/common.h"', '''#include "src/platform/common.h"
#ifdef _WIN32
#include "src/deskport/windows/reusable-device-pool.h"
#endif''')
    source = replace(source, '    std::unique_ptr<lvh::Runtime> runtime;', '''#ifdef _WIN32
    // DeskPort reusable Windows pointer devices
    using touch_pool_t = deskport::reusable_device_pool<lvh::Touchscreen>;
    using pen_pool_t = deskport::reusable_device_pool<lvh::PenTablet>;
#endif
    std::unique_ptr<lvh::Runtime> runtime;''')
    source = replace(source, '    std::unique_ptr<lvh::Mouse> mouse;  ///< Shared virtual mouse.', '''    std::unique_ptr<lvh::Mouse> mouse;  ///< Shared virtual mouse.
#ifdef _WIN32
    touch_pool_t touch_pool;
    pen_pool_t pen_pool;
#endif''')
    source = replace(source, '    std::unique_ptr<lvh::Touchscreen> touch;  ///< Per-client touchscreen.\n    std::unique_ptr<lvh::PenTablet> pen;  ///< Per-client pen tablet.', '''#ifdef _WIN32
    ~client_context_t();
    input_context_t::touch_pool_t::lease touch;
    input_context_t::pen_pool_t::lease pen;
    bool pen_used = false;
#else
    std::unique_ptr<lvh::Touchscreen> touch;  ///< Per-client touchscreen.
    std::unique_ptr<lvh::PenTablet> pen;  ///< Per-client pen tablet.
#endif''')
    path.write_text(source)

path = target/'src/platform/virtualhid_input.cpp'
source = path.read_text()
if '// DeskPort return only cleared pointer devices' not in source:
    source = replace(source, '  client_context_t::client_context_t(input_context_t &input):\n      global {&input} {', '''  client_context_t::client_context_t(input_context_t &input):
      global {&input} {
#ifdef _WIN32
    touch = global->touch_pool.acquire();
    pen = global->pen_pool.acquire();
#endif''')
    source = replace(source, '    if (capabilities.supports_touchscreen) {', '    if (capabilities.supports_touchscreen && !touch) {')
    source = replace(source, '    if (capabilities.supports_pen_tablet) {', '    if (capabilities.supports_pen_tablet && !pen) {')
    source = replace(source, '  std::unique_ptr<lvh::Runtime> create_runtime(lvh::BackendKind backend) {', '''#ifdef _WIN32
  client_context_t::~client_context_t() {
    // DeskPort return only cleared pointer devices; concurrent clients retain
    // separate leases. Never reuse a device whose cancellation failed.
    bool touch_clean = true, pen_clean = true;
    if (touch) for (const auto id : active_touches) {
      if (!touch->cancel_contact(id).ok()) touch_clean = false;
    }
    if (pen) {
      for (const auto button : pressed_pen_buttons) {
        if (!pen->button(button, false).ok()) pen_clean = false;
      }
      if (pen_used) {
        auto state = pen->last_submitted_tool();
        state.transition = lvh::PointerTransition::cancel;
        if (!pen->place_tool(state).ok()) pen_clean = false;
      }
    }
    if (!touch_clean) touch.reset();
    if (!pen_clean) pen.reset();
  }
#endif

  std::unique_ptr<lvh::Runtime> create_runtime(lvh::BackendKind backend) {''')
    source = replace(source, '    const auto pen_buttons = static_cast<std::byte>(pen.penButtons);', '''#ifdef _WIN32
    context.pen_used = true;
#endif
    const auto pen_buttons = static_cast<std::byte>(pen.penButtons);''')
    path.write_text(source)
