#!/usr/bin/env python3
"""Exercise real BrowserHost policy with a fake HostManager, never a deployed host."""

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
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=_build_storage.Storage().paths["tests"] / "browser-client/gateway")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix="host-test-", dir=args.output))
for name in ("tmp", "cache", "config", "data"):
    (work / name).mkdir()
project = work / "test.pro"
resources = work / "resources.qrc"
resources.write_text(f'<RCC><qresource prefix="/res"><file alias="deskport-tray-black.svg">{root}/app/res/deskport-tray-black.svg</file></qresource></RCC>')
project.write_text(f'''QT += core gui widgets network testlib qml quick quickcontrols2
linux: QT += dbus
CONFIG += console c++17 testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = browser-host-tests
SOURCES += "{root}/tests/browser-host.cpp" "{root}/app/backend/browserhost.cpp" "{root}/app/backend/browsergateway.cpp" "{root}/app/backend/hostmanager.cpp" "{root}/app/backend/hostmanager-startup.cpp" "{root}/app/backend/hostmanager-trust.cpp" "{root}/app/backend/hostmanager-display.cpp" "{root}/app/backend/diagnostics.cpp" "{root}/app/backend/nvaddress.cpp" "{root}/third_party/qrcodegen/qrcodegen.cpp"
HEADERS += "{root}/app/backend/browserhost.h" "{root}/app/backend/browsergateway.h" "{root}/app/backend/hostmanager.h" "{root}/app/backend/diagnostics.h"
INCLUDEPATH += "{root}/app/backend" "{root}/app"
RESOURCES += "{resources}"
macx {{
    OBJECTIVE_SOURCES += "{root}/app/backend/macpermissions.mm" "{root}/app/backend/macdock.mm" "{root}/app/backend/macclipboard.mm" "{root}/app/backend/macunattended.mm"
    LIBS += -framework CoreGraphics -framework AVFoundation -framework ApplicationServices -framework AppKit -framework ServiceManagement
}}
''')
environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", QML_DISABLE_DISK_CACHE="1",
    XDG_CONFIG_HOME=str(work / "config"), XDG_CACHE_HOME=str(work / "cache"), XDG_DATA_HOME=str(work / "data"),
    TMPDIR=str(work / "tmp"))
environment.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
prefixes = environment.get("QT_ADDITIONAL_PACKAGES_PREFIX_PATH", "").split(os.pathsep)
plugins = [str(Path(prefix) / "lib/qt-6/plugins") for prefix in prefixes if prefix and (Path(prefix) / "lib/qt-6/plugins").is_dir()]
if plugins:
    environment["QT_PLUGIN_PATH"] = os.pathsep.join(plugins + ([environment["QT_PLUGIN_PATH"]] if environment.get("QT_PLUGIN_PATH") else []))
with (work / "build.log").open("w") as log:
    subprocess.run([environment.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=work, env=environment, check=True, stdout=log, stderr=log)
    build = subprocess.run(["make", "-j2"], cwd=work, env=environment, stdout=log, stderr=log)
if build.returncode:
    print((work / "build.log").read_text())
    raise SystemExit(build.returncode)
result = subprocess.run([str(work / "browser-host-tests")], cwd=work, env=environment, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(work / "test.log").write_text(result.stdout)
print(result.stdout)
print(f"Browser host policy test evidence: {work}")
raise SystemExit(result.returncode)
