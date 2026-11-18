#!/usr/bin/env python3
"""Test viewer lifecycle with synthetic windows only; --native needs an owned GUI.

No host, stream, physical input, clipboard, installed settings or service is used.
Generated files use the checkout build root. Native OS notifications are separate
from the default dummy SDL/Qt fixture and from real media/input acceptance.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
native = '--native' in sys.argv
build = Path(subprocess.check_output(['bash',str(root/'scripts/build-paths.sh'),'checkout'],text=True).strip())
temporary_root = build/'tests/viewer-activity'
temporary_root.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='run-',dir=temporary_root) as temporary:
    work = Path(temporary)
    sdl = root/'libs/mac/Frameworks'
    project = f'''QT += core gui widgets testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = viewer-activity
SOURCES += "{root}/tests/viewer-activity.cpp" "{root}/app/streaming/windowvisibility.cpp"
INCLUDEPATH += "{root}/app/streaming"
'''
    if sys.platform == 'darwin':
        project += f'''QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0
OBJECTIVE_SOURCES += "{root}/app/streaming/windowvisibility_mac.mm" "{root}/tests/windowvisibility-mac.mm"
QMAKE_CXXFLAGS += -F"{sdl}"
INCLUDEPATH += "{sdl}/SDL2.framework/Versions/A/Headers"
LIBS += -F"{sdl}" -framework SDL2 -framework AppKit
QMAKE_RPATHDIR += "{sdl}"
'''
    else:
        project += 'CONFIG += link_pkgconfig\nPKGCONFIG += sdl2\n'
        if sys.platform == 'win32':
            # Qt owns main. The Windows SDL pkg-config file otherwise redirects
            # it to SDL_main and adds its own GUI entry-point wrapper.
            project += '''load(link_pkgconfig)
DEFINES -= main=SDL_main
QMAKE_CFLAGS -= -Dmain=SDL_main
QMAKE_CXXFLAGS -= -Dmain=SDL_main
LIBS -= -mwindows -lSDL2main
LIBS += -ldwmapi -luser32
'''
    (work/'test.pro').write_text(project)
    subprocess.run([os.environ.get('DESKPORT_QMAKE','qmake'),'test.pro'],cwd=work,check=True)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    env = dict(os.environ)
    env.update(XDG_CONFIG_HOME=str(work/'config'), XDG_DATA_HOME=str(work/'data'), XDG_CACHE_HOME=str(work/'cache'))
    if native: env['DESKPORT_NATIVE_VISIBILITY_TEST']='1'
    else: env.update(QT_QPA_PLATFORM='offscreen',SDL_VIDEODRIVER='dummy')
    subprocess.run([str(work/'viewer-activity')],cwd=work,env=env,check=True,timeout=60)
