#!/usr/bin/env python3
"""Exercise macOS window-drag event routing without global input or a host."""

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
p.add_argument('--mactitlebar-source', type=Path,
               help='Use a saved production source file for before/after regression comparison')
p.add_argument('test_args', nargs=argparse.REMAINDER)
a = p.parse_args()
if sys.platform != 'darwin':
    p.error('this test requires macOS')
root = Path(__file__).resolve().parents[1]
source = (a.mactitlebar_source or root / 'app/backend/mactitlebar.mm').resolve()
if not source.is_file():
    p.error('the MacTitleBar source file must exist')
work = a.output.resolve()
work.mkdir(parents=True, exist_ok=False)
(work / 'test.pro').write_text('\n'.join([
    'QT += core gui quick testlib', 'CONFIG += console c++17 testcase',
    'CONFIG -= app_bundle', 'TARGET = macos-window-drag-test',
    'QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0',
    f'INCLUDEPATH += "{root}/app" "{root}/app/backend"',
    f'OBJECTIVE_SOURCES += "{root}/tests/macos-window-drag.mm" "{source}"',
    f'HEADERS += "{root}/app/backend/mactitlebar.h"',
    'LIBS += -framework Cocoa',
]))
subprocess.run([os.environ.get('QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True,
               stdout=subprocess.DEVNULL)
subprocess.run(['make', '-s', '-j2'], cwd=work, check=True)
test_args = a.test_args[1:] if a.test_args[:1] == ['--'] else a.test_args
subprocess.run([str(work / 'macos-window-drag-test'), *test_args], check=True, timeout=30,
               env=dict(os.environ, QT_QPA_PLATFORM='cocoa', QT_QUICK_BACKEND='software'))
