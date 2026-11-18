#!/usr/bin/env python3
"""Pure streaming-policy tests. No capture, personal hosts or input injection."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os, pathlib, subprocess, tempfile, sys
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-smart-") as tmp:
    binary = pathlib.Path(tmp) / "test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root), str(root / "tests/smart-stream.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

if sys.platform == "darwin":
    with tempfile.TemporaryDirectory(prefix="deskport-pixels-") as tmp:
        binary = pathlib.Path(tmp) / "test"
        subprocess.run(["xcrun", "clang++", "-std=c++17", "-O2", "-I", str(root), str(root / "tests/smart-pixels.cpp"), "-framework", "CoreVideo", "-framework", "CoreFoundation", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
