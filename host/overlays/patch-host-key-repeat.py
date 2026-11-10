#!/usr/bin/env python3
"""Let the client's keyboard drive key repeat.

The host repeats a held key from its own timer, started when the press
arrives. A release delayed by the network for longer than the repeat delay
therefore typed the key many times. Clients that set the client-driven bit
forward their own repeats; for those keys the host re-emits on request and does
not start its timer. Other clients keep the previous behavior. See
host/common/keyrepeat.h.
"""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'src/input.cpp'
source = path.read_text()

def patch(old, new):
    global source
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Key repeat anchor mismatch: {old[:80]!r}')
    source = source.replace(old, new)

patch('#include "input.h"\n', '#include "input.h"\n#include "deskport/common/keyrepeat.h"\n')
patch('''    auto &pressed = key_press[make_kpid(keyCode, packet->flags)];
''', '''    const bool deskport_client_repeat = deskport::key_repeat::clientDriven(packet->flags);
    packet->flags = deskport::key_repeat::hostFlags(packet->flags);
    auto &pressed = key_press[make_kpid(keyCode, packet->flags)];
''')
patch('''        if (config::input.key_repeat_delay.count() > 0) {
''', '''        if (config::input.key_repeat_delay.count() > 0 && !deskport_client_repeat) {
''')
patch('''    } else if (!release) {
      // Already pressed down key
      return;
    }
''', '''    } else if (!release) {
      // Already pressed: only a client that drives repeat re-emits the key.
      if (deskport_client_repeat) {
        send_key_and_modifiers(keyCode, false, packet->flags, synthetic_modifiers);
      }
      return;
    }
''')
path.write_text(source)
