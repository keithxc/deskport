#!/usr/bin/env python3
"""Viewer side of video resume after a background pause. Pure unit tests:
no host connection, network traffic or input injection."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os, pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
common = root / 'moonlight-common-c/moonlight-common-c'
with tempfile.TemporaryDirectory(prefix='deskport-video-resume-') as tmp:
    watchdog = pathlib.Path(tmp) / 'watchdog'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    str(root / 'tests/resume-watchdog.cpp'), '-o', str(watchdog)], check=True)
    subprocess.run([str(watchdog)], check=True)
    # The real RTP video queue with stubbed collaborators.
    queue = pathlib.Path(tmp) / 'rtp-frame-window'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-w', '-I', str(common / 'src'),
                    '-I', str(common / 'reedsolomon'), '-I', str(common / 'enet/include'),
                    str(root / 'tests/rtp-frame-window.c'), str(common / 'src/RtpVideoQueue.c'),
                    str(common / 'reedsolomon/rs.c'), '-o', str(queue)], check=True)
    subprocess.run([str(queue)], check=True)
