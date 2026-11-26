#!/usr/bin/env python3
"""Verify PlVkRenderer color-range mapping against libplacebo and libavutil."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=False)

def pkg_config(flag, *packages):
    out = subprocess.check_output(['pkg-config', flag, *packages], text=True)
    return shlex.split(out)

cflags = pkg_config('--cflags', 'libplacebo', 'libavutil', 'libavcodec', 'libavformat')
libs = pkg_config('--libs', 'libplacebo', 'libavutil', 'libavcodec')
includes = [f'-I{root}', f'-I{root}/moonlight-common-c/moonlight-common-c/src']
cc = os.environ.get('CC', 'cc')
cxx = os.environ.get('CXX', 'c++')

plvk_obj = work / 'plvk_c.o'
binary = work / 'plvk-color-test'

subprocess.run(
    [cc, '-c', '-O2', '-Wall', '-Wextra', *cflags, *includes,
     str(root / 'app/streaming/video/ffmpeg-renderers/plvk_c.c'), '-o', str(plvk_obj)],
    check=True,
)
subprocess.run(
    [cxx, '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
     *cflags, *includes, str(root / 'tests/plvk-color.cpp'), str(plvk_obj),
     *libs, '-lm', '-o', str(binary)],
    check=True,
)
subprocess.run([str(binary)], check=True, timeout=15)
