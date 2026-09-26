#!/usr/bin/env python3
"""Keep desktop handles owned until their calling thread detaches."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
target = Path(sys.argv[1])
header = target / 'src/deskport/windows/thread-desktop.h'
header.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(root / 'host/windows/thread-desktop.h', header)
path = target / 'src/platform/windows/misc.cpp'
source = path.read_text()
marker = '// DeskPort owned thread desktop lifetime'
if marker not in source:
    start = source.index('  HDESK syncThreadDesktop() {')
    end = source.index('\n  void print_status(', start)
    source = source[:start] + '''  HDESK syncThreadDesktop() {
    // DeskPort owned thread desktop lifetime
    thread_local deskport::thread_desktop desktop;
    const auto handle = desktop.sync();
    if (!handle) {
      BOOST_LOG(error) << "Failed to sync desktop to thread [0x"sv << util::hex(GetLastError()).to_string_view() << ']';
    }
    return handle;
  }
''' + source[end:]
    anchor = '#include "misc.h"'
    if source.count(anchor) != 1:
        raise SystemExit('Windows desktop include anchor changed')
    source = source.replace(anchor, anchor + '\n#include "src/deskport/windows/thread-desktop.h"')
    path.write_text(source)
