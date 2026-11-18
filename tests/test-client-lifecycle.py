#!/usr/bin/env python3
"""Inject normalized client power/lock events without sleeping the computer."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-client-lifecycle-') as temporary:
    work = Path(temporary)
    (work / 'test.pro').write_text(f'''QT += core gui testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = client-lifecycle
INCLUDEPATH += "{root}/app/backend"
SOURCES += "{root}/tests/client-lifecycle.cpp" "{root}/app/backend/sleepmonitor.cpp"
HEADERS += "{root}/app/backend/sleepmonitor.h"
linux {{
    QT += dbus
    SOURCES += "{root}/tests/client-lifecycle-logind.cpp"
}}
win32 {{
    SOURCES += "{root}/app/backend/sleepmonitor_win.cpp" "{root}/tests/client-lifecycle-native-win.cpp"
    LIBS += -lwtsapi32
}}
macx {{
    OBJECTIVE_SOURCES += "{root}/app/backend/sleepmonitor_mac.mm" "{root}/tests/client-lifecycle-native.mm"
    LIBS += -framework AppKit -framework CoreGraphics
}}
''')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, check=True)
    subprocess.run(['make', '-j4'], cwd=work, check=True, stdout=subprocess.DEVNULL)
    command = [str(work / 'client-lifecycle')]
    if sys.platform.startswith('linux'):
        command = ['dbus-run-session', '--', 'sh', '-ec',
                   'export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"; export DESKPORT_LIFECYCLE_PRIVATE_BUS=1; exec "$1"',
                   'lifecycle', *command]
    subprocess.run(command, cwd=work, check=True, timeout=40)
