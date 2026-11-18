#!/usr/bin/env python3
"""Exercise production format negotiation with deterministic decoder probes."""
import os
from pathlib import Path
import subprocess as sp
import tempfile
root = Path(__file__).resolve().parents[1]
work = Path(sp.check_output(['bash', root/'scripts/build-paths.sh', 'checkout'], text=True).strip())/'tests/video-formats'
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=work) as temporary:
    path = Path(temporary)
    (path/'test.pro').write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
INCLUDEPATH += "{root}" "{root}/moonlight-common-c/moonlight-common-c/src"
SOURCES += "{root}/tests/video-formats.cpp"
TARGET = video-formats
''')
    sp.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=path, check=True, stdout=sp.DEVNULL)
    sp.run(['make', '-j2'], cwd=path, check=True, stdout=sp.DEVNULL)
    sp.run([path/'video-formats'], check=True)
