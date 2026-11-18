#!/usr/bin/env python3
"""Isolated protocol/demand tests; never read the user's system clipboard."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--offscreen-only", action="store_true", help="Skip the separate native named-pasteboard pass")
args = parser.parse_args()

root = Path(__file__).resolve().parents[1]
build_root = Path(subprocess.check_output(["bash", str(root / "scripts/build-paths.sh"), "checkout"], text=True).strip())
(build_root / "tests").mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="deskport-demand-test-", dir=build_root / "tests") as tmp:
    work = Path(tmp)
    project = work / "demand.pro"
    sources = ["tests/clipboard-demand.cpp", "app/clipboard/agent.cpp", "app/clipboard/native.cpp"]
    extra = f'OBJECTIVE_SOURCES += "{root}/app/clipboard/macnative.mm"\nLIBS += -framework AppKit\n' if sys.platform == "darwin" else f'SOURCES += "{root}/app/clipboard/waylandnative.cpp" "{root}/app/clipboard/ext-data-control.c" "{root}/app/clipboard/wlr-data-control.c" "{root}/app/clipboard/portalnative.cpp"\nCONFIG += link_pkgconfig\nPKGCONFIG += wayland-client\nQT += dbus\n'
    project.write_text('QT += core gui testlib\nCONFIG += console c++17 testcase\nCONFIG -= app_bundle\nTARGET = demand\n' + f'INCLUDEPATH += "{root}/app"\n' + 'SOURCES += ' + ' '.join(f'"{root / p}"' for p in sources) + '\n' + extra)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=work, env=env, check=True)
    jobs = max(1, min(4, int(os.environ.get("JOBS", "2"))))
    subprocess.run(["make", f"-j{jobs}"], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / "demand")], cwd=work, env=env, check=True, timeout=120)

    if sys.platform == "darwin" and not args.offscreen_only:
        native_env = dict(env, QT_QPA_PLATFORM="cocoa")
        subprocess.run([str(work / "demand"), "nativeMacProviders", "nativeMacAgentRoundTrip"],
                       cwd=work, env=native_env, check=True, timeout=60)
