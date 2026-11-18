#!/usr/bin/env python3
"""Build an explicitly isolated HTTPS-to-Sunshine fixture with the real web client."""

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

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=_build_storage.Storage().paths["work"] / "browser-client/live-gateway")
args = parser.parse_args()
output = _build_storage.output(args.output)
output.mkdir(parents=True, exist_ok=True)
project = output / "fixture.pro"
project.write_text(f'''QT = core network
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = browser-live-gateway
SOURCES += "{root}/tests/browser-live-gateway.cpp" "{root}/app/backend/browsergateway.cpp"
HEADERS += "{root}/app/backend/browsergateway.h"
INCLUDEPATH += "{root}/app/backend"
RESOURCES += "{root}/app/browser/browser.qrc"
''')
with (output / "build.log").open("w") as log:
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=output, check=True, stdout=log, stderr=log)
    build = subprocess.run(["make", "-j2"], cwd=output, stdout=log, stderr=log)
if build.returncode:
    print((output / "build.log").read_text())
    raise SystemExit(build.returncode)
print(output / "browser-live-gateway")
