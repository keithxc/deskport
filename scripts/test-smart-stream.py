#!/usr/bin/env python3
"""Pure streaming-policy tests. No capture, personal hosts or input injection."""
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
