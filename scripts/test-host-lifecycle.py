#!/usr/bin/env python3
"""Test host supervision with fake children and loopback-only port reservations."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if sys.platform != "darwin":
    raise SystemExit("This native host lifecycle harness currently requires macOS and Qt.")
root = Path(__file__).resolve().parents[1]
qmake = os.environ.get("DESKPORT_QMAKE", "qmake")
with tempfile.TemporaryDirectory(prefix="deskport-lifecycle-") as temporary:
    work = Path(temporary)
    macos = work / "Contents/MacOS"
    helpers = work / "Contents/Helpers"
    macos.mkdir(parents=True)
    (helpers / "Sunshine.app/Contents/MacOS").mkdir(parents=True)
    interpreter = shutil.which("python3")
    display = helpers / "deskport-display"
    display.write_text(f"#!{interpreter}\n" + '''import json, os, select, sys
if os.environ.get("DESKPORT_TEST_MODE") == "display-fail":
    sys.exit(4)
print(json.dumps({"displayId": 123}), flush=True)
if os.environ.get("DESKPORT_TEST_MODE") == "display-late-fail":
    select.select([sys.stdin], [], [], 1.5)
    sys.exit(4)
for line in sys.stdin:
    pass
''')
    host = helpers / "Sunshine.app/Contents/MacOS/Sunshine"
    host.write_text(f"#!{interpreter}\n" + '''import os, pathlib, signal, sys, time
state = pathlib.Path(sys.argv[1]).parent
mode = os.environ.get("DESKPORT_TEST_MODE")
if "--creds" in sys.argv:
    (state / "auth-started").touch()
    if mode == "auth-fail": sys.exit(3)
    time.sleep(10 if mode == "auth-slow" else 0.4)
    sys.exit(0)
if mode == "host-fail": sys.exit(7)
if mode == "stubborn": signal.signal(signal.SIGTERM, signal.SIG_IGN)
(state / "host-started").touch()
while True: time.sleep(1)
''')
    for executable in (display, host):
        executable.chmod(0o700)
    binding = "--binding" in sys.argv
    extra_sources = f'"{root}/app/backend/peermanager.cpp"' if binding else ""
    extra_headers = f'"{root}/app/backend/peermanager.h"' if binding else ""
    suite = "peer-binding" if binding else "host-lifecycle"
    project = work / "tests.pro"
    project.write_text(f'''QT += core gui widgets network testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = host-lifecycle-tests
DESTDIR = "{macos}"
SOURCES += "{root}/tests/{suite}.cpp" "{root}/app/backend/hostmanager.cpp" "{root}/app/backend/nvaddress.cpp" {extra_sources}
HEADERS += "{root}/app/backend/hostmanager.h" {extra_headers}
INCLUDEPATH += "{root}/app/backend"
LIBS += -framework CoreGraphics
''')
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    if binding:
        for name in ("A", "B", "C"):
            cert, key = work / f"{name}.pem", work / f"{name}.key"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                "-subj", "/CN=NVIDIA GameStream Client", "-set_serial", "0", "-keyout", str(key), "-out", str(cert)],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            environment[f"TEST_CERT_{name}"] = str(cert)
            environment[f"TEST_KEY_{name}"] = str(key)
    environment.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    subprocess.run([qmake, str(project)], cwd=work, env=environment, check=True)
    subprocess.run(["make", "-j4"], cwd=work, env=environment, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(macos / "host-lifecycle-tests")], cwd=work, env=environment, check=True)
