#!/usr/bin/env python3
"""Compile/read-only caret checks. No personal session or remote input."""
import os
from pathlib import Path
import subprocess
import tempfile
import shutil
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-caret-') as name:
    work=Path(name)
    (work/'test.pro').write_text(f'''QT = core dbus testlib
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = caret-test
SOURCES += {root}/tests/text-caret.cpp
''')
    subprocess.run([os.environ.get('DESKPORT_QMAKE','qmake'),'test.pro'],cwd=work,check=True)
    subprocess.run(['make','-j4'],cwd=work,check=True)
    daemon=os.environ.get('DESKPORT_DBUS_DAEMON') or shutil.which('dbus-daemon')
    if not daemon:
        raise SystemExit('dbus-daemon is required for the isolated AT-SPI fixture')
    config=work/'bus.conf'
    config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth><policy context="default"><allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/></policy></busconfig>')
    with subprocess.Popen([daemon,'--config-file='+str(config),'--nofork','--print-address=1'],stdout=subprocess.PIPE,text=True) as bus:
        try:
            address=bus.stdout.readline().strip()
            if not address: raise RuntimeError('isolated message bus failed to start')
            subprocess.run([str(work/'caret-test')],cwd=work,env=dict(os.environ,DESKPORT_TEST_DBUS_ADDRESS=address),check=True,timeout=20)
        finally:
            bus.terminate()
            bus.wait(timeout=5)
