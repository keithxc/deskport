#!/usr/bin/env python3
"""Compile the production canvas focus/IME routing with inert media adapters."""

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
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
sources = ['tests/canvas-focus.cpp', 'app/gui/sessioncanvas.cpp',
           'app/backend/shortcutrecorderinhibitor.cpp', 'app/streaming/canvasbridge.cpp']
extra = ''
if sys.platform.startswith('linux'):
    sources += ['app/backend/shortcutrecorderinhibitor_wayland.cpp',
                'app/backend/keyboard-shortcuts-inhibit-v1.c']
    extra = 'QT += gui-private\nPKGCONFIG += wayland-client sdl2\n'
else:
    sources += ['app/backend/shortcutrecorderinhibitor_mac.mm']
    sdl = root / 'libs/mac/Frameworks'
    extra = f'QMAKE_CXXFLAGS += -F"{sdl}"\nINCLUDEPATH += "{sdl}/SDL2.framework/Headers"\nLIBS += -F"{sdl}" -framework SDL2 -framework Cocoa -framework ApplicationServices\nQMAKE_RPATHDIR += "{sdl}"\n'
(work / 'test.pro').write_text('\n'.join([
    'QT += core gui gui-private quick testlib', 'CONFIG += console c++17 testcase link_pkgconfig',
    'CONFIG -= app_bundle', 'QMAKE_CXXFLAGS += -UNDEBUG', 'TARGET = canvas-focus-test',
    f'INCLUDEPATH += "{root}/app"',
    'SOURCES += ' + ' '.join(f'"{root / s}"' for s in sources),
    'HEADERS += ' + ' '.join(f'"{root / s}"' for s in ['app/gui/sessioncanvas.h', 'app/gui/sessionpresentation.h']),
    extra]))
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True, stdout=subprocess.DEVNULL)
subprocess.run(['make', '-s', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'canvas-focus-test')], check=True, timeout=30,
               env=dict(os.environ, QT_QPA_PLATFORM='offscreen', QSG_RHI_BACKEND='software', SDL_VIDEODRIVER='dummy'))
