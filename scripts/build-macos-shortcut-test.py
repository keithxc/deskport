#!/usr/bin/env python3
"""Build the native macOS fixture; execution needs an interactive test session."""

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
work = _build_storage.output(a.output)
work.mkdir(parents=True, exist_ok=False)
(work / 'test.pro').write_text('\n'.join([
    'QT = core gui testlib', 'CONFIG += console c++17 testcase',
    'CONFIG -= app_bundle', 'TARGET = macos-shortcut-capture-test',
    'QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0',
    f'INCLUDEPATH += "{root}/app"',
    f'SOURCES += "{root}/app/backend/shortcutrecorderinhibitor.cpp"',
    f'OBJECTIVE_SOURCES += "{root}/tests/macos-shortcut-capture.mm" "{root}/app/backend/shortcutrecorderinhibitor_mac.mm"',
    'LIBS += -framework Cocoa -framework ApplicationServices -framework Carbon',
]))
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True)
subprocess.run(['make', '-j2'], cwd=work, check=True)
print(work / 'macos-shortcut-capture-test')
