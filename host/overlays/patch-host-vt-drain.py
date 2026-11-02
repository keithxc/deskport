#!/usr/bin/env python3
"""Complete smart-stream VideoToolbox callbacks before FFmpeg checks its queue.

The host links its pinned FFmpeg statically. vtlatency.h forwards the public
native encode API and completes the submitted PTS only inside the scoped host
encode call. No polling, second submission, FFmpeg flush or private codec ABI.
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
        raise SystemExit(f'VideoToolbox latency anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

patch('src/video.cpp', '#include "video.h"', '''#include "video.h"
#ifdef __APPLE__
  #include "deskport/macos/vtlatency.h"
  #include "deskport/macos/vtclock.h"
#endif''')
patch('src/video.cpp', '  class avcodec_encode_session_t: public encode_session_t {', '''  class avcodec_encode_session_t: public encode_session_t {
  public:
    bool deskport_complete_vt_frames = false;
#ifdef __APPLE__
    deskport::vt::FrameClock deskport_clock;
#endif''')
patch('src/video.cpp', '''    // send the frame to the encoder
    auto ret = avcodec_send_frame(ctx.get(), frame);''', '''#ifdef __APPLE__
    deskport::vt::ScopedFrameCompletion deskport_completion(session.deskport_complete_vt_frames);
    const bool deskport_timed_vt = session.deskport_complete_vt_frames && ctx->codec &&
        (std::string_view(ctx->codec->name) == "h264_videotoolbox" ||
         std::string_view(ctx->codec->name) == "hevc_videotoolbox");
    bool deskport_current_produced = false;
    if (deskport_timed_vt) {
      const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      const int deskport_max_qp = session.deskport_quality.requestedMaximum(frame->flags & AV_FRAME_FLAG_KEY);
      if (session.deskport_quality.needsMaximum(deskport_max_qp)) {
        if (!deskport::vt::applyQualityCeiling(deskport::vt::session(ctx.get()), deskport_max_qp)) {
          BOOST_LOG(error) << "VideoToolbox picture quality failed"; return -1;
        }
        session.deskport_quality.appliedMaximum(deskport_max_qp);
        BOOST_LOG(info) << "DeskPort live quality: min_qp=22, max_qp=" << deskport_max_qp
                        << ", strict=" << (deskport_max_qp == 26 ? "on" : "off");
      }
      const auto pts = session.deskport_clock.submit(now_us, frame_nr, ctx->time_base.num, ctx->time_base.den);
      if (!pts) { BOOST_LOG(error) << "Invalid VideoToolbox frame clock"; return -1; }
      frame->pts = *pts;
    }
#endif
    // send the frame to the encoder
    auto ret = avcodec_send_frame(ctx.get(), frame);''')
for call in ('    auto ret = avcodec_send_frame(ctx.get(), frame);',
             '      ret = avcodec_receive_packet(ctx.get(), av_packet);'):
    indent = call[:len(call) - len(call.lstrip())]
    patch('src/video.cpp', call, call + '''
#ifdef __APPLE__
''' + indent + '''if (deskport_completion.status() != noErr) {
''' + indent + '''  BOOST_LOG(error) << "VideoToolbox frame completion failed: " << deskport_completion.status();
''' + indent + '''  return -1;
''' + indent + '''}
#endif''')
patch('src/video.cpp', '''      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;''', '''      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
#ifdef __APPLE__
        if (deskport_timed_vt) session.deskport_quality.completed(deskport_current_produced);
#endif
        return 0;''')
# Match recovery against native PTS, then restore the submitted frame ID before
# the existing capture timestamp, encoded-size and contiguous wire numbering.
patch('src/video.cpp', '''      if (session.deskport_keys.take(av_packet->pts) && !(av_packet->flags & AV_PKT_FLAG_KEY)) {
        BOOST_LOG(error) << "Encoder did not produce IDR frame when requested! Returned PTS="sv
                         << av_packet->pts << ", encoder="sv << ctx->codec->name;
      }''', '''      if (session.deskport_keys.take(av_packet->pts) && !(av_packet->flags & AV_PKT_FLAG_KEY)) {
        BOOST_LOG(error) << "Encoder did not produce IDR frame when requested! Returned PTS="sv
                         << av_packet->pts << ", encoder="sv << ctx->codec->name;
      }
#ifdef __APPLE__
      if (deskport_timed_vt) {
        const auto identity = session.deskport_clock.take(av_packet->pts);
        if (!identity) { BOOST_LOG(error) << "Unmapped VideoToolbox output timestamp"; return -1; }
        av_packet->pts = *identity;
        if (*identity == frame_nr) deskport_current_produced = true;
      }
#endif''')
# Only actual popped capture images arm the sharp first attempt. A conversion
# failure terminates this session before submission can consume the mark. Keep
# conversion anchors intact for the later data-saver admission/pending overlay.
# Recovery IDR requests do not pass through this captured-image branch.
patch('src/video.cpp', '''
          frame_timestamp = img->frame_timestamp;''', '''
          if (auto *avcodec_session = dynamic_cast<avcodec_encode_session_t *>(session.get())) {
            avcodec_session->deskport_quality.captured();
          }
          frame_timestamp = img->frame_timestamp;''')
# The two pinned revisions spell this cast differently; match only the unique
# avcodec factory, leaving its format/injection expression untouched.
p = root / 'src/video.cpp'
source = p.read_text()
assignment = '    session->deskport_complete_vt_frames = config.deskport_smart || config.deskport_saver;'
if assignment not in source:
    start = source.index('    auto session = std::make_unique<avcodec_encode_session_t>(')
    end = source.index('\n    );', start) + len('\n    );')
    source = source[:end] + '\n' + assignment + source[end:]
    p.write_text(source)
