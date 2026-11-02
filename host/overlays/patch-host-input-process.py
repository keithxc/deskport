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
