#!/usr/bin/env python3
"""Check production gamepad release with synthetic state and recording C stubs."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-input-release-') as temporary:
    work = Path(temporary)
    sdl = root / 'libs/mac/Frameworks/SDL2.framework/Versions/A/Headers'
    (work / 'test.pro').write_text(f'''QT += core gui network qml testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = client-input-release
INCLUDEPATH += "{root}/app" "{root}/moonlight-common-c/moonlight-common-c/src"
SOURCES += "{root}/tests/client-input-release.cpp" "{root}/app/streaming/input/inputrelease.cpp"
macx {{
    INCLUDEPATH += "{sdl}"
    QMAKE_CXXFLAGS += -F"{root}/libs/mac/Frameworks"
}}
linux {{
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2
}}
''')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'],cwd=work,check=True)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work/'client-input-release')],cwd=work,check=True,timeout=30)
