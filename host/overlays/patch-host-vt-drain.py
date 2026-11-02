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
#endif''')
patch('src/video.cpp', '  class avcodec_encode_session_t: public encode_session_t {', '''  class avcodec_encode_session_t: public encode_session_t {
  public:
    bool deskport_complete_vt_frames = false;''')
patch('src/video.cpp', '''    // send the frame to the encoder
    auto ret = avcodec_send_frame(ctx.get(), frame);''', '''#ifdef __APPLE__
    deskport::vt::ScopedFrameCompletion deskport_completion(session.deskport_complete_vt_frames);
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
