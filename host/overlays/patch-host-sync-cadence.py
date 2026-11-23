#!/usr/bin/env python3
"""Use the same session policy in the synchronous (including VAAPI) encoder."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'src/video.cpp'
s = p.read_text()
if '#include "deskport/common/framecadence.h"' in s:
    raise SystemExit(0)
def patch(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'Sync cadence anchor mismatch: {old[:80]!r}')
    s = s.replace(old, new)
patch('#include "deskport/common/inputactivity.h"', '#include "deskport/common/inputactivity.h"\n#include "deskport/common/framecadence.h"')
patch('  struct sync_session_ctx_t {', '''  struct sync_session_ctx_t {
    safe::mail_t deskport_mail;''')
patch('      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {', '''      ref->encode_session_ctx_queue.raise(sync_session_ctx_t {
        mail,''')
patch('  struct sync_session_t {', '''  struct sync_session_t {
    safe::mail_raw_t::event_t<deskport::ActivityBoost> deskport_activity;
    safe::mail_raw_t::event_t<int> deskport_congestion;
    std::optional<deskport::FrameCadence> deskport_cadence;
    std::optional<std::chrono::steady_clock::time_point> deskport_timestamp;
    std::chrono::steady_clock::time_point deskport_manual_encoded {};
    int deskport_last_floor = 0;
    int deskport_last_ceiling = 0;''')
patch('    encode_session.session = std::move(session);', '''    encode_session.session = std::move(session);
    ctx.shutdown_event->deskport_watch(deskport::captureWake);
    ctx.idr_events->deskport_watch(deskport::captureWake);
    // mail_raw_t owns weak references: retain the channels for this session.
    encode_session.deskport_activity = ctx.deskport_mail->event<deskport::ActivityBoost>("deskport_activity");
    encode_session.deskport_congestion = ctx.deskport_mail->event<int>("deskport_congestion");
    encode_session.deskport_activity->deskport_watch(deskport::captureWake);
    encode_session.deskport_congestion->deskport_watch(deskport::captureWake);
    encode_session.deskport_cadence.emplace(ctx.config.framerate,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());''')
patch('          if (ctx->idr_events->peek()) {', '''          const bool deskport_recovery = ctx->idr_events->peek();
          if (deskport_recovery) {''')
old='''          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (img) {
            frame_timestamp = img->frame_timestamp;
          }
'''
patch(old, '''          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (frame_captured && img) {
            pos->deskport_timestamp = img->frame_timestamp;
          }
          if (ctx->config.deskport_smart) {
            auto &cadence = *pos->deskport_cadence;
            const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (pos->deskport_congestion->pop(0ms))
              cadence.policy.loss(now_us / 1000);
            if (auto boost = pos->deskport_activity->pop(0ms))
              cadence.boost = *boost;
            const int floor = cadence.floor(now_us);
            const int ceiling = cadence.policy.frameRate(now_us / 1000);
            if (floor != pos->deskport_last_floor || ceiling != pos->deskport_last_ceiling) {
              BOOST_LOG(info) << "DeskPort input cadence: " << floor << " fps; congestion ceiling: " << ceiling;
              pos->deskport_last_floor = floor;
              pos->deskport_last_ceiling = ceiling;
            }
            if (!cadence.admit(now_us, frame_captured, deskport_recovery)) {
              deskport::captureDeadline = std::min(deskport::captureDeadline,
                  std::chrono::steady_clock::time_point(std::chrono::microseconds(cadence.readyUs(now_us))));
              ++pos;
              continue;
            }
            frame_timestamp = pos->deskport_timestamp;
            pos->deskport_timestamp.reset();
          } else if (img) {
            frame_timestamp = img->frame_timestamp;
          }
          // Lifecycle/control notifications wake every mode. Manual streaming
          // keeps the original one-second idle timeout rather than encoding a
          // duplicate for each such notification (real frames and IDR bypass).
          if (deskport::pipewireControlTicks && !ctx->config.deskport_smart && deskport::liveSaver.load() != 1 &&
              !frame_captured && !deskport_recovery && pos->deskport_manual_encoded != std::chrono::steady_clock::time_point {}) {
            const auto deadline = pos->deskport_manual_encoded + 1s;
            if (std::chrono::steady_clock::now() < deadline) {
              deskport::captureDeadline = std::min(deskport::captureDeadline, deadline);
              ++pos;
              continue;
            }
          }
''')
patch('''    auto ec = platf::capture_e::ok;
''', '''    encode_session_ctx_queue.deskport_watch(deskport::captureWake);
    switch_display_event->deskport_watch(deskport::captureWake);
    auto deskport_schedule_guard = util::fail_guard([] {
      deskport::captureDeadline = std::chrono::steady_clock::time_point::max();
    });
    auto ec = platf::capture_e::ok;
''')
patch('''      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        while (encode_session_ctx_queue.peek()) {''', '''      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        deskport::captureDeadline = std::chrono::steady_clock::time_point::max();
        while (encode_session_ctx_queue.peek()) {''')
patch('''          pos->session->request_normal_frame();''', '''          if (pos->deskport_cadence) {
            const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            pos->deskport_manual_encoded = std::chrono::steady_clock::now();
            // Manual streams retain the upstream one-second idle refresh.
            const auto ready_us = ctx->config.deskport_smart || deskport::liveSaver.load() == 1
                ? pos->deskport_cadence->readyUs(now_us) : now_us + 1000000;
            deskport::captureDeadline = std::min(deskport::captureDeadline,
                std::chrono::steady_clock::time_point(std::chrono::microseconds(ready_us)));
          }
          pos->session->request_normal_frame();''')
p.write_text(s)
