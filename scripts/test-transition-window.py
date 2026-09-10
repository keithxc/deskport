#!/usr/bin/env python3
"""Isolated SDL window lifetime test. No host connection or real input injection."""
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
SOURCES += "{root}/tests/transition-window.cpp" "{root}/app/streaming/transitionwindow.cpp" "{root}/app/streaming/waylandloading.cpp"
INCLUDEPATH += "{root}/app/streaming"
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
        project += 'CONFIG += link_pkgconfig\nPKGCONFIG += sdl2 wayland-client\nDEFINES += HAS_WAYLAND\n'
    (work / 'test.pro').write_text(project)
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True)
    subprocess.run(['make', '-j4'], cwd=work, check=True, stdout=subprocess.DEVNULL)
    env = dict(os.environ)
    if '--native' not in sys.argv:
        env.update(QT_QPA_PLATFORM='offscreen', SDL_VIDEODRIVER='dummy')
    subprocess.run([str(work / 'transition-test')], env=env, check=True, timeout=30)
