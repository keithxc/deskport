#!/usr/bin/env python3
"""Render production desktop pages with inert backends and isolated preferences.

Requires the locked devShell. Output must be an explicit build-workspace path.
--build-only also supports cross qmake via DESKPORT_TYPOGRAPHY_QMAKE_ARGS.
The resulting work/ directory is self-contained for a native Windows run.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--platform", default="offscreen")
parser.add_argument("--renderer", choices=("software", "native"), default="software")
parser.add_argument("--widths", default="640,1120")
parser.add_argument("--height", type=int, default=760)
parser.add_argument("--build-only", action="store_true")
parser.add_argument("--focus-only", action="store_true", help="Run actual modal QML keyboard controls only")
args = parser.parse_args()
try:
    widths = [int(width) for width in args.widths.split(",")]
except ValueError:
    parser.error("widths must be comma-separated integers")
if not widths or not all(320 <= width <= 1920 for width in widths) or not 360 <= args.height <= 1080:
    parser.error("widths must be 320..1920 and height 360..1080")
output = args.output.resolve()
if root == output or root in output.parents:
    parser.error("output must be outside the source checkout")
work = output / "work"
work.mkdir(parents=True, exist_ok=True)
gui = work / "gui"
gui.mkdir(exist_ok=True)
for source in (root / "app/gui").glob("*.qml"):
    shutil.copy2(source, gui / source.name)
resources = ['<RCC><qresource prefix="/res">']
for source in (root / "app/res").rglob("*.svg"):
    resources.append(f'<file alias="{source.relative_to(root / "app/res")}">{source}</file>')
resources.append('</qresource><qresource prefix="/">')
resources.append(f'<file alias="manual/manual.json">{root}/shared/deskport-core/manual/manual.json</file>')
resources.append(f'<file alias="i18n/languages.json">{root}/shared/deskport-core/i18n/languages.json</file>')
for locale in json.loads((root / "shared/deskport-core/i18n/languages.json").read_text()):
    if locale["qt"] == "en":
        continue
    name = f'qml_{locale["qt"]}'
    qm = work / f"{name}.qm"
    subprocess.run([os.environ.get("DESKPORT_LRELEASE", "lrelease"), str(root / f"app/languages/{name}.ts"), "-qm", str(qm)], check=True, stdout=subprocess.DEVNULL)
    resources.append(f'<file alias="languages/{name}.qm">{qm}</file>')
resources.append('</qresource><qresource prefix="/gui">')
resources.extend(f'<file alias="{p.name}">{p}</file>' for p in gui.glob("*.qml"))
(work / "resources.qrc").write_text("\n".join(resources + ['</qresource></RCC>']))
(work / "fixture.pro").write_text(f'''QT += core gui widgets network qml quick quickcontrols2 testlib
CONFIG += console c++17 testcase release
CONFIG -= app_bundle debug debug_and_release
TARGET = desktop-typography
DESTDIR = .
INCLUDEPATH += "{root}/app"
SOURCES += "{root}/tests/desktop-typography.cpp" "{root}/app/settings/streamingpreferences.cpp" "{root}/app/gui/manual.cpp"
HEADERS += "{root}/app/settings/streamingpreferences.h" "{root}/app/gui/manual.h"
RESOURCES += resources.qrc
macx: QMAKE_MACOSX_DEPLOYMENT_TARGET = 14.0
win32: QTPLUGIN += qoffscreen qwindows
''')
env = dict(os.environ, QML_DISABLE_DISK_CACHE="1")
qmake = [os.environ.get("DESKPORT_QMAKE", "qmake"), *shlex.split(os.environ.get("DESKPORT_TYPOGRAPHY_QMAKE_ARGS", ""))]
subprocess.run([*qmake, "fixture.pro"], cwd=work, env=env, check=True)
subprocess.run(["make", f'-j{max(1, min(4, int(env.get("JOBS", "4"))))}'], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
if not args.build_only:
    env.update(QT_QPA_PLATFORM=args.platform, QT_SCALE_FACTOR="1", QT_SCREEN_SCALE_FACTORS="1")
    base_dpr = float(subprocess.check_output([str(work / "desktop-typography"), "--print-dpr"], env=env, text=True))
    for scale in (1, 2):
        evidence = output / f"scale-{scale}"
        evidence.mkdir(exist_ok=True)
        env.update(QT_QPA_PLATFORM=args.platform, QT_SCALE_FACTOR=str(scale / base_dpr),
                   QT_SCREEN_SCALE_FACTORS="1",
                   DESKPORT_TYPOGRAPHY_DPR=str(scale),
                   DESKPORT_TYPOGRAPHY_WIDTHS=",".join(map(str, widths)), DESKPORT_TYPOGRAPHY_HEIGHT=str(args.height),
                   QT_QUICK_CONTROLS_STYLE="Material",
                   TEST_GUI_DIR=str(gui), DESKPORT_TYPOGRAPHY_OUTPUT=str(evidence),
                   DESKPORT_TYPOGRAPHY_SETTINGS=str(output / f"settings-{scale}"),
                   XDG_CACHE_HOME=str(output / "cache"), XDG_CONFIG_HOME=str(output / "config"))
        if args.renderer == "software":
            env["QT_QUICK_BACKEND"] = "software"
        else:
            env.pop("QT_QUICK_BACKEND", None)
        subprocess.run([str(work / "desktop-typography"), *(["modalFocus"] if args.focus_only else [])], cwd=work, env=env, check=True)
