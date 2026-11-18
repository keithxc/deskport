#!/usr/bin/env python3
"""Exercise the local encoded-video source without a host, window, or decoder."""

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
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=False)
(work / 'video-source.pro').write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = video-source-test
QMAKE_CXXFLAGS += -UNDEBUG
SOURCES += "{root}/tests/video-source.cpp"
INCLUDEPATH += "{root}/app" "{root}/moonlight-common-c/moonlight-common-c/src"
''')
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'video-source.pro'], cwd=work, check=True, stdout=subprocess.DEVNULL)
subprocess.run(['make', '-s', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'video-source-test')], check=True, timeout=15)
