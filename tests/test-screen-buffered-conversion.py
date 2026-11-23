#!/usr/bin/env python3
"""Compile the actual buffered SCK path without starting a capture session."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage
if __name__ == '__main__':
    build_storage.bootstrap()

import os
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sck-buffered-') as temporary:
    work = Path(temporary)
    source = work / 'source'
    names = ('src/platform/macos/av_video.h', 'src/platform/macos/av_video.m')
    with tarfile.open(root / 'host/vendor/sunshine.tar.gz') as archive:
        archive.extractall(source, members=[archive.getmember(name) for name in names], filter='data')
    legacy = work / 'legacy.o'
    subprocess.run(['xcrun', 'clang', '-I', str(source), '-c',
                    str(source / names[1]), '-o', str(legacy)], check=True)
    binary = work / 'buffered-conversion'
    command = ['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-I', str(root), '-I', str(source),
               str(root / 'tests/screen-buffered-conversion.m'),
               str(root / 'host/macos/color-conversion.m'), str(legacy), '-o', str(binary)]
    if os.environ.get('DESKPORT_SCK_SANITIZE'):
        command += ['-fsanitize=' + os.environ['DESKPORT_SCK_SANITIZE'], '-g', '-O1', '-fno-omit-frame-pointer']
    for framework in ('ScreenCaptureKit', 'AVFoundation', 'CoreMedia', 'CoreVideo',
                      'Metal', 'VideoToolbox', 'AppKit', 'CoreGraphics', 'Foundation', 'QuartzCore'):
        command += ['-framework', framework]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
