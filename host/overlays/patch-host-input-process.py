#!/usr/bin/env python3
"""Inject macOS desktop input from a separate host process.

Held keys repeated on the host while data saver was on, although the input
module itself was unchanged. Sunshine now keeps connection, decryption,
activity tracking and encoding; keyboard, text, mouse and scroll packets go to
`Sunshine <config> --deskport-input <fd>`, which runs Sunshine's own input
module (key repeat, modifiers, cursor mapping). See host/macos/inputprocess-host.h.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])

def patch(name, old, new, count=1):
    path = root / name
    source = path.read_text()
    if new in source:
        return
    if source.count(old) != count:
        raise SystemExit(f'Input process anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

include = '#include "deskport/macos/inputprocess-host.h"'
for name, anchor in (('src/main.cpp', '#include "video.h"'), ('src/stream.cpp', '#include "stream.h"'),
                     ('src/video.cpp', '#include "video.h"')):
    patch(name, anchor, anchor + '\n#ifdef __APPLE__\n  ' + include + '\n#endif', 1)

# Helper command and start-up, after Sunshine's own input initialization.
patch('src/main.cpp', '''  {"version"sv, [](const char *name, int argc, char **argv) {
     return args::version();
   }},''', '''  {"version"sv, [](const char *name, int argc, char **argv) {
     return args::version();
   }},
#ifdef __APPLE__
  {"deskport-input"sv, [](const char *name, int argc, char **argv) {
     return deskport::input_process::run(argc, argv);
   }},
#endif''')
patch('src/main.cpp', '''  auto input_deinit_guard = input::init();
''', '''  auto input_deinit_guard = input::init();
#ifdef __APPLE__
  deskport::input_process::Host::get().start();
#endif
''')

# Both authenticated input paths, after activity has been observed.
patch('src/stream.cpp', '''          input::passthrough(session->input, std::move(plaintext));''', '''#ifdef __APPLE__
          deskport::input_process::passthrough(session->input, std::move(plaintext));
#else
          input::passthrough(session->input, std::move(plaintext));
#endif''', 2)
patch('src/stream.cpp', '''      input::reset(session.input);
''', '''      input::reset(session.input);
#ifdef __APPLE__
      deskport::input_process::Host::get().reset();
#endif
''')

# Absolute mouse mapping follows the captured display in the helper too.
patch('src/video.cpp', '    ctx.touch_port_events->raise(make_port(disp, ctx.config));', '''#ifdef __APPLE__
    deskport::input_process::raisePort(ctx.touch_port_events, make_port(disp, ctx.config));
#else
    ctx.touch_port_events->raise(make_port(disp, ctx.config));
#endif''')
patch('src/video.cpp', '      touch_port_event->raise(make_port(display.get(), config));', '''#ifdef __APPLE__
      deskport::input_process::raisePort(touch_port_event, make_port(display.get(), config));
#else
      touch_port_event->raise(make_port(display.get(), config));
#endif''')

# Eliminate macOS per-motion CGEventCreate WindowServer IPC and synchronous
# CGWarpMouseCursorPosition suppression stalls during continuous pointer motion.
macos_backend = root / 'third-party/libvirtualhid/src/platform/macos/macos_backend.cpp'
if macos_backend.exists():
    patch('third-party/libvirtualhid/src/platform/macos/macos_backend.cpp',
          '          scroll_lines_per_detent_value {read_scroll_lines_per_detent(scrollwheel_scaling)} {}',
          '''          scroll_lines_per_detent_value {read_scroll_lines_per_detent(scrollwheel_scaling)} {
        if (source) {
          CGEventSourceSetLocalEventsSuppressionInterval(source, 0.0);
        }
        if (keyboard_source) {
          CGEventSourceSetLocalEventsSuppressionInterval(keyboard_source, 0.0);
        }
      }''')
    patch('third-party/libvirtualhid/src/platform/macos/macos_backend.cpp',
          '''      OperationStatus submit_relative_motion(std::int32_t delta_x, std::int32_t delta_y) {
        const auto current = current_location();
        const auto location = CGPoint {current.x + delta_x, current.y + delta_y};
        const auto motion = macos_mouse_motion(mouse_down_);
        return post_mouse(motion.button, motion.event_type, location, current, motion.event_type != kCGEventMouseMoved ? drag_click_count_ : 0);
      }

      OperationStatus submit_absolute_motion(const MouseEvent &event) {
        const auto display_bounds = CGDisplayBounds(state_->display);
        const auto location = absolute_mouse_location(event, display_bounds);
        const auto motion = macos_mouse_motion(mouse_down_);
        const bool dragging = motion.event_type != kCGEventMouseMoved;
        // Absolute drags need no warp; relative motion still uses it so the next
        // delta starts from the updated cursor position.
        return post_mouse(motion.button, motion.event_type, location, current_location(), dragging ? drag_click_count_ : 0, !dragging);
      }

      OperationStatus submit_button(const MouseEvent &event) {
        using enum ErrorCode;

        const auto button = macos_mouse_button(event.button);
        if (!button) {
          return OperationStatus::failure(unsupported_profile, "macOS mouse backend supports only left, middle, and right buttons");
        }

        mouse_down_[button->index] = event.pressed;
        const auto now = std::chrono::steady_clock::now();
        const auto release_index = event.pressed ? 0U : 1U;
        const auto mouse_position = current_location();
        const auto click_count = now < last_mouse_event_[button->index][release_index] + multi_click_delay ? 2 : 1;

        last_mouse_event_[button->index][release_index] = now;
        if (event.pressed) {
          drag_click_count_ = click_count;
        }
        return post_mouse(button->button, event.pressed ? button->down_event : button->up_event, mouse_position, mouse_position, click_count);
      }''',
          '''      OperationStatus submit_relative_motion(std::int32_t delta_x, std::int32_t delta_y) {
        const auto now = std::chrono::steady_clock::now();
        const bool resync = !cursor_location_ || now - last_motion_time_ > std::chrono::milliseconds(80);
        const auto current = resync ? current_location() : *cursor_location_;
        const auto display_bounds = CGDisplayBounds(state_->display);
        const auto raw_location = CGPoint {current.x + delta_x, current.y + delta_y};
        const auto clamped = CGPoint {
          std::clamp(raw_location.x, display_bounds.origin.x, display_bounds.origin.x + display_bounds.size.width - 1),
          std::clamp(raw_location.y, display_bounds.origin.y, display_bounds.origin.y + display_bounds.size.height - 1)
        };
        cursor_location_ = clamped;
        last_motion_time_ = now;
        const auto motion = macos_mouse_motion(mouse_down_);
        const bool cross_display = resync && !CGRectContainsPoint(display_bounds, current);
        return post_mouse(motion.button, motion.event_type, raw_location, current,
                          motion.event_type != kCGEventMouseMoved ? drag_click_count_ : 0, cross_display);
      }

      OperationStatus submit_absolute_motion(const MouseEvent &event) {
        const auto now = std::chrono::steady_clock::now();
        const bool resync = !cursor_location_ || now - last_motion_time_ > std::chrono::milliseconds(80);
        const auto previous = resync ? current_location() : *cursor_location_;
        const auto display_bounds = CGDisplayBounds(state_->display);
        const auto location = absolute_mouse_location(event, display_bounds);
        const auto clamped = CGPoint {
          std::clamp(location.x, display_bounds.origin.x, display_bounds.origin.x + display_bounds.size.width - 1),
          std::clamp(location.y, display_bounds.origin.y, display_bounds.origin.y + display_bounds.size.height - 1)
        };
        cursor_location_ = clamped;
        last_motion_time_ = now;
        const auto motion = macos_mouse_motion(mouse_down_);
        const bool dragging = motion.event_type != kCGEventMouseMoved;
        const bool cross_display = !dragging && resync && !CGRectContainsPoint(display_bounds, previous);
        return post_mouse(motion.button, motion.event_type, location, previous, dragging ? drag_click_count_ : 0, cross_display);
      }

      OperationStatus submit_button(const MouseEvent &event) {
        using enum ErrorCode;

        const auto button = macos_mouse_button(event.button);
        if (!button) {
          return OperationStatus::failure(unsupported_profile, "macOS mouse backend supports only left, middle, and right buttons");
        }

        mouse_down_[button->index] = event.pressed;
        const auto now = std::chrono::steady_clock::now();
        const auto release_index = event.pressed ? 0U : 1U;
        const auto mouse_position = (cursor_location_ && now - last_motion_time_ <= std::chrono::milliseconds(80))
            ? *cursor_location_ : current_location();
        cursor_location_ = mouse_position;
        last_motion_time_ = now;
        const auto click_count = now < last_mouse_event_[button->index][release_index] + multi_click_delay ? 2 : 1;

        last_mouse_event_[button->index][release_index] = now;
        if (event.pressed) {
          drag_click_count_ = click_count;
        }
        return post_mouse(button->button, event.pressed ? button->down_event : button->up_event, mouse_position, mouse_position, click_count, false);
      }''')
    patch('third-party/libvirtualhid/src/platform/macos/macos_backend.cpp',
          '      int drag_click_count_ = 1;  ///< Click state carried by drag events, as AppKit reports it.',
          '''      int drag_click_count_ = 1;  ///< Click state carried by drag events, as AppKit reports it.
      std::optional<CGPoint> cursor_location_;
      std::chrono::steady_clock::time_point last_motion_time_ {};''')
