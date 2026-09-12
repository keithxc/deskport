#!/usr/bin/env python3
"""Build and exercise the actual SCK bridge with synthetic frames, no capture."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
source = root / "build-macos.noindex/sunshine-source"
with tempfile.TemporaryDirectory(prefix="deskport-sck-test-") as temp:
    legacy = pathlib.Path(temp) / "legacy.o"
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["xcrun", "clang", "-c", str(source / "src/platform/macos/av_video.m"), "-o", str(legacy)], check=True)
    subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Wall", "-Werror", "-I", str(root), "-I", str(source),
                    str(root / "tests/screen-capture.m"), str(legacy), "-o", str(binary),
                    "-framework", "ScreenCaptureKit", "-framework", "AVFoundation", "-framework", "CoreMedia",
                    "-framework", "CoreVideo", "-framework", "CoreGraphics", "-framework", "Foundation", "-framework", "QuartzCore"], check=True)
    subprocess.run([str(binary)], check=True)
