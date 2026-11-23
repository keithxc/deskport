#!/usr/bin/env python3
"""Build and execute the real Pacer with synthetic frames and VSync sources.

Run inside the locked desktop devShell (or set DESKPORT_QMAKE). All generated
files, compiler caches, and temporary output stay under --output-dir. No GUI,
network session, hardware decoder, or system clipboard is used.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


sys.path.insert(0, str(ROOT / "scripts"))
import build_storage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    storage = build_storage.bootstrap()
    output = storage.validate(args.output_dir)
    env = dict(os.environ)
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="pacer-resources-", dir=output) as directory:
        work = Path(directory)
        for key in ("TMPDIR", "TMP", "TEMP", "TEMPDIR", "XDG_CACHE_HOME", "CLANG_MODULE_CACHE_PATH"):
            target = work / key.lower()
            target.mkdir()
            env[key] = str(target)
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        env["CCACHE_DISABLE"] = "1"
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["SDL_VIDEODRIVER"] = "dummy"

        project = f'''QT += gui qml
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release
QMAKE_CXXFLAGS += -Wall -Wextra -UNDEBUG
SOURCES += "{ROOT}/tests/pacer-resources.cpp" "{ROOT}/app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp"
INCLUDEPATH += "{ROOT}/app" "{ROOT}/moonlight-common-c/moonlight-common-c/src"
TARGET = pacer-resources
'''
        if sys.platform == "darwin":
            frameworks = ROOT / "libs/mac/Frameworks"
            libraries = ROOT / "libs/mac/lib"
            env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
            project += f'''QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0
QMAKE_CXXFLAGS += -F"{frameworks}"
INCLUDEPATH += "{ROOT}/libs/mac/include" "{frameworks}/SDL2.framework/Versions/A/Headers" "{frameworks}/SDL2_ttf.framework/Versions/A/Headers"
LIBS += -F"{frameworks}" -framework SDL2 "{libraries}/libavutil.59.dylib" "{libraries}/libswscale.8.dylib"
QMAKE_RPATHDIR += "{frameworks}" "{libraries}"
'''
        else:
            project += "CONFIG += link_pkgconfig\nPKGCONFIG += sdl2 SDL2_ttf libavcodec libavutil libswscale\n"
            if sys.platform == "win32":
                project += f'SOURCES += "{ROOT}/app/streaming/video/ffmpeg-renderers/pacer/dxvsyncsource.cpp"\n'
        (work / "test.pro").write_text(project)
        commands = [
            [env.get("DESKPORT_QMAKE", "qmake"), "test.pro"],
            [env.get("MAKE", "make"), "-j2"],
            [str(work / ("pacer-resources.exe" if sys.platform == "win32" else "pacer-resources"))],
        ]
        for command in commands:
            subprocess.run(command, cwd=work, env=env, check=True, timeout=120)


if __name__ == "__main__":
    main()
