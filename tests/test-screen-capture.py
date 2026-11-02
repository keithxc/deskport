#!/usr/bin/env python3
"""Build and exercise the actual SCK bridge with synthetic frames, no capture."""
import os, pathlib, subprocess, tarfile, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="deskport-sck-test-") as temp:
    source_override = os.environ.get("DESKPORT_HOST_SOURCE_DIR")
    if source_override:
        # Existing callers may supply an already patched host tree. Never modify it.
        source = pathlib.Path(source_override)
    else:
        # This synthetic test needs only AVVideo's declarations and implementation,
        # not a previously compiled host or its mutable build cache.
        source = pathlib.Path(temp) / "source"
        names = ("src/platform/macos/av_video.h", "src/platform/macos/av_video.m")
        with tarfile.open(root / "host/vendor/sunshine.tar.gz") as archive:
            members = [archive.getmember(name) for name in names]
            if any(not member.isfile() for member in members):
                raise RuntimeError("Expected regular vendored AVVideo source files")
            archive.extractall(source, members=members, filter="data")
        subprocess.run(["git", "apply", "--include=src/platform/macos/av_video.*",
                        str(root / "host/macos/patches/sunshine-capture-timeout.patch")],
                       cwd=source, check=True)
    legacy = pathlib.Path(temp) / "legacy.o"
    binary = pathlib.Path(temp) / "test"
    subprocess.run(["xcrun", "clang", "-I", str(source), "-c", str(source / "src/platform/macos/av_video.m"), "-o", str(legacy)], check=True)
    subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Wall", "-Werror", "-I", str(root), "-I", str(source),
                    str(root / "tests/screen-capture.m"), str(legacy), "-o", str(binary),
                    "-framework", "ScreenCaptureKit", "-framework", "AVFoundation", "-framework", "CoreMedia",
                    "-framework", "CoreVideo", "-framework", "CoreGraphics", "-framework", "Foundation", "-framework", "QuartzCore"], check=True)
    subprocess.run([str(binary)], check=True)
