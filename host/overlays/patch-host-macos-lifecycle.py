#!/usr/bin/env python3
"""Apply on-demand capture/input routing after the existing macOS overlays."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(relative, old, new):
    p = root / relative
    s = p.read_text()
    if new in s: return
    if old not in s: raise SystemExit(f'Unsupported lifecycle source: {relative}')
    p.write_text(s.replace(old, new, 1))
# Encoder probing runs again at launch/resume after the authenticated display ACK.
patch('src/main.cpp', 'if (video::probe_encoders()) {',
      'if (!std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE") && video::probe_encoders()) {')
patch('src/platform/macos/display.mm', '#include "src/deskport/macos/screen-video.h"',
      '#include "src/deskport/macos/screen-video.h"\n#include <fstream>\n#include <cstdlib>')
patch('src/platform/macos/display.mm', '    // Print all displays available with their names and ids', '''    if (const auto path = std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
      std::ifstream target(path);
      uint64_t ident = 0;
      if (!(target >> ident) || !ident || ident > UINT32_MAX || !CGDisplayIsActive(CGDirectDisplayID(ident))) {
        BOOST_LOG(error) << "No admitted DeskPort virtual display; refusing another capture target";
        return nullptr;
      }
      display->display_id = CGDirectDisplayID(ident);
    }
    // Print all displays available with their names and ids''')
patch('src/platform/macos/display.mm', '    std::vector<std::string> display_names;', '''    std::vector<std::string> display_names;
    if (const auto path = std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
      std::ifstream target(path); uint64_t ident = 0;
      if ((target >> ident) && ident && ident <= UINT32_MAX && CGDisplayIsActive(CGDirectDisplayID(ident)))
        display_names.emplace_back(std::to_string(ident));
      return display_names;
    }''')
hid = 'third-party/libvirtualhid/src/platform/macos/macos_backend.cpp'
patch(hid, '#include <stdexcept>', '#include <stdexcept>\n#include <fstream>')
# Construct idle input runtime without requiring a display. Actual events resolve
# the current identity atomically published before the display ACK.
patch(hid, '      const auto value = std::getenv("DESKPORT_CAPTURE_DISPLAY");', '''      if (std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) return CGMainDisplayID();
      const auto value = std::getenv("DESKPORT_CAPTURE_DISPLAY");''')
patch(hid, '        if (!state_->source || !state_->mouse_event) {', '''        if (const auto path = std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
          std::ifstream target(path); uint64_t ident = 0;
          if (!(target >> ident) || !ident || ident > UINT32_MAX || !CGDisplayIsActive(CGDirectDisplayID(ident)))
            return OperationStatus::failure(ErrorCode::backend_failure, "DeskPort input display is unavailable");
          state_->display = CGDirectDisplayID(ident);
          state_->display_scaling = MacosInputState::display_scaling_for(state_->display);
        }
        if (!state_->source || !state_->mouse_event) {''')
# Do not apply the generic device-mode preflight to our admitted virtual target.
# Actual capture still verifies its identity and dimensions against the compositor.
patch('src/platform/macos/display.mm',
      'if (!display_device::wake_display(display_name, 1s)) {',
      'if (!std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE") && !display_device::wake_display(display_name, 1s)) {')
patch('src/video.cpp', '      const auto devices {display_device::enumerate_devices()};', '''#ifdef __APPLE__
      if (std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) return true;
#endif
      const auto devices {display_device::enumerate_devices()};''')
# Context changes must reach an already-static SCK capture without waiting for
# its watchdog. The bridge only enqueues a NULL lifecycle callback on each SCK
# queue; image conversion and normal frame cadence are unchanged. Guard the
# bridge on Apple even if the patched source is later reused by another build.
for name, header in (('src/video.cpp', 'video.h'), ('src/input.cpp', 'input.h')):
    include = f'#include "{header}"'
    patch(name, include, include + '\n#ifdef __APPLE__\nextern "C" void DPNudgeCaptureLifecycle(void);\n#endif')

for name, event in (
    ('src/video.cpp', '    ref->capture_ctx_queue->raise(capture_ctx_t {images, config});'),
    ('src/video.cpp', '      images->stop();\n      shutdown_event->raise(true);'),
    ('src/video.cpp', '    capture_thread_ctx.capture_ctx_queue->stop();'),
    ('src/input.cpp', '      mail::man->event<int>(mail::switch_display)->raise(keyCode - VK_F1);'),
):
    indent = event[:len(event) - len(event.lstrip())]
    patch(name, event, event + '\n#ifdef __APPLE__\n' + indent + 'DPNudgeCaptureLifecycle();\n#endif')
patch(hid, '#include <fstream>', '#include <fstream>\n#include "deskport-admitted-display.h"')
patch(hid, '        const auto mode = CGDisplayCopyDisplayMode(display_id);', '''        if (std::getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
          int width, height, scale;
          if (dp_admitted_display_mode(display_id, &width, &height, &scale))
            return CGFloat(1.0) / scale;
          return 1.0;
        }
        const auto mode = CGDisplayCopyDisplayMode(display_id);''')
patch(hid, '          state_->display = CGDirectDisplayID(ident);', '''          int width, height, scale;
          if (!dp_admitted_display_mode(CGDirectDisplayID(ident), &width, &height, &scale))
            return OperationStatus::failure(ErrorCode::backend_failure, "DeskPort input mode is unavailable");
          state_->display = CGDirectDisplayID(ident);''')
patch('src/platform/macos/av_video.m', '#import "av_video.h"', '#import "av_video.h"\n#include "src/deskport/macos/admitted-display.h"')
patch('src/platform/macos/av_video.m', '''  if (!mode) {
    [self release];
    return nil;
  }''', '''  int width = 0, height = 0, scale = 0;
  if (getenv("DESKPORT_CAPTURE_DISPLAY_FILE")) {
    if (!dp_admitted_display_mode(displayID, &width, &height, &scale)) {
      if (mode) CFRelease(mode);
      [self release]; return nil;
    }
  } else if (mode) {
    width = (int)CGDisplayModeGetPixelWidth(mode);
    height = (int)CGDisplayModeGetPixelHeight(mode);
  } else { [self release]; return nil; }''')
patch('src/platform/macos/av_video.m', '  self.frameWidth = (int) CGDisplayModeGetPixelWidth(mode);', '  self.frameWidth = width;')
patch('src/platform/macos/av_video.m', '  self.frameHeight = (int) CGDisplayModeGetPixelHeight(mode);', '  self.frameHeight = height;')
patch('src/platform/macos/av_video.m', '  CFRelease(mode);\n\n  AVCaptureScreenInput', '  if (mode) CFRelease(mode);\n\n  AVCaptureScreenInput')
