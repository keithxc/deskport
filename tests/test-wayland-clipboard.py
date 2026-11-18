#!/usr/bin/env python3
"""Synthetic portal tests on a private bus; no personal desktop/clipboard access."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--native-build", type=Path, help="Build a retained native fixture, without running it")
args = parser.parse_args()
if args.native_build:
    work = args.native_build.resolve()
    work.mkdir(parents=True, exist_ok=True)
    project = work / "native.pro"
    sources = ["tests/waylandclipboard-native.cpp", "app/clipboard/waylandnative.cpp", "app/clipboard/portalnative.cpp", "app/clipboard/agent.cpp", "app/clipboard/native.cpp", "app/clipboard/ext-data-control.c", "app/clipboard/wlr-data-control.c"]
    project.write_text('QT = core gui dbus testlib\nCONFIG += console c++17 testcase link_pkgconfig\nCONFIG -= app_bundle\nPKGCONFIG += wayland-client\nTARGET = native-clipboard-test\n' + f'INCLUDEPATH += "{root}/app"\nSOURCES += ' + ' '.join(f'"{root / source}"' for source in sources) + '\n')
    subprocess.run(["qmake", str(project)], cwd=work, check=True)
    subprocess.run(["make", "-j" + str(min(4, int(os.environ.get("JOBS", "2"))))], cwd=work, check=True, stdout=subprocess.DEVNULL)
    print(work / "native-clipboard-test")
    raise SystemExit(0)
build_root = Path(subprocess.check_output(["bash", str(root / "scripts/build-paths.sh"), "checkout"], text=True).strip())
(build_root / "tests").mkdir(parents=True, exist_ok=True)
socket_root = Path(subprocess.check_output(["bash", str(root / "scripts/build-paths.sh"), "root"], text=True).strip()) / "sockets"
socket_root.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="deskport-portal-test-", dir=build_root / "tests") as tmp, tempfile.TemporaryDirectory(prefix="cb-", dir=socket_root) as bus_tmp:
    work = Path(tmp)
    project = work / "portal.pro"
    project.write_text('QT += core gui dbus testlib\nCONFIG += console c++17 testcase\nCONFIG -= app_bundle\nTARGET = portal-test\n' +
                       f'INCLUDEPATH += "{root}/app"\nSOURCES += "{root}/tests/portalclipboard.cpp" "{root}/app/clipboard/portalnative.cpp"\n')
    config = work / "bus.conf"
    config.write_text('<busconfig><type>session</type><listen>unix:tmpdir=' + bus_tmp + '</listen><auth>EXTERNAL</auth><policy context="default"><allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/></policy></busconfig>')
    # No service directories: a missing/revoked fake portal must never activate
    # real desktop services through the private bus.
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    subprocess.run(["qmake", str(project)], cwd=work, env=env, check=True)
    subprocess.run(["make", "-j" + str(min(4, int(os.environ.get("JOBS", "2"))))], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["dbus-run-session", "--config-file", str(config), "--", "env", "DESKPORT_TEST_PRIVATE_BUS=1", str(work / "portal-test")], cwd=work, env=env, check=True, timeout=120)

# Retain the compiled provider fixture for an explicit, separately owned native run.
# This does not connect to Wayland or the desktop bus.
build_root = Path(subprocess.check_output(["bash", str(root / "scripts/build-paths.sh"), "checkout"], text=True).strip())
subprocess.run([sys.executable, str(Path(__file__).resolve()), "--native-build", str(build_root / "tests/wayland-native")], env=env, check=True)
