#!/usr/bin/env python3
"""Exercise external-process native clipboard changes on a unique named board."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-native-clipboard-") as tmp:
    binary = Path(tmp) / "clipboard-test"
    subprocess.run(["xcrun", "clang++", "-std=c++17", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "app"), str(root / "tests/macclipboard.mm"),
                    str(root / "app/backend/macclipboard.mm"), "-framework", "AppKit",
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
