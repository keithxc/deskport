#!/usr/bin/env python3
"""Data saver cadence and FEC on both pinned host revisions (after remote-rate).

The viewer's data saver state comes from the launch request and then from the
optional `saver` field of video-rate (deskport::liveSaver). While it is on, the
encoder spends few frames on unchanged or slowly changing content and small
frames carry one parity shard on a clean link. See host/common/framecadence.h.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])

def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Data saver anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

# Launch state; a later video-rate command may change it.
patch('src/rtsp.cpp', '''      deskport::liveCeilingPermille = deskport::ceilingPermille(session.deskport_rate_cap, configuredBitrateKbps);''', '''      deskport::liveCeilingPermille = deskport::ceilingPermille(session.deskport_rate_cap, configuredBitrateKbps);
      deskport::liveSaver = session.deskport_saver ? 1 : 0;''')

# Asynchronous encoder (VideoToolbox and other FFmpeg encoders).
# Saver is an explicit session policy even when automatic streaming is disabled.
# A manual-mode toggle reconnects so disabling saver restores the original policy.
patch('src/video.cpp', '''    const bool smart = config.deskport_smart;''', '''    const bool smart = config.deskport_smart || config.deskport_saver;''')

# A capture lifecycle tick must not broadcast an old picture as new content.
# Retain one real image for encoders joining an idle shared capture, and release it
# immediately when capture exits so display reinitialization cannot wait on it.
patch('src/video.cpp', '''      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        KITTY_WHILE_LOOP''', '''      std::shared_ptr<platf::img_t> deskport_latest_image;
      auto push_captured_image_callback = [&](std::shared_ptr<platf::img_t> &&img, bool frame_captured) -> bool {
        if (frame_captured && img) deskport_latest_image = img;
        KITTY_WHILE_LOOP''')
patch('src/video.cpp', '''          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));''', '''          capture_ctxs.emplace_back(std::move(*capture_ctx_queue->pop()));
          if (deskport_latest_image) capture_ctxs.back().images->raise(deskport_latest_image);''')
patch('src/video.cpp', '''      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &display_cursor);

      if (artificial_reinit''', '''      auto status = disp->capture(push_captured_image_callback, pull_free_image_callback, &display_cursor);
      deskport_latest_image.reset();

      if (artificial_reinit''')

patch('src/video.cpp', '''    deskport::KeyframeGate deskport_key_gate;''', '''    deskport::KeyframeGate deskport_key_gate;
    deskport::SaverGate deskport_saver_gate;
    bool deskport_saver_pending = false;''')
patch('src/video.cpp', '''      if (smart) {
        if (auto event = activity->pop(0ms)) boost = *event;''', '''      const bool deskport_saving = smart && deskport::liveSaver.load() == 1;
      if (smart) {
        if (auto event = activity->pop(0ms)) boost = *event;''')
patch('src/video.cpp', '''        const int fps = std::max(deskport::activityFrameRate(boost, absolute_ms, policy.frameRate(now_us / 1000)),
                                 deskport::refineFrameRate(absolute_ms, deskport_last_change_ms, policy.frameRate(now_us / 1000)));''', '''        const int fps = deskport_saving
            ? deskport::saverFrameRate(absolute_ms, deskport_last_change_ms, boost, policy.frameRate(now_us / 1000))
            : std::max(deskport::activityFrameRate(boost, absolute_ms, policy.frameRate(now_us / 1000)),
                       deskport::refineFrameRate(absolute_ms, deskport_last_change_ms, policy.frameRate(now_us / 1000)));''')
patch('src/video.cpp', '''        if (config.deskport_saver && fps <= 1) max_frametime = std::chrono::duration<double, std::milli>(5000.0);''', '''        if (deskport_saving && fps <= 1) max_frametime = std::chrono::duration<double, std::milli>(double(deskport::kSaverIdleMs));''')
# Content frames: most images arrive while the loop waits in pop(), so the gate
# applies after a pop. A denied image is still converted (the encoder input holds
# the latest picture) and is sent as soon as the gate opens.
patch('src/video.cpp', '''          if (session->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            return;
          }
        } else if (!images->running()) {
          break;
        } else if (smart && !requested_idr_frame''', '''          if (session->convert(*img)) {
            BOOST_LOG(error) << "Could not convert image"sv;
            return;
          }
          const auto deskport_saver_us = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - smart_start).count();
          if (deskport_saving && !deskport_saver_gate.admit(deskport_saver_us, requested_idr_frame)) {
            deskport_saver_pending = true;
            continue;
          }
          deskport_saver_pending = false;
        } else if (!images->running()) {
          break;
        } else if (deskport_saving && deskport_saver_pending &&
                   deskport_saver_gate.admit(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - smart_start).count(), false)) {
          deskport_saver_pending = false; // the held picture goes out now
        } else if (smart && !requested_idr_frame''')

# Synchronous encoder (VAAPI and other shared-capture encoders).
patch('src/video.cpp', '''          if (ctx->config.deskport_smart) {''', '''          if (ctx->config.deskport_smart || deskport::liveSaver.load() == 1) {''')
patch('src/video.cpp', '''            const int floor = cadence.floor(now_us);''', '''            cadence.saver = deskport::liveSaver.load() == 1;
            const int floor = cadence.floor(now_us);''')

# FEC: remember the last unrecoverable block and relax the parity minimum.
patch('src/stream.cpp', '''    std::atomic<int> deskport_remote {-1};''', '''    std::atomic<int> deskport_remote {-1};
    std::atomic<std::int64_t> deskport_unrecoverable_ms {-deskport::kSaverCleanMs};''')
patch('src/stream.cpp', '''        session->mail->event<int>("deskport_congestion")->raise(1);''', '''        session->mail->event<int>("deskport_congestion")->raise(1);
        session->deskport_unrecoverable_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();''')
patch('src/stream.cpp', '''session->config.minRequiredFecPackets''', '''deskport::saverMinParity(deskport::liveSaver.load() == 1, session->config.minRequiredFecPackets,
              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
              session->deskport_unrecoverable_ms.load())''')
