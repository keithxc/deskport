#!/usr/bin/env python3
"""Build and run the device group/order rules test; no settings or hosts are touched."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
qmake = os.environ.get("QMAKE", "qmake")
with tempfile.TemporaryDirectory(prefix="deskport-host-layout-") as work:
    work = Path(work)
    (work / "host-layout.pro").write_text(f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = host-layout-test
SOURCES += "{root}/tests/host-layout.cpp" "{root}/app/gui/hostlayout.cpp"
HEADERS += "{root}/app/gui/hostlayout.h"
INCLUDEPATH += "{root}/app/gui"
''')
    subprocess.run([qmake, "host-layout.pro"], cwd=work, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["make", "-s"], cwd=work, check=True)
    sys.exit(subprocess.run([str(work / "host-layout-test")]).returncode)
