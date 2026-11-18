#!/usr/bin/env python3
"""Exercise the same forced socket instrumentation as the transport build."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import sys
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-traffic-") as work:
    binary = str(Path(work) / "traffic")
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-pthread", "-Wall", "-Wextra",
                    *(["-D_GNU_SOURCE=1"] if sys.platform.startswith("linux") else []),
                    "-include", str(root / "moonlight-common-c/traffic.h"),
                    str(root / "moonlight-common-c/traffic.c"), str(root / "tests/traffic.c"),
                    "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=20)
