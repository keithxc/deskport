#!/usr/bin/env python3
"""Adapt PipeWire idle detection/polling without reading GPU pixels back to CPU."""
from pathlib import Path
import sys
p = Path(sys.argv[1]) / 'src/platform/linux/pipewire.cpp'
s = p.read_text()
if 'bool deskport_smart = false;' in s:
    raise SystemExit(0)
def patch(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'Linux cadence anchor mismatch: {old[:80]!r}')
    s = s.replace(old, new)
patch('''      img_descriptor->pw_damage = (damage && damage->region.size.width > 0 && damage->region.size.height > 0) ? std::optional<bool>(true) : std::nullopt;''', '''      // SPA's first invalid region terminates the damage array. Distinguish
      // an explicitly empty array from missing metadata (which means unknown).
      img_descriptor->pw_damage = damage ? std::optional<bool>(spa_meta_region_is_valid(damage)) : std::nullopt;''')
patch('''      // calculate frame interval we should capture at''', '''      deskport_smart = config.deskport_smart || config.deskport_saver;
      // calculate frame interval we should capture at''')
patch('#include "src/video.h"', '#include "src/video.h"\n#include "src/utility.h"\n#include "src/deskport/common/smartstream.h"')
patch('''      auto deadline = std::chrono::steady_clock::now() + timeout;''', '''      auto deadline = std::chrono::steady_clock::now() + timeout;
      if (deskport_smart) deadline = std::min(deadline, std::chrono::steady_clock::now() + 100ms);
      deadline = std::min(deadline, deskport::captureDeadline);''')
patch('''      auto next_frame = std::chrono::steady_clock::now();''', '''      // Only PipeWire injects extra control callbacks into manual capture.
      const auto previous_ticks = std::exchange(deskport::pipewireControlTicks, true);
      auto restore_ticks = util::fail_guard([previous_ticks] { deskport::pipewireControlTicks = previous_ticks; });
      deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
      deskport_control_generation = deskport::captureWake->generation();
      // Advance before servicing controls, never at the next loop's top. Input
      // or resume arriving after this callback must wake the following wait.
      auto deskport_push = [&](std::shared_ptr<platf::img_t> &&img, bool captured) {
        deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
        deskport_control_generation = deskport::captureWake->generation();
        return push_captured_image_cb(std::move(img), captured);
      };
      auto next_frame = std::chrono::steady_clock::now();''')
pacing_old = '''        if (next_frame > now) {
          std::this_thread::sleep_until(next_frame);
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }'''
pacing_wait = '''        if (next_frame > now) {
          // Wake controls promptly even at a low negotiated FPS. Lifecycle
          // callbacks cannot consume/emit a PipeWire image before its real slot.
          while (std::chrono::steady_clock::now() < next_frame) {
            if (deskport::captureWake->waitUntil(deskport_control_generation, next_frame)) {
              std::shared_ptr<platf::img_t> control_img;
              if (!pull_free_image_cb(control_img)) return platf::capture_e::interrupted;
              if (!deskport_push(std::move(control_img), false)) return platf::capture_e::ok;
              if (shared_state->stream_dead.load()) break;
            }
          }
          if (shared_state->stream_dead.load()) continue;
          sleep_overshoot_logger.first_point(next_frame);
          sleep_overshoot_logger.second_point_now_and_log();
        }'''
if pacing_old in s:
    patch(pacing_old, pacing_wait)
else:
    # The newer pin delegates native pacing to a helper. Preserve its target
    # advancement and optional unpaced path, replacing only the blocking wait.
    patch('''          platf::handle_pacing(next_frame, delay, sleep_overshoot_logger);''',
          '''          auto now = std::chrono::steady_clock::now();
''' + pacing_wait + '''
          next_frame += delay;
          if (next_frame < now) next_frame = now + delay;''')
patch('''if (!push_captured_image_cb(std::move(img_out), false))''', '''if (!deskport_push(std::move(img_out), false))''')
patch('''if (!push_captured_image_cb(std::move(img_out), true))''', '''if (!deskport_push(std::move(img_out), true))''')
patch('''    bool wait_for_frame(std::chrono::steady_clock::time_point deadline) {
      std::unique_lock<std::mutex> lock(pipewire.frame_mutex());

      bool success = pipewire.frame_cv().wait_until(lock, deadline, [&] {
        return pipewire.is_frame_ready() || shared_state->stream_dead.load();
      });

      if (success) {
        pipewire.set_frame_ready(false);
        return true;
      }
      return false;
    }''', '''    bool wait_for_frame(std::chrono::steady_clock::time_point deadline) {
      // Frame arrival and lifecycle/control events share a snapshot wake,
      // but the frame payload stays under PipeWire's own frame mutex.
      // Refresh after each wait so redundant damage events cannot spin until
      // the idle deadline. A control-only wake yields a lifecycle callback.
      {
        std::unique_lock<std::mutex> lock(pipewire.frame_mutex());
        if (shared_state->stream_dead.load()) return false;
        if (deskport_control_generation != deskport::captureWake->generation()) return false;
        if (pipewire.is_frame_ready()) {
          pipewire.set_frame_ready(false);
          deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
          return true;
        }
      }
      deskport::pipewireFrameOrControl->waitUntil(deskport_capture_generation, deadline);
      deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
      std::unique_lock<std::mutex> lock(pipewire.frame_mutex());
      // Let capture() run its existing dead-stream/subclass handling on the
      // next lifecycle tick; never spin on an invalid buffer until timeout.
      if (shared_state->stream_dead.load()) return false;
      if (deskport_control_generation != deskport::captureWake->generation()) return false;
      const bool ready = pipewire.is_frame_ready();
      if (ready) pipewire.set_frame_ready(false);
      return ready;
    }''')
# Notify the same signal for actual PipeWire frames and dead-stream changes;
# retain upstream CV notifications for native/manual callers.
s = s.replace('d->frame_cv.notify_all();', 'deskport::captureWake->notify();\n            d->frame_cv.notify_all();')
s = s.replace('d->frame_cv.notify_one();', 'deskport::pipewireFrameOrControl->notify();\n      d->frame_cv.notify_one();')
patch('''    bool is_buffer_redundant(const egl::img_descriptor_t *img) {''', '''    bool deskport_smart = false;
    std::uint64_t deskport_capture_generation = 0, deskport_control_generation = 0;
    bool is_buffer_redundant(const egl::img_descriptor_t *img) {
      // Only skip explicit unchanged content after a delivered frame. A sequence
      // gap might hide the last changed frame, so deliver it conservatively.
      if (deskport_smart && last_seq && img->seq && *img->seq == *last_seq + 1 &&
          img->pw_damage.has_value() && !*img->pw_damage) {
        last_seq = img->seq;
        return true;
      }''')
p.write_text(s)
