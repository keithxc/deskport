#!/usr/bin/env python3
"""Isolated SDL window lifetime test. No host connection or real input injection."""

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
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
if sys.platform == 'darwin': os.environ.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
with tempfile.TemporaryDirectory(prefix='deskport-transition-') as temporary:
    work = Path(temporary)
    sdl = root / 'libs/mac/Frameworks'
    project = f'''QT += gui
CONFIG += console c++17
CONFIG -= app_bundle
QMAKE_CXXFLAGS += -UNDEBUG
SOURCES += "{root}/tests/transition-window.cpp" "{root}/app/streaming/transitionwindow.cpp" "{root}/app/streaming/waylandloading.cpp" "{root}/app/streaming/windowactivation.cpp" "{root}/app/streaming/canvasbridge.cpp"
INCLUDEPATH += "{root}/app/streaming" "{root}/app"
TARGET = transition-test
'''
    if sys.platform == 'darwin':
        project += f'''QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0
QMAKE_CXXFLAGS += -F"{sdl}"
INCLUDEPATH += "{sdl}/SDL2.framework/Versions/A/Headers"
LIBS += -F"{sdl}" -framework SDL2
QMAKE_RPATHDIR += "{sdl}"
'''
    else:
        project += f'CONFIG += link_pkgconfig\nPKGCONFIG += sdl2 wayland-client\nDEFINES += HAS_WAYLAND\nSOURCES += "{root}/app/streaming/xdg-activation-v1.c"\n'
    (work / 'test.pro').write_text(project)
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True)
    subprocess.run(['make', '-j' + str(max(1, min(4, int(os.environ.get('JOBS', '2')))))], cwd=work, check=True, stdout=subprocess.DEVNULL)
    env = dict(os.environ)
    if '--native' not in sys.argv:
        env.update(QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy')
    subprocess.run([str(work / 'transition-test')], env=env, check=True, timeout=30)
