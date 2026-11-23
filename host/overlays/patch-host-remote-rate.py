#!/usr/bin/env python3
"""Remote-path rate control, pacing and contiguous frame numbering.

Applies to both pinned host revisions after the smart-stream and encoder-policy
overlays. Live encoder bitrate is capability-gated: FFmpeg VideoToolbox
and native NVENC implement it; other backends keep their negotiated configuration.
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
        raise SystemExit(f'Remote rate anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

for name in ('video.cpp', 'stream.cpp'):
    patch('src/' + name, '#include "deskport/common/encoderpolicy.h"',
          '#include "deskport/common/encoderpolicy.h"\n#include "deskport/common/ratecontrol.h"')

# Live bitrate hook. The default reports "unsupported" so the policy can log it.
patch('src/video.h', '    virtual void request_normal_frame() = 0;', '''    virtual void request_normal_frame() = 0;

    virtual bool deskport_set_bitrate(int kbps, bool constrained, bool saver) {
      return false;
    }''')

patch('src/video.cpp', '#include "deskport/common/ratecontrol.h"', '''#include "deskport/common/ratecontrol.h"
#include "deskport/common/picturequality.h"
#ifdef __APPLE__
  #include "deskport/macos/vtbitrate.h"
#endif
#include <mutex>

namespace {
  // Survives the stop/resume used by adaptive resize; one host process.
  std::mutex deskport_rate_memory_mutex;
  deskport::RateMemory deskport_rate_memory;
  std::int64_t deskport_wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
}''')

patch('src/video.cpp', '    deskport::KeyframeRequests deskport_keys;', '''    deskport::KeyframeRequests deskport_keys;
    int deskport_initial_kbps = 0;
    std::int64_t deskport_previous_limit_bps = 0;
    std::int64_t deskport_last_pts = -1;
    std::int64_t deskport_pts_shift = 0;
    deskport::PictureQuality deskport_quality;
    bool deskport_saver_configured = false;

    bool deskport_set_bitrate(int kbps, bool constrained, bool saver) override {
#ifdef __APPLE__
      auto *vt = deskport::vt::session(avcodec_ctx.get());
      // QP 32 measurably shrinks content-change frames on a congested link.
      if (!avcodec_ctx || kbps <= 0 || kbps > 200000) return false;
      if (!deskport_initial_kbps) deskport_initial_kbps = int(avcodec_ctx->bit_rate / 1000);
      if (kbps > deskport_initial_kbps) return false;
      const auto bps = std::int64_t(kbps) * 1000;
      const auto previous = deskport_previous_limit_bps ? deskport_previous_limit_bps : avcodec_ctx->rc_max_rate;
      // Retain main's saver burst, cadence and quality bounds in one rollback transaction.
      const bool configured = saver || deskport_saver_configured;
      const int burst = saver ? (constrained ? 1 : 2) : 1;
      const bool applied = deskport::vt::apply(vt, bps,
          deskport::encoderMinQp(saver, constrained), previous, burst,
          configured ? deskport::encoderMaxQp(saver, constrained) : -1,
          configured ? (saver ? 8.0 : av_q2d(avcodec_ctx->framerate)) : 0);
      if (applied) {
        deskport_previous_limit_bps = bps * burst;
        deskport_saver_configured = saver;
        deskport_quality.configured(saver && !constrained &&
            deskport::sharpPictureBudget(kbps, avcodec_ctx->width, avcodec_ctx->height));
      }
      return applied;
#else
      return false;
#endif
    }''')

# Rate-controlled encoders may drop a frame. A gap in frame numbers makes the
# client treat it as network loss and request an IDR, so number only the frames
# actually produced. Recovery matching and frame timestamps use the original PTS.
patch('src/video.cpp', '''      if (av_packet && av_packet->pts == frame_nr) {
        packet->frame_timestamp = frame_timestamp;
      }
''', '''      if (av_packet && av_packet->pts == frame_nr) {
        packet->frame_timestamp = frame_timestamp;
      }
      if (av_packet) {
        if (session.deskport_last_pts >= 0 && av_packet->pts > session.deskport_last_pts + 1) {
          session.deskport_pts_shift += av_packet->pts - session.deskport_last_pts - 1;
        }
        session.deskport_last_pts = av_packet->pts;
        av_packet->pts -= session.deskport_pts_shift;
      }
''')

patch('src/video.cpp', '      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);', '''      const bool deskport_lost = smart && congestion->pop(0ms);
      if (deskport_lost) policy.loss(now_us / 1000);
      // Remote sessions receive RTT samples from the control thread; LAN
      // sessions never do, so they keep the negotiated bitrate.
      auto rtt = deskport_rtt->pop(0ms);
      if (smart && rtt) {
        if (!deskport_rate) {
          deskport_rate.emplace(deskport_ceiling_kbps, now_us / 1000);
          if (deskport_permille > 0) deskport_rate->setCeiling(deskport::applyPermille(deskport_ceiling_kbps, deskport_permille));
          int resume = 0;
          {
            std::lock_guard lock {deskport_rate_memory_mutex};
            resume = deskport_rate_memory.resumeFor(deskport_ceiling_kbps, deskport_wall_ms());
          }
          if (deskport_live && resume) deskport_rate->resume(resume);
          BOOST_LOG(info) << "DeskPort remote rate control: ceiling_kbps=" << deskport_ceiling_kbps
                          << ", floor_kbps=" << deskport_rate->floorKbps()
                          << ", resume_kbps=" << resume
                          << ", live_encoder=" << (deskport_live ? "yes" : "no");
        }
        deskport_rate->rtt(now_us / 1000, *rtt);
        deskport_last_rtt = *rtt;
      }
      // Live viewer ceiling (data saver) from the authenticated local API.
      if (smart) {
        const int permille = deskport::liveCeilingPermille.load();
        if (permille != deskport_permille) {
          deskport_permille = permille;
          const int capped = deskport::applyPermille(deskport_ceiling_kbps, permille);
          if (deskport_rate) {
            deskport_rate->setCeiling(capped);
          }
          BOOST_LOG(info) << "DeskPort live ceiling: kbps=" << capped << ", permille=" << permille;
        }
      }
      if (auto sent = deskport_sent->pop(0ms); deskport_rate && sent) deskport_rate->sent(*sent);
      if (deskport_rate && deskport_lost) deskport_rate->loss(now_us / 1000);
      // Apply the complete encoder policy on LAN and remote paths. A saver
      // toggle or a new congestion floor can change VT properties at equal kbps.
      if (smart && deskport_live) {
        const int kbps = deskport_rate ? deskport_rate->targetKbps(now_us / 1000)
          : deskport::applyPermille(deskport_ceiling_kbps, deskport_permille);
        const bool constrained = deskport_rate && deskport_rate->constrained();
        const bool saver = deskport::liveSaver.load() == 1;
        if (!deskport_applied_rate.matches(kbps, constrained, saver)) {
          if (!session->deskport_set_bitrate(kbps, constrained, saver)) {
            BOOST_LOG(warning) << "DeskPort live rate control: encoder rejected " << kbps << " kbps";
            deskport_live = false;
            deskport::liveEncoder = 0;
          } else {
            deskport_applied_rate.applied(kbps, constrained, saver);
            BOOST_LOG(info) << "DeskPort live rate: target_kbps=" << kbps << ", rtt_ms=" << deskport_last_rtt
                            << ", quality_floor=" << (constrained ? "on" : "off")
                            << ", saver=" << (saver ? "on" : "off")
                            << ", min_qp=" << deskport::encoderMinQp(saver, constrained)
                            << ", max_qp=" << deskport::encoderMaxQp(saver, constrained);
            mail->event<int>("deskport_rate_kbps")->raise(kbps);
            if (deskport_rate) {
              std::lock_guard lock {deskport_rate_memory_mutex};
              deskport_rate_memory.store(deskport_ceiling_kbps, kbps, deskport_wall_ms());
            }
          }
        }
        if (deskport_live) {
          deskport::appliedSaver = saver ? 1 : 0;
          deskport::appliedCeilingPermille = deskport_permille;
        }
      }''')

patch('src/video.cpp', '''    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");''', '''    deskport::StreamPolicy policy(config.framerate);
    auto congestion = mail->event<int>("deskport_congestion");
    auto deskport_rtt = mail->event<int>("deskport_rtt");
    auto deskport_sent = mail->event<int>("deskport_sent_kbps");
    std::optional<deskport::BitrateController> deskport_rate;
    const int deskport_ceiling_kbps = config::video.max_bitrate > 0 ? std::min(config.bitrate, config::video.max_bitrate) : config.bitrate;
    deskport::EncoderRateState deskport_applied_rate;
    int deskport_last_rtt = 0;
    bool deskport_live = false;
    std::int64_t deskport_last_change_ms = -1;''')

# Refine a still picture at the full cadence for a short window after the last
# captured change; the idle floor alone left scrolled text blurry for seconds.
patch('src/video.cpp', '''        const int fps = deskport::activityFrameRate(boost, absolute_ms, policy.frameRate(now_us / 1000));''', '''        const int fps = std::max(deskport::activityFrameRate(boost, absolute_ms, policy.frameRate(now_us / 1000)),
                                 deskport::refineFrameRate(absolute_ms, deskport_last_change_ms, policy.frameRate(now_us / 1000)));''')
patch('src/video.cpp', '''          frame_timestamp = img->frame_timestamp;
          if (session->convert(*img)) {''', '''          frame_timestamp = img->frame_timestamp;
          deskport_last_change_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
          if (session->convert(*img)) {''')

# Per-session transport state.
patch('src/stream.cpp', '''    deskport::EncodedWindow deskport_encoded;''', '''    deskport::EncodedWindow deskport_encoded;
    std::atomic<int> deskport_remote {-1};
    int deskport_pace_kbps = 0;
    std::chrono::steady_clock::time_point deskport_rtt_sample {};
    std::uint64_t deskport_sent_bytes = 0;
    std::chrono::steady_clock::time_point deskport_sent_since {};''')

# Sample the existing ENet control RTT for remote sessions only.
patch('src/stream.cpp', '''          if (!session->control.peer) {
            has_session_awaiting_peer = true;
          } else {''', '''          if (session->control.peer && session->deskport_remote.load(std::memory_order_relaxed) == 1 &&
              now - session->deskport_rtt_sample >= 500ms) {
            session->deskport_rtt_sample = now;
            session->mail->event<int>("deskport_rtt")->raise((int) session->control.peer->roundTripTime);
          }

          if (!session->control.peer) {
            has_session_awaiting_peer = true;
          } else {''')

# Remote sessions pace at a multiple of the live target instead of ~800 Mbps.
patch('src/stream.cpp', '''        size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;''', '''        size_t ratecontrol_packets_in_1ms = std::giga::num * 80 / 100 / 1000 / blocksize / 8;
        if (session->deskport_remote.load(std::memory_order_relaxed) < 0) {
          const auto address = session->video.peer.address();
          bool remote = false;
          if (address.is_v4()) {
            const auto bytes = address.to_v4().to_bytes();
            remote = deskport::remotePeer(bytes.data(), (int) bytes.size());
          } else if (address.is_v6()) {
            const auto bytes = address.to_v6().to_bytes();
            remote = deskport::remotePeer(bytes.data(), (int) bytes.size());
          }
          session->deskport_remote.store(remote ? 1 : 0, std::memory_order_relaxed);
          BOOST_LOG(info) << "DeskPort network path: " << (remote ? "remote" : "local");
        }
        const bool deskport_remote_paced = session->config.monitor.deskport_smart &&
            session->deskport_remote.load(std::memory_order_relaxed) == 1;
        if (deskport_remote_paced) {
          if (auto kbps = session->mail->event<int>("deskport_rate_kbps")->pop(0ms)) session->deskport_pace_kbps = *kbps;
        }
        const int deskport_pace_target = deskport_remote_paced && session->deskport_pace_kbps > 0
            ? session->deskport_pace_kbps
            : session->config.monitor.bitrate;
        // Pace UDP bursts on both Wi-Fi LAN and remote paths instead of 800 Mbps
        // 64-packet microbursts that overflow AP queues when Data Saver is off.
        ratecontrol_packets_in_1ms = deskport::remotePacketsPerMs(
            deskport_pace_target, blocksize, deskport_remote_paced && deskport::liveSaver.load() == 1);
        if (deskport_remote_paced) {
          // Actual video payload rate, once a second, for the rate controller.
          const auto sent_now = std::chrono::steady_clock::now();
          session->deskport_sent_bytes += payload.size();
          if (session->deskport_sent_since == std::chrono::steady_clock::time_point {}) {
            session->deskport_sent_since = sent_now;
          } else if (sent_now - session->deskport_sent_since >= 1s) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(sent_now - session->deskport_sent_since).count();
            session->mail->event<int>("deskport_sent_kbps")->raise(int(session->deskport_sent_bytes * 8 / std::max<long long>(1, ms)));
            session->deskport_sent_bytes = 0;
            session->deskport_sent_since = sent_now;
          }
        }''')
patch('src/stream.cpp', '''        send_batch_size = std::min<size_t>(64, send_batch_size);''', '''        send_batch_size = std::min<size_t>(64, send_batch_size);
        send_batch_size = std::min(send_batch_size, ratecontrol_packets_in_1ms);''')

# Data saver from the launch/resume request (absent from older clients: off).
patch('src/rtsp.h', '''    bool deskport_smart = true;''', '''    bool deskport_smart = true;
    bool deskport_saver = false;''')
patch('src/video.h', '''    bool deskport_smart = true;''', '''    bool deskport_smart = true;
    bool deskport_saver = false;''')
patch('src/nvhttp.cpp', '''    launch_session->deskport_smart = get_arg(args, "deskportSmart", "1") == "1";''', '''    launch_session->deskport_smart = get_arg(args, "deskportSmart", "1") == "1";
    launch_session->deskport_saver = get_arg(args, "deskportSaver", "0") == "1";''')
patch('src/rtsp.cpp', '''    config.monitor.deskport_smart = session.deskport_smart;''', '''    config.monitor.deskport_smart = session.deskport_smart;
    config.monitor.deskport_saver = session.deskport_saver;''')
# An unchanged picture is refreshed every 5 s instead of every second while
# saving data; a lost final frame is then noticed up to 5 s later.
patch('src/video.cpp', '''        max_frametime = std::chrono::duration<double, std::milli>(1000.0 / fps);''', '''        max_frametime = std::chrono::duration<double, std::milli>(1000.0 / fps);
        if (config.deskport_saver && fps <= 1) max_frametime = std::chrono::duration<double, std::milli>(5000.0);''')

# Coalesce recovery key frames while the previous one is still in flight.
patch('src/video.cpp', '''      while (invalidate_ref_frames_events->peek()) {
        if (auto frames = invalidate_ref_frames_events->pop(0ms)) {
          session->invalidate_ref_frames(frames->first, frames->second);
        }
      }

      if (idr_events->peek()) {
        requested_idr_frame = true;
        idr_events->pop();
      }''', '''      const auto deskport_key_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      // FFmpeg encoders answer reference invalidation with a key frame, so
      // both requests share one gate; true invalidation encoders are untouched.
      const bool deskport_idr_only = dynamic_cast<avcodec_encode_session_t *>(session.get()) != nullptr;
      while (invalidate_ref_frames_events->peek()) {
        if (auto frames = invalidate_ref_frames_events->pop(0ms)) {
          if (!deskport_idr_only || deskport_key_gate.allow(deskport_key_ms, deskport_last_rtt)) {
            session->invalidate_ref_frames(frames->first, frames->second);
            // FFmpeg answers invalidation with an IDR too. With data saver,
            // bypass the idle wait and refine as for an explicit request;
            // otherwise keep the original single encoder-driven IDR.
            if (deskport_idr_only && deskport::liveSaver.load() == 1) requested_idr_frame = true;
          }
        }
      }

      if (idr_events->peek() || requested_idr_frame) {
        if (idr_events->peek()) {
          idr_events->pop();
          // A coalesced explicit request cannot cancel accepted invalidation.
          if (deskport_key_gate.allow(deskport_key_ms, deskport_last_rtt)) requested_idr_frame = true;
        }
        // Follow a recovery key frame with the short full-rate refinement: a
        // still picture otherwise sends nothing for up to 5 s (data saver), so
        // a lost key frame could not be noticed and requested again sooner.
        if (requested_idr_frame) deskport_last_change_ms = deskport_key_ms;
      }
      if (smart && deskport_key_gate.coalesced != deskport_coalesced_logged &&
          deskport_key_ms - deskport_coalesced_log_ms >= 5000) {
        BOOST_LOG(info) << "DeskPort coalesced key frame requests: " << deskport_key_gate.coalesced;
        deskport_coalesced_logged = deskport_key_gate.coalesced;
        deskport_coalesced_log_ms = deskport_key_ms;
      }''')
patch('src/video.cpp', '''    std::int64_t deskport_last_change_ms = -1;''', '''    std::int64_t deskport_last_change_ms = -1;
    // Whether this encoder changes rate live; reported to the viewer's ceiling
    // requests. Starts at the launch ceiling, which never exceeds the negotiated rate.
    int deskport_permille = deskport::liveCeilingPermille.load();
    if (smart) {
      const int start = deskport::applyPermille(deskport_ceiling_kbps, deskport_permille);
      const bool saver = deskport::liveSaver.load() == 1;
      deskport_live = session->deskport_set_bitrate(start, false, saver);
      deskport::liveEncoder = deskport_live ? 1 : 0;
      if (deskport_live) {
        deskport_applied_rate.applied(start, false, saver);
        deskport::appliedSaver = saver ? 1 : 0;
        mail->event<int>("deskport_rate_kbps")->raise(start);
        BOOST_LOG(info) << "DeskPort live rate: target_kbps=" << start << ", rtt_ms=0"
                        << ", quality_floor=off, saver=" << (saver ? "on" : "off")
                        << ", min_qp=" << deskport::encoderMinQp(saver, false)
                        << ", max_qp=" << deskport::encoderMaxQp(saver, false);
        deskport::appliedCeilingPermille = deskport_permille;
      }
    }
    deskport::KeyframeGate deskport_key_gate;
    std::uint64_t deskport_coalesced_logged = 0;
    std::int64_t deskport_coalesced_log_ms = 0;''')

# Live ceiling: the launch request may carry one, and the stream records the
# bitrate the viewer requested so later ceilings scale the encoder target.
patch('src/rtsp.h', '''    bool deskport_saver = false;''', '''    bool deskport_saver = false;
    int deskport_rate_cap = 0;''')
patch('src/nvhttp.cpp', '''    launch_session->deskport_saver = get_arg(args, "deskportSaver", "0") == "1";''', '''    launch_session->deskport_saver = get_arg(args, "deskportSaver", "0") == "1";
    launch_session->deskport_rate_cap = std::max(0, std::atoi(get_arg(args, "deskportRateCap", "0").c_str()));''')
patch('src/rtsp.cpp', '''#include "rtsp.h"''', '''#include "rtsp.h"
#include "deskport/common/ratecontrol.h"''')
patch('src/rtsp.cpp', '''      BOOST_LOG(debug) << "Client configured bitrate is "sv << configuredBitrateKbps << " Kbps"sv;''', '''      BOOST_LOG(debug) << "Client configured bitrate is "sv << configuredBitrateKbps << " Kbps"sv;
      deskport::liveEncoder = 0;
      deskport::appliedCeilingPermille = -1;
      deskport::appliedSaver = -1;
      deskport::requestedKbps = (int) configuredBitrateKbps;
      deskport::liveCeilingPermille = deskport::ceilingPermille(session.deskport_rate_cap, configuredBitrateKbps);''')

patch('src/nvenc/nvenc_base.cpp', '#include "nvenc_base.h"', '#include "nvenc_base.h"\n#include <algorithm>')

# Bound the initial native encoder too, including drivers without live support.
# FFmpeg already applies this host limit during context creation.
patch('src/video.cpp', '    if (!encode_device->init_encoder(client_config, encode_device->colorspace)) {', '''    auto deskport_encoder_config = client_config;
    if (config::video.max_bitrate > 0)
      deskport_encoder_config.bitrate = std::min(deskport_encoder_config.bitrate, config::video.max_bitrate);
    if (deskport_encoder_config.bitrate <= 0 || !encode_device->init_encoder(deskport_encoder_config, encode_device->colorspace)) {''')

# Native Windows NVENC uses a separate interface (the newer pin compiles this
# implementation once per pinned SDK). No FFmpeg private layout is involved.
interface = root / 'src/nvenc/nvenc_encoder.h'
if interface.exists():
    patch('src/nvenc/nvenc_encoder.h', '    virtual void destroy_encoder() = 0;', '''    virtual void destroy_encoder() = 0;
    // Encode-thread only, between completed frames.
    virtual bool deskport_set_bitrate(int kbps) = 0;''')
    declaration = '    bool deskport_set_bitrate(int kbps) override;'
else:
    declaration = '    bool deskport_set_bitrate(int kbps);'
patch('src/nvenc/nvenc_base.h', '    void destroy_encoder()' + (' override;' if interface.exists() else ';'),
      '    void destroy_encoder()' + (' override;' if interface.exists() else ';') + '\n' + declaration)
patch('src/nvenc/nvenc_base.h', '    void *encoder = nullptr;', '''    void *encoder = nullptr;
    NV_ENC_INITIALIZE_PARAMS deskport_init {};
    NV_ENC_CONFIG deskport_config {}, deskport_initial_config {};
    bool deskport_dynamic_rate = false;''')
# Both pins reach this only after initialization and resource registration.
patch('src/nvenc/nvenc_base.cpp', '''    encoder_state = {};
    fail_guard.disable();''', '''    encoder_state = {};
    deskport_init = init_params;
    deskport_config = deskport_initial_config = enc_config;
    deskport_init.encodeConfig = nullptr; // never retain the stack pointer
    NV_ENC_CAPS_PARAM deskport_caps {NV_ENC_CAPS_PARAM_VER};
    deskport_caps.capsToQuery = NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE;
    int deskport_supported = 0;
    deskport_dynamic_rate = nvenc->nvEncReconfigureEncoder &&
        nvenc->nvEncGetEncodeCaps(encoder, init_params.encodeGUID, &deskport_caps, &deskport_supported) == NV_ENC_SUCCESS &&
        deskport_supported && enc_config.rcParams.averageBitRate > 0 &&
        enc_config.rcParams.rateControlMode != NV_ENC_PARAMS_RC_CONSTQP;
    fail_guard.disable();''')
patch('src/nvenc/nvenc_base.cpp', '  void nvenc_base::destroy_encoder() {', '''  // Called on the encoder owner thread after encode_frame has completed.
  bool nvenc_base::deskport_set_bitrate(int kbps) {
    const auto ceiling = deskport_initial_config.rcParams.averageBitRate;
    if (!encoder || !deskport_dynamic_rate || kbps <= 0 || kbps > 200000 ||
        std::uint64_t(kbps) * 1000 > ceiling) return false;
    const auto bps = std::uint32_t(kbps) * 1000;
    if (deskport_config.rcParams.averageBitRate == bps) return true;
    auto candidate = deskport_config;
    auto &rate = candidate.rcParams;
    rate.averageBitRate = bps;
    // Preserve the configured VBV duration, initial fill and peak/average ratio.
    // Always scale the original configuration, avoiding cumulative rounding.
    const auto scale = [bps, ceiling](std::uint32_t original) {
      return original ? std::uint32_t(std::max<std::uint64_t>(1, std::uint64_t(original) * bps / ceiling)) : 0U;
    };
    rate.maxBitRate = scale(deskport_initial_config.rcParams.maxBitRate);
    rate.vbvBufferSize = scale(deskport_initial_config.rcParams.vbvBufferSize);
    rate.vbvInitialDelay = scale(deskport_initial_config.rcParams.vbvInitialDelay);
    NV_ENC_RECONFIGURE_PARAMS params {NV_ENC_RECONFIGURE_PARAMS_VER};
    params.reInitEncodeParams = deskport_init;
    params.reInitEncodeParams.encodeConfig = &candidate;
    // Bitrate/VBV-only updates preserve RC/reference state. VIDEO_RATE.md
    // forbids forcing a key frame for a ceiling change. A driver that rejects
    // this update withdraws live support instead of resetting the stream.
    params.resetEncoder = 0;
    params.forceIDR = 0;
    if (nvenc_failed(nvenc->nvEncReconfigureEncoder(encoder, &params))) {
      BOOST_LOG(warning) << "DeskPort NVENC live rate rejected; reconnect required: " << last_nvenc_error_string;
      deskport_dynamic_rate = false;
      // Best effort restore; never report live even when restoration succeeds.
      params.reInitEncodeParams.encodeConfig = &deskport_config;
      if (nvenc_failed(nvenc->nvEncReconfigureEncoder(encoder, &params)))
        BOOST_LOG(error) << "DeskPort NVENC rate rollback failed: " << last_nvenc_error_string;
      return false;
    }
    deskport_config = candidate;
    return true;
  }

  void nvenc_base::destroy_encoder() {
    deskport_dynamic_rate = false;''')
# The device member is private: implement the hook in the owning session.
patch('src/video.cpp', '''    nvenc::nvenc_encoded_frame encode_frame(uint64_t frame_index) {''', '''    bool deskport_set_bitrate(int kbps, bool constrained, bool saver) override {
      return device && device->nvenc && device->nvenc->deskport_set_bitrate(kbps);
    }

    nvenc::nvenc_encoded_frame encode_frame(uint64_t frame_index) {''')
