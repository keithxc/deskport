#!/usr/bin/env python3
"""Exercise production display policy and recovery without touching real displays."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-topology-") as temporary:
    binary = Path(temporary) / "topology"
    subprocess.run(["xcrun", "clang", "-fobjc-arc", "-fblocks", "-fsanitize=address,undefined",
                    str(root / "tests/session-topology.m"), "-framework", "Foundation",
                    "-framework", "ApplicationServices", "-framework", "AppKit", "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(root / "tests/fixtures/macos-topologies.json")], check=True)

    subprocess.run([str(binary)], check=True, env=dict(os.environ, DESKPORT_DISPLAY_ISOLATED="1", DESKPORT_DISPLAY_STATE_DIR=temporary, DESKPORT_DISPLAY_SERIAL="2147483650"))
