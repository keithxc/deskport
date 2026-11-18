#!/usr/bin/env python3
"""Cross-compile the native Windows keyboard fixture using retained Qt inputs.

Run the resulting executable only in an interactive Windows test session. It
injects into its own foreground windows, never connects to a remote host.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import configparser
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--qt-prefix', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
qt = a.qt_prefix.resolve()
host = Path(os.environ['QT_HOST_PATH'])
config = configparser.ConfigParser()
config.optionxform = str
config.read(qt / 'bin/target_qt.conf')
config['Paths'].update(Prefix=str(qt), HostPrefix=str(host), HostData=str(qt))
with (work / 'qt.conf').open('w') as f:
    config.write(f, space_around_delimiters=False)
(work / 'test.pro').write_text('\n'.join([
    'QT = core gui testlib', 'CONFIG += console c++17 testcase release',
    'CONFIG -= app_bundle', 'TARGET = windows-shortcut-capture-test',
    f'INCLUDEPATH += "{root}/app"',
    'SOURCES += ' + ' '.join(f'"{root / s}"' for s in [
        'tests/windows-shortcut-capture.cpp', 'app/backend/shortcutrecorderinhibitor.cpp',
        'app/backend/shortcutrecorderinhibitor_windows.cpp']),
    'LIBS += -luser32',
]))
subprocess.run([str(host / 'bin/qmake'), '-qtconf', str(work / 'qt.conf'),
                'test.pro', '-spec', 'win32-g++'], cwd=work, check=True)
subprocess.run(['make', '-j4'], cwd=work, check=True)
print(work / 'release/windows-shortcut-capture-test.exe')
