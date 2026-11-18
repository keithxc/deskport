#!/usr/bin/env python3
"""Check the production native-window frame adapter without contacting a host."""

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

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output-dir", type=Path, required=True)
parser.add_argument("--native-wayland", action="store_true",
                    help="Use an already-owned private Wayland compositor under the DeskPort build root")
args = parser.parse_args()
work = args.output_dir.resolve()
work.mkdir(parents=True, exist_ok=True)
project = work / "test.pro"
project.write_text(f'''QT += core gui quick testlib
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = window-frame-test
INCLUDEPATH += "{ROOT}/app/backend"
SOURCES += "{ROOT}/tests/window-frame.cpp"
''')
env = dict(os.environ, QT_QUICK_BACKEND="software", QSG_RENDER_LOOP="basic")
if args.native_wayland:
    build_root = Path(subprocess.check_output(["bash", str(ROOT / "scripts/build-paths.sh"), "root"], text=True).strip()).resolve()
    runtime = Path(env.get("XDG_RUNTIME_DIR", "/nonexistent")).resolve()
    if not runtime.is_relative_to(build_root / "t") or not env.get("WAYLAND_DISPLAY"):
        parser.error("--native-wayland requires an owned private compositor runtime beneath the build root's t/ directory")
    env["QT_QPA_PLATFORM"] = "wayland"
    env.pop("DISPLAY", None)
else:
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/nonexistent-deskport-private-bus"
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_ACTIVATION_TOKEN"):
        env.pop(key, None)
for key in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME"):
    path = work / key.lower()
    path.mkdir(exist_ok=True)
    env[key] = str(path)
env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
qmake = env.get("DESKPORT_QMAKE", "qmake")
subprocess.run([qmake, str(project)], cwd=work, env=env, check=True)
subprocess.run(["make", "-j" + str(max(1, min(4, int(env.get("JOBS", "2")))))], cwd=work, env=env,
               check=True, stdout=subprocess.DEVNULL)
subprocess.run([str(work / "window-frame-test")], cwd=work, env=env, check=True, timeout=30)
