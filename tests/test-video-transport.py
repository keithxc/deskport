#!/usr/bin/env python3
"""Exercise authenticated local video IPC after worker environment cleanup."""

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
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
(work / 'video-transport.pro').write_text(f'''QT = core gui network qml
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
TARGET = video-transport-test
QMAKE_CXXFLAGS += -UNDEBUG
macx {{
 QMAKE_CXXFLAGS += -F"{root}/libs/mac/Frameworks"
 LIBS += -F"{root}/libs/mac/Frameworks" -framework SDL2
 INCLUDEPATH += "{root}/libs/mac/Frameworks/SDL2.framework/Headers"
 QMAKE_RPATHDIR += "{root}/libs/mac/Frameworks"
}} else {{
 PKGCONFIG += sdl2
}}
SOURCES += "{root}/tests/video-transport.cpp" "{root}/app/streaming/canvasbridge.cpp" "{root}/app/streaming/video/remotedecoder.cpp"
INCLUDEPATH += "{root}/app" "{root}/moonlight-common-c/moonlight-common-c/src"
''')
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'video-transport.pro'], cwd=work, check=True, stdout=subprocess.DEVNULL)
subprocess.run(['make', '-s', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'video-transport-test')], check=True, timeout=15)
