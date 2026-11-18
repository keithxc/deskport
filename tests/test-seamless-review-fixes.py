#!/usr/bin/env python3
"""Offline owned-supervisor API doubles and fixed-capacity frame decode.

Requires Qt Core/Gui, zlib and a C++17 compiler. --output is mandatory.
On Linux, also compiles the production supervisor helper for native fixtures.
"""

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
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--native-supervisor', action='store_true', help='Linux only: launch owned process fixtures')
args = parser.parse_args()
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=True)
(work / 'fixture.pro').write_text(f'''QT += core gui
CONFIG += console c++17
CONFIG -= app_bundle debug debug_and_release
QMAKE_CXXFLAGS += -UNDEBUG
TARGET = review-inflate
DESTDIR = .
SOURCES += "{root}/tests/fixtures/review-fixes/inflate.cpp"
INCLUDEPATH += "{root}/app/seamless"
LIBS += -lz
''')
subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'fixture.pro'], cwd=work, check=True)
subprocess.run(['make', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'review-inflate')], check=True)
# Compile the exact production pipe-control method bodies with inert QProcess
# fixtures; no Wayland compositor or application/host backend is linked.
source = (root / 'host/linux/seamless/seamlesshost.cpp').read_text()
methods = []
for signature in ('void SidecarHost::requestOwnedShutdown()', 'bool SidecarHost::ownedChildrenRunning() const'):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    methods.append(source[start:end])
(work / 'sidecar-control.cpp').write_text((root / 'tests/fixtures/review-fixes/sidecar-control.cpp.in').read_text().replace('@PRODUCTION_METHODS@', '\n'.join(methods)))
(work / 'control.pro').write_text('''QT += core
CONFIG += console c++17
CONFIG -= app_bundle debug debug_and_release
QMAKE_CXXFLAGS += -UNDEBUG
TARGET = review-sidecar-control
DESTDIR = .
SOURCES += sidecar-control.cpp
''')
subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'control.pro', '-o', 'Makefile.control'], cwd=work, check=True)
subprocess.run(['make', '-f', 'Makefile.control', '-j2'], cwd=work, check=True)
subprocess.run([str(work / 'review-sidecar-control')], cwd=work, check=True)
# Assertions are intentionally enabled, including release qmake builds.
cxx = shlex.split(os.environ.get('CXX', 'c++'))
subprocess.run([*cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-I' + str(root / 'host/linux/seamless'),
                str(root / 'tests/fixtures/review-fixes/supervisor.cpp'),
                '-o', str(work / 'review-supervisor')], check=True)
subprocess.run([str(work / 'review-supervisor')], check=True)

# Only the Linux prctl constant is stubbed; production adapter and POSIX headers
# are compiled unchanged, all process syscalls are replaced by the fixture.
(work / 'sys').mkdir(exist_ok=True)
(work / 'sys/prctl.h').write_text('#pragma once\n#define PR_SET_CHILD_SUBREAPER 36\n')
subprocess.run([*cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                '-I' + str(work), '-I' + str(root / 'host/linux/seamless'),
                str(root / 'tests/fixtures/review-fixes/supervisor-linux-api.cpp'),
                '-o', str(work / 'review-supervisor-linux-api')], check=True)
subprocess.run([str(work / 'review-supervisor-linux-api')], cwd=work, check=True)

if args.native_supervisor:
    if sys.platform != 'linux':
        parser.error('--native-supervisor requires Linux; not run on this host')
    subprocess.run([*cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'host/linux/seamless'),
                    str(root / 'tests/fixtures/review-fixes/supervisor-main.cpp'),
                    str(root / 'host/linux/seamless/ownedsupervisor.cpp'),
                    '-o', str(work / 'review-supervisor-native')], check=True)
    subprocess.run([sys.executable, str(root / 'tests/fixtures/review-fixes/supervisor-native.py'),
                    str(work / 'review-supervisor-native'), str(work)], cwd=work, check=True)
else:
    print('NOT RUN: Linux native supervisor fixtures (explicit --native-supervisor on Linux required)')
