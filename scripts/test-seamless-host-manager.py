#!/usr/bin/env python3
"""Build and run the isolated Seamless sidecar supervisor regression."""

import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
build_root = Path(os.environ.get(
    "DESKPORT_BUILD_ROOT", str(Path.home() / "mygit/build/deskport")))
scratch_root = build_root / "tests"
scratch_root.mkdir(parents=True, exist_ok=True)
qmake = os.environ.get("DESKPORT_QMAKE", os.environ.get("QMAKE", "qmake"))

with tempfile.TemporaryDirectory(prefix="seamless-host-manager-", dir=scratch_root) as temporary:
    work = Path(temporary)
    project = work / "test.pro"
    project.write_text(f'''QT += core testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = seamless-host-manager-test
SOURCES += "{root}/tests/seamless-host-manager.cpp" \\
           "{root}/app/backend/seamlesshostmanager.cpp"
HEADERS += "{root}/app/backend/seamlesshostmanager.h"
INCLUDEPATH += "{root}/app/backend"
''')
    subprocess.run([qmake, str(project)], cwd=work, check=True)
    subprocess.run(["make", "-s"], cwd=work, check=True)
    binary = work / "seamless-host-manager-test"
    if not binary.exists():
        binary = work / "seamless-host-manager-test.app/Contents/MacOS/seamless-host-manager-test"
    environment = dict(os.environ, TMPDIR=str(work))
    subprocess.run([str(binary)], cwd=work, env=environment, check=True, timeout=90)

print("PASS: Seamless host supervisor validates bounded JSON and tears down its exact child")
