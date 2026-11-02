#!/usr/bin/env python3
"""Live viewer bitrate ceiling on the authenticated loopback API (both pins).

Runs after patch-host-video-pause.py and uses the same lease validation. The
encoder thread reads the ceiling from deskport/common/ratecontrol.h.
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
        raise SystemExit(f'Rate API anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

patch('src/confighttp.cpp', '#include "confighttp.h"', '#include "confighttp.h"\n#include "deskport/common/ratecontrol.h"')
patch('src/confighttp.cpp', '''        if (action == "video") {''', '''        if (action == "rate") {
          const auto ceiling = input.contains("ceilingKbps") && input["ceilingKbps"].is_number_integer()
            ? input["ceilingKbps"].get<long long>() : -1;
          // Optional data saver state; absent keeps the current one.
          const bool saver_valid = !input.contains("saver") || input["saver"].is_boolean();
          if (deskport_admission::lease.empty() || input.value("lease", "") != deskport_admission::lease ||
              ceiling < 500 || ceiling > 200000 || !saver_valid || rtsp_stream::session_count() > 1) {
            output["status"] = false; output["code"] = "unauthorized";
          } else {
            // Saver first: the encoder applies the ceiling with the saver's burst.
            if (input.contains("saver")) {
              deskport::liveSaver = input["saver"].get<bool>() ? 1 : 0;
              output["saver"] = input["saver"].get<bool>();
            }
            deskport::liveCeilingPermille = deskport::ceilingPermille(ceiling, deskport::requestedKbps.load());
            output["status"] = true;
            output["ceilingKbps"] = ceiling;
            output["live"] = deskport::liveEncoder.load() == 1;
          }
        } else if (action == "video") {''')
