#!/usr/bin/env python3
"""Apply common smart streaming to both pinned host source revisions."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(name, old, new):
    p = root / name
    source = p.read_text()
    if new in source: return
    if source.count(old) != 1:
        raise SystemExit(f'Smart stream anchor mismatch: {name}: {old[:80]!r}')
    p.write_text(source.replace(old, new))

# Add opt-in multi-source wake subscriptions to both upstream mailbox revisions.
# Existing queue/event behavior and payload ownership stay unchanged. A separate
# stop subscription lets capture wait without waking on its own image raises.
path = root / 'src/thread_safe.h'
source = path.read_text()
if 'void deskport_watch(' not in source:
    source = source.replace('#include "utility.h"', '#include "utility.h"\n#include "deskport/common/smartstream.h"')
    for kind, end in (('event_t', '  template<class T>\n  class alarm_raw_t'),
                      ('queue_t', '  template<class T>\n  class shared_t')):
        start = source.index('  class ' + kind + ' {')
        finish = source.index(end, start)
        block = source[start:finish]
        block = block.replace('  public:\n', '''  public:
    void deskport_watch(const std::shared_ptr<deskport::CadenceWake> &wake, bool stop_only = false) {
      std::lock_guard lock {_lock};
      (stop_only ? deskport_stops : deskport_changes).watch(wake);
    }
''', 1)
        block = block.replace('      _cv.notify_all();', '      deskport_changes.notify();\n      _cv.notify_all();')
        block = block.replace('      _continue = false;', '      _continue = false;\n      deskport_stops.notify();')
        # A waiter inspects these predicates between sampling a generation and
        # sleeping. Synchronize with raise/stop on both pinned implementations.
        predicate = 'return _continue && ' + ('(bool) _status;' if kind == 'event_t' else '!_queue.empty();')
        for read in (predicate, 'return _continue;'):
            block = block.replace('      ' + read, '      std::lock_guard lg {_lock};\n      ' + read)
        block = block.replace('    std::mutex _lock;', '    mutable std::mutex _lock;')
        block = block.replace('  private:\n', '''  private:
    deskport::CadenceListeners deskport_changes, deskport_stops;
''', 1)
        source = source[:start] + block + source[finish:]
    path.write_text(source)

patch('src/video.cpp', '''#include "globals.h"
''', '''#include "globals.h"
#include "deskport/common/smartstream.h"
#include <cstdlib>
''')

patch('src/video.cpp', '''    // set max frame time based on client-requested target framerate.
    double minimum_fps_target = (config::video.minimum_fps_target > 0.0) ? config::video.minimum_fps_target : (config.framerate / 2);
''', '''    const bool smart = std::getenv("DESKPORT_SMART_STREAMING") &&
        std::string_view(std::getenv("DESKPORT_SMART_STREAMING")) == "1";
    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");
    const auto smart_start = std::chrono::steady_clock::now();
    int last_target = config.framerate;
    auto last_encoded = smart_start;

    // set max frame time based on client-requested target framerate.
    double minimum_fps_target = (config::video.minimum_fps_target > 0.0) ? config::video.minimum_fps_target : (config.framerate / 2);
    if (smart) minimum_fps_target = std::min(1.0, double(config.framerate));
''')

patch('src/video.cpp', '''      bool requested_idr_frame = false;
''', '''      bool requested_idr_frame = false;
      const auto deskport_generation = deskport_wake->generation();
      const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - smart_start).count();
      if (shutdown_event->peek() || !images->running() || (reinit_event.peek() && frame_nr > 1)) break;
      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);
      if (smart && policy.frameRate(now_us / 1000) != last_target) {
        last_target = policy.frameRate(now_us / 1000);
        BOOST_LOG(info) << "DeskPort congestion frame-rate ceiling: " << last_target << " fps";
      }
''')

patch('src/video.cpp', '''    auto invalidate_ref_frames_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);
''', '''    auto invalidate_ref_frames_events = mail->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames);
    auto deskport_wake = std::make_shared<deskport::CadenceWake>();
    images->deskport_watch(deskport_wake);
    shutdown_event->deskport_watch(deskport_wake);
    idr_events->deskport_watch(deskport_wake);
    invalidate_ref_frames_events->deskport_watch(deskport_wake);
    reinit_event.deskport_watch(deskport_wake);
    congestion->deskport_watch(deskport_wake);
    mail->event<int>("deskport_rtt")->deskport_watch(deskport_wake);
    mail->event<int>("deskport_sent_kbps")->deskport_watch(deskport_wake);
    deskport::cadenceControls.watch(deskport_wake);
''')

patch('src/video.cpp', '''    auto initial_capture_ctx = capture_ctx_queue->pop();''', '''    capture_ctx_queue->deskport_watch(deskport::captureWake);
    switch_display_event->deskport_watch(deskport::captureWake);
    auto initial_capture_ctx = capture_ctx_queue->pop();''')
patch('src/video.cpp', '''    capture_ctxs.emplace_back(std::move(*initial_capture_ctx));''', '''    capture_ctxs.emplace_back(std::move(*initial_capture_ctx));
    capture_ctxs.back().images->deskport_watch(deskport::captureWake, true);''')
patch('src/video.cpp', '''          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));''', '''          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
          capture_ctxs.back().images->deskport_watch(deskport::captureWake, true);''')

patch('src/video.cpp', '''      // Encode at a minimum FPS to avoid image quality issues with static content
''', '''      // A producer can notify between this check and waitUntil(): the loop's
      // generation was sampled before inspecting any source, so it cannot be lost.
      if (!requested_idr_frame && !images->peek() && images->running() &&
          !shutdown_event->peek() && !(reinit_event.peek() && frame_nr > 1)) {
        auto deadline = last_encoded + std::chrono::duration_cast<std::chrono::steady_clock::duration>(max_frametime);
        if (std::chrono::steady_clock::now() < deadline) {
          deskport_wake->waitUntil(deskport_generation, deadline);
          continue; // Process input, IDR, rate controls and teardown before encoding.
        }
      }

      // Encode at a minimum FPS to avoid image quality issues with static content
''')

patch('src/video.cpp', '''        if (auto img = images->pop(max_frametime)) {
''', '''        // Leave the latest image in the mailbox while rate-limited. Consuming
        // and dropping it would lose a final text/cursor update on an idle screen.
        if (smart && images->peek()) {
          const auto captured_us = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - smart_start).count();
          if (!policy.admit(captured_us, requested_idr_frame)) {
            const auto ready = smart_start + std::chrono::microseconds(policy.readyUs(captured_us));
            deskport_wake->waitUntil(deskport_generation, ready);
            continue;
          }
        }
        if (auto img = images->pop(0ms)) {
''')

patch('src/video.cpp', '''        } else if (!images->running()) {
          break;
''', '''        } else if (!images->running()) {
          break;
        } else if (smart && !requested_idr_frame && !shutdown_event->peek() && !reinit_event.peek() &&
                   std::chrono::steady_clock::now() - last_encoded < max_frametime) {
          continue; // Process controls without encoding an early duplicate.
''')

patch('src/video.cpp', '''      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
''', '''      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {
''')

patch('src/stream.cpp', '''#include "sync.h"
''', '''#include "sync.h"
#include "deskport/common/smartstream.h"
''')

patch('src/stream.cpp', '''    server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
''', '''    // Existing Sunshine FEC feedback is scoped to the authenticated stream.
    // Only final, unrecoverable blocks count; repaired loss must not lower FPS.
    server->map(0x5502, [](session_t *session, const std::string_view &payload) {
      if (deskport::unrecoverableFec(reinterpret_cast<const unsigned char *>(payload.data()), payload.size())) {
        session->mail->event<int>("deskport_congestion")->raise(1);
      }
    });

    server->map(packetTypes[IDX_LOSS_STATS], [&](session_t *session, const std::string_view &payload) {
''')
