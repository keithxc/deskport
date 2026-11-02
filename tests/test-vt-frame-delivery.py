#!/usr/bin/env python3
"""Verify native frame delivery and rate limits with the host FFmpeg prebuilt.

Pass --ffmpeg-root to the checksum-verified dependency cache. No capture, live
session or input injection is used. TMPDIR can place compiler output in a run.
"""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ffmpeg-root', type=Path, required=True)
args = parser.parse_args()
if sys.platform != 'darwin':
    raise SystemExit('VideoToolbox verification requires macOS')
root = Path(__file__).resolve().parents[1]
ffmpeg = args.ffmpeg_root.resolve()
with tempfile.TemporaryDirectory(prefix='deskport-vt-delivery-') as tmp:
    command = ['xcrun', 'clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-I', str(root), '-I', str(ffmpeg / 'include'),
               '-L', str(ffmpeg / 'lib'),
               '-lavcodec', '-lavutil', '-lSvtAv1Enc', '-lx264', '-lx265',
               '-lz', '-lbz2', '-liconv', '-pthread']
    for framework in ('VideoToolbox', 'CoreFoundation', 'CoreMedia', 'CoreVideo', 'CoreServices', 'Security'):
        command += ['-framework', framework]
    for name in ('vt-frame-delivery', 'vt-rate-limits'):
        binary = Path(tmp) / name
        subprocess.run(command + [str(root / 'tests' / f'{name}.cpp'), '-o', str(binary)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', '--options', 'runtime', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
