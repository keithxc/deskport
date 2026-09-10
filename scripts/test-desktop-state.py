#!/usr/bin/env python3
"""Test desktop preferences and saved window state without hosts or real input."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-desktop-state-') as temporary:
    work = Path(temporary)
    (work / 'test.pro').write_text(f'''QT += core gui qml testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
SOURCES += "{root}/tests/desktop-state.cpp" "{root}/app/settings/streamingpreferences.cpp"
HEADERS += "{root}/app/settings/streamingpreferences.h"
INCLUDEPATH += "{root}/app"
TARGET = desktop-state
''')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'],cwd=work,check=True)
    subprocess.run(['make','-j4'],cwd=work,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([str(work / 'desktop-state')],cwd=work,check=True,timeout=30,
                   env=dict(os.environ,QT_QPA_PLATFORM='offscreen'))
