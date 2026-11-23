#!/usr/bin/env python3
"""Gate video transmission on the authenticated admission lease, on both pins."""
import difflib
import subprocess
import sys
from pathlib import Path
root = Path(sys.argv[1])
marker = root / 'deskport-video-pause.patch'
if marker.exists():
    subprocess.run(['git', 'apply', '--reverse', '--check', str(marker.resolve())], cwd=root, check=True)
    if '--revert' in sys.argv:
        subprocess.run(['git', 'apply', '--reverse', str(marker.resolve())], cwd=root, check=True)
        marker.unlink()
    raise SystemExit(0)
if '--revert' in sys.argv:
    raise SystemExit(0)
original, changes = {}, {}
def edit(name, anchor, replacement):
    path = root / 'src' / name
    source = changes.get(path, path.read_text())
    original.setdefault(path, source)
    if source.count(anchor) != 1:
        raise SystemExit(f'Expected one video pause anchor in {name}: {anchor!r}')
    changes[path] = source.replace(anchor, replacement, 1)
edit('rtsp.h', '  inline std::recursive_mutex mutex;', '''  // Separate from admission: terminating streams joins the sender while
  // holding admission. The sender must never acquire the admission mutex.
  inline std::mutex video_mutex;
  inline bool video_paused = false, video_needs_idr = false;
  // Encoders read this without the sender lock. While set they skip encoding:
  // unsent frames would still advance the frame index, and a jump past half of
  // the viewer's 16-bit frame window made it reject every resumed frame.
  inline std::atomic<bool> video_encode_paused {false};
  // A generation, rather than an observed paused flag, also catches a complete
  // pause/resume between two encoder iterations. Every session consumes it.
  inline std::atomic<unsigned long long> video_resume_generation {0};
  inline std::chrono::steady_clock::time_point video_resume_time;
  inline unsigned long long video_frames_sent = 0, video_frames_suppressed = 0;
  inline unsigned long long video_key_frames_sent = 0, video_last_frame = 0;
  inline void reset_video() {
    std::lock_guard lock {video_mutex};
    video_paused = false; video_needs_idr = false;
    ++video_resume_generation;
    video_encode_paused = false;
    deskport::cadenceControls.notify();
  }
  inline std::recursive_mutex mutex;''')
edit('rtsp.h', '#include <mutex>', '#include <mutex>\n#include <atomic>\n#include <chrono>\n#include "deskport/common/smartstream.h"')
edit('confighttp.cpp', '        if (action == "release") {', '''        if (action == "video") {
          if (deskport_admission::lease.empty() || input.value("lease", "") != deskport_admission::lease ||
              !input.contains("paused") || !input["paused"].is_boolean() || rtsp_stream::session_count() > 1) {
            output["status"] = false; output["code"] = "unauthorized";
          } else {
            std::lock_guard lock {deskport_admission::video_mutex};
            const bool paused = input["paused"].get<bool>();
            if (deskport_admission::video_paused && !paused) {
              deskport_admission::video_needs_idr = true;
              deskport_admission::video_resume_time = std::chrono::steady_clock::now();
              ++deskport_admission::video_resume_generation;
            }
            deskport_admission::video_paused = paused;
            // After video_needs_idr: a resumed encoder's first frame is a key
            // frame encoded after video_resume_time, which the sender accepts.
            deskport_admission::video_encode_paused = paused;
            deskport::cadenceControls.notify();
            output["status"] = true;
            output["paused"] = paused;
            output["framesSent"] = deskport_admission::video_frames_sent;
            output["framesSuppressed"] = deskport_admission::video_frames_suppressed;
          }
        } else if (action == "release") {''')
edit('confighttp.cpp', '            deskport_admission::certificate.clear(); deskport_admission::lease.clear();', '            deskport_admission::reset_video();\n            deskport_admission::certificate.clear(); deskport_admission::lease.clear();')
edit('confighttp.cpp', '            rtsp_stream::terminate_sessions();', '            rtsp_stream::terminate_sessions();\n            deskport_admission::reset_video();')
edit('confighttp.cpp', '    output["reserved"] = !deskport_admission::lease.empty();', '''    output["reserved"] = !deskport_admission::lease.empty();
    // Read-only telemetry under the same authenticated loopback API. This
    // observes the sender without sending another state-changing command.
    {
      std::lock_guard lock {deskport_admission::video_mutex};
      output["video"] = {{"paused", deskport_admission::video_paused},
                         {"framesSent", deskport_admission::video_frames_sent},
                         {"framesSuppressed", deskport_admission::video_frames_suppressed},
                         {"keyFramesSent", deskport_admission::video_key_frames_sent},
                         {"lastFrame", deskport_admission::video_last_frame}};
    }''')
edit('stream.cpp', '#include "stream.h"', '#include "stream.h"\n#include "rtsp.h"')
edit('video.cpp', '#include "video.h"', '#include "video.h"\n#include "rtsp.h"')
edit('video.cpp', '    deskport::cadenceControls.watch(deskport_wake);', '''    deskport::cadenceControls.watch(deskport_wake);
    // A moving desktop still replaces the mailbox while paused. Only controls
    // and lifecycle events may wake this encoder until video resumes.
    auto deskport_pause_wake = std::make_shared<deskport::CadenceWake>();
    images->deskport_watch(deskport_pause_wake, true);
    shutdown_event->deskport_watch(deskport_pause_wake);
    reinit_event.deskport_watch(deskport_pause_wake);
    deskport::cadenceControls.watch(deskport_pause_wake);''')
# Keep the capture mailbox untouched while paused: it already replaces its
# single slot with the freshest picture, including the final static change.
# Defer dummy conversion until an actual encode needs it (also after reinit).
edit('video.cpp', '''    void *channel_data
  ) {
    auto session = make_encode_session(disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));''',
     '''    void *channel_data
  ) {
    auto deskport_resume_generation = deskport_admission::video_resume_generation.load();
    auto session = make_encode_session(disp.get(), encoder, config, disp->width, disp->height, std::move(encode_device));''')
edit('video.cpp', '''    {
      // Load a dummy image into the AVFrame to ensure we have something to encode
      // even if we timeout waiting on the first frame. This is a relatively large
      // allocation which can be freed immediately after convert(), so we do this
      // in a separate scope.
      auto dummy_img = disp->alloc_img();
      if (!dummy_img || disp->dummy_img(dummy_img.get()) || session->convert(*dummy_img)) {
        return;
      }
    }''', '''    bool deskport_picture_ready = false;
    std::shared_ptr<platf::img_t> deskport_pause_image;
    std::optional<std::chrono::steady_clock::time_point> deskport_pause_timestamp;
''')
edit('video.cpp', '''      bool requested_idr_frame = false;
      const auto deskport_generation = deskport_wake->generation();''', '''      const auto deskport_generation = deskport_wake->generation();
      // Reinit must retire a paused encoder even before its first picture; the
      // normal unpaused path below still sends the bootstrap dummy when needed.
      if (shutdown_event->peek() || !images->running() ||
          (reinit_event.peek() && (frame_nr > 1 || deskport_admission::video_encode_paused))) break;
      if (deskport_admission::video_encode_paused) {
        // Sample the dedicated wake before rechecking every condition: resume
        // or teardown between the first check and this snapshot cannot be lost.
        const auto deskport_pause_generation = deskport_pause_wake->generation();
        if (shutdown_event->peek() || !images->running() || reinit_event.peek()) break;
        if (!deskport_admission::video_encode_paused) continue;
        deskport_pause_wake->waitUntil(deskport_pause_generation, std::chrono::steady_clock::time_point::max());
        continue;
      }
      const auto deskport_resume = deskport_admission::video_resume_generation.load();
      bool requested_idr_frame = deskport_resume != deskport_resume_generation;
      deskport_resume_generation = deskport_resume;''')

# Cadence has already replaced polling with multi-source event waits. A frame
# popped just as pause arrives needs one bounded fallback slot; a newer mailbox
# frame always wins on resume. Do not wait for another capture on a static screen.
edit('video.cpp', '      if (!requested_idr_frame && !images->peek() && images->running() &&',
     '      if (!requested_idr_frame && !images->peek() && !deskport_pause_image && images->running() &&')
edit('video.cpp', '          !shutdown_event->peek() && !(reinit_event.peek() && frame_nr > 1)) {',
     '          !shutdown_event->peek() && !reinit_event.peek()) {')
edit('video.cpp', '      if (!requested_idr_frame || images->peek()) {',
     '      if (!requested_idr_frame || images->peek() || deskport_pause_image) {')
edit('video.cpp', '        if (auto img = images->pop(0ms)) {', '''        auto deskport_image = images->pop(0ms);
        if (!deskport_image) deskport_image = std::move(deskport_pause_image);
        if (auto img = std::move(deskport_image)) {
          deskport_pause_image.reset(); // a newer mailbox value wins
          if (deskport_admission::video_encode_paused) {
            deskport_pause_image = std::move(img);
            continue;
          }
          deskport_picture_ready = true;''')
edit('video.cpp', '\n      std::optional<std::chrono::steady_clock::time_point> frame_timestamp;',
     '\n      auto frame_timestamp = deskport_pause_timestamp;')
edit('video.cpp', '''      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {''', '''      if (deskport_admission::video_encode_paused ||
          deskport_resume_generation != deskport_admission::video_resume_generation.load()) {
        if (frame_timestamp) deskport_pause_timestamp = frame_timestamp;
        continue;
      }
      if (!deskport_picture_ready) {
        auto dummy_img = disp->alloc_img();
        if (!dummy_img || disp->dummy_img(dummy_img.get()) || session->convert(*dummy_img)) return;
        deskport_picture_ready = true;
        if (shutdown_event->peek() || !images->running() || (reinit_event.peek() && frame_nr > 1)) break;
        if (deskport_admission::video_encode_paused ||
            deskport_resume_generation != deskport_admission::video_resume_generation.load()) continue;
      }
      last_encoded = std::chrono::steady_clock::now();
      if (encode(frame_nr++, *session, packets, channel_data, frame_timestamp)) {''')
edit('video.cpp', '      session->request_normal_frame();', '''      deskport_pause_timestamp.reset();
      session->request_normal_frame();''')

# Sync capture keeps one mutable current image. Remember per session that a
# paused update needs conversion, even when resume gets only an idle callback.
edit('video.cpp', '    auto ec = platf::capture_e::ok;', '''    // Some backends report idle/control ticks with a null image. Keep the
    // current capture image (or the initial dummy) until this capture run ends.
    auto deskport_sync_image = img;
    auto ec = platf::capture_e::ok;''')
edit('video.cpp', '''        deskport::captureDeadline = std::chrono::steady_clock::time_point::max();
        while (encode_session_ctx_queue.peek()) {''', '''        deskport::captureDeadline = std::chrono::steady_clock::time_point::max();
        if (frame_captured && img) deskport_sync_image = img;
        else img = deskport_sync_image;
        while (encode_session_ctx_queue.peek()) {''')
edit('video.cpp', '    int deskport_last_floor = 0;', '''    int deskport_last_floor = 0;
    bool deskport_picture_ready = false;
    bool deskport_pause_pending = false;
    unsigned long long deskport_resume_generation = deskport_admission::video_resume_generation.load();''')
edit('video.cpp', '''    if (session->convert(img)) {
      BOOST_LOG(error) << "Could not convert initial image"sv;
      return std::nullopt;
    }''', '''    if (!deskport_admission::video_encode_paused) {
      if (session->convert(img)) {
        BOOST_LOG(error) << "Could not convert initial image"sv;
        return std::nullopt;
      }
      encode_session.deskport_picture_ready = true;
    }''')
edit('video.cpp', '          const bool deskport_recovery = ctx->idr_events->peek();', '''          if (deskport_admission::video_encode_paused) {
            if (frame_captured && img) {
              pos->deskport_pause_pending = true;
              pos->deskport_timestamp = img->frame_timestamp;
            }
            ++pos;
            continue;
          }
          const auto deskport_resume = deskport_admission::video_resume_generation.load();
          const bool deskport_recovery = ctx->idr_events->peek() ||
              pos->deskport_resume_generation != deskport_resume;''')
edit('video.cpp', '''            pos->session->request_idr_frame();
            ctx->idr_events->pop();''', '''            pos->session->request_idr_frame();
            if (ctx->idr_events->peek()) ctx->idr_events->pop();''')
edit('video.cpp', '          if (frame_captured && pos->session->convert(*img)) {', '''          if ((frame_captured || pos->deskport_pause_pending || !pos->deskport_picture_ready) && pos->session->convert(*img)) {''')
edit('video.cpp', '''          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (frame_captured && img) {''', '''          pos->deskport_pause_pending = false;
          pos->deskport_picture_ready = true;
          std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
          if (frame_captured && img) {''')
edit('video.cpp', '''          } else if (img) {
            frame_timestamp = img->frame_timestamp;
          }''', '''          } else {
            frame_timestamp = pos->deskport_timestamp;
            pos->deskport_timestamp.reset();
          }''')
edit('video.cpp', '''          if (encode(ctx->frame_nr++, *pos->session, ctx->packets, ctx->channel_data, frame_timestamp)) {''', '''          if (deskport_admission::video_encode_paused ||
              deskport_resume != deskport_admission::video_resume_generation.load()) {
            if (frame_timestamp) pos->deskport_timestamp = frame_timestamp;
            ++pos;
            continue;
          }
          pos->deskport_resume_generation = deskport_resume;
          if (encode(ctx->frame_nr++, *pos->session, ctx->packets, ctx->channel_data, frame_timestamp)) {''')
edit('video.h', '    void *channel_data = nullptr;', '    const std::chrono::steady_clock::time_point deskport_encoded_at = std::chrono::steady_clock::now();\n    void *channel_data = nullptr;')
edit('stream.cpp', '      auto lowseq = session->video.lowseq;', '''      // Keep the lock through the complete frame send. A successful pause
      // acknowledgment therefore fences all video UDP sends (in-flight network
      // packets can still arrive). Audio and control retain their own threads.
      std::unique_lock video_lock {deskport_admission::video_mutex};
      if (deskport_admission::video_paused) {
        ++deskport_admission::video_frames_suppressed;
        continue;
      }
      if (deskport_admission::video_needs_idr) {
        if (!packet->is_idr() ||
            packet->deskport_encoded_at < deskport_admission::video_resume_time) {
          session->video.idr_events->raise(true);
          ++deskport_admission::video_frames_suppressed;
          continue;
        }
        deskport_admission::video_needs_idr = false;
      }
      auto lowseq = session->video.lowseq;''')
edit('stream.cpp', '        session->video.lowseq = lowseq;', '''        session->video.lowseq = lowseq;
        ++deskport_admission::video_frames_sent;
        if (packet->is_idr()) ++deskport_admission::video_key_frames_sent;
        deskport_admission::video_last_frame = packet->frame_index();''')
patch = ''.join(''.join(difflib.unified_diff(original[p].splitlines(True), s.splitlines(True),
    fromfile='a/' + str(p.relative_to(root)), tofile='b/' + str(p.relative_to(root)))) for p, s in changes.items())
for p, s in changes.items(): p.write_text(s)
marker.write_text(patch)
