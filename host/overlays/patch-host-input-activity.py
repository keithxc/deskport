#!/usr/bin/env python3
"""Add session-local input activity to the shared smart encoder overlay."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(name, old, new):
    p = root / name
    s = p.read_text()
    if new in s: return
    if s.count(old) != 1: raise SystemExit(f'Input activity anchor mismatch: {name}: {old[:60]}')
    p.write_text(s.replace(old, new))
patch('src/stream.cpp', '#include "deskport/common/smartstream.h"', '#include "deskport/common/smartstream.h"\n#include "deskport/common/inputactivity.h"')
patch('src/stream.cpp', '  struct session_t {', '  struct session_t {\n    deskport::InputActivity deskport_activity;')
p = root / 'src/stream.cpp'
s = p.read_text()
old = 'if (session->config.deskport_input) input::passthrough(session->input, std::move(plaintext));'
new = '''if (session->config.deskport_input) {
          const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
          const auto boost = session->deskport_activity.observe(plaintext.data(), plaintext.size(), now);
          if (boost.untilMs > now)
            session->mail->event<deskport::ActivityBoost>("deskport_activity")->raise(boost);
          input::passthrough(session->input, std::move(plaintext));
        }'''
if new not in s:
    if s.count(old) != 2: raise SystemExit('Expected both authenticated input paths')
    p.write_text(s.replace(old, new))
patch('src/video.cpp', '#include "deskport/common/smartstream.h"', '#include "deskport/common/smartstream.h"\n#include "deskport/common/inputactivity.h"')
patch('src/video.cpp', '    int last_target = config.framerate;', '''    int last_target = config.framerate;
    auto activity = mail->event<deskport::ActivityBoost>("deskport_activity");
    deskport::ActivityBoost boost;
    int last_activity_fps = 1;''')
patch('src/video.cpp', '    congestion->deskport_watch(deskport_wake);', '''    congestion->deskport_watch(deskport_wake);
    activity->deskport_watch(deskport_wake);''')
patch('src/video.cpp', '      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);', '''      if (smart && congestion->pop(0ms)) policy.loss(now_us / 1000);
      if (smart) {
        if (auto event = activity->pop(0ms)) boost = *event;
        const auto absolute_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const int fps = deskport::activityFrameRate(boost, absolute_ms, policy.frameRate(now_us / 1000));
        max_frametime = std::chrono::duration<double, std::milli>(1000.0 / fps);
        if (fps != last_activity_fps) {
          BOOST_LOG(info) << "DeskPort input cadence: " << fps << " fps; congestion ceiling: "
                          << policy.frameRate(now_us / 1000);
          last_activity_fps = fps;
        }
      }''')
