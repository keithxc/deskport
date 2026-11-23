#!/usr/bin/env python3
"""Build the production SDL renderer and exercise a real SDL dummy audio device.

Run in the locked desktop devShell (or set DESKPORT_QMAKE). Supply a checked
--output-dir; compiler output, caches and temporary files stay beneath it.
No personal streaming connection, hardware audio or system clipboard is used.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_storage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()

    # Storage resolves the installed shared policy before creating any output.
    storage = build_storage.Storage()
    output = storage.validate(args.output_dir)
    env = {**os.environ, **storage.environment()}
    env['DESKPORT_BUILD_ROOT'] = str(storage.paths['root'])
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='audio-idle-', dir=output) as directory:
        work = Path(directory)
        for key in ('TMPDIR', 'TMP', 'TEMP', 'TEMPDIR', 'XDG_CACHE_HOME',
                    'CLANG_MODULE_CACHE_PATH', 'COMPILATION_CACHE_CAS_PATH',
                    'PYTHONPYCACHEPREFIX', 'CCACHE_DIR', 'SCCACHE_DIR'):
            destination = work / key.lower()
            destination.mkdir()
            env[key] = str(destination)
        env.update(PYTHONDONTWRITEBYTECODE='1', CCACHE_DISABLE='1',
                   SDL_AUDIODRIVER='dummy', QT_QPA_PLATFORM='offscreen')
        project = f'''QT = core
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release
QMAKE_CXXFLAGS += -Wall -Wextra -UNDEBUG
SOURCES += "{ROOT}/tests/audio-idle.cpp" "{ROOT}/app/streaming/audio/renderers/sdlaud.cpp"
INCLUDEPATH += "{ROOT}/app" "{ROOT}/moonlight-common-c/moonlight-common-c/src"
TARGET = audio-idle
'''
        if sys.platform == 'darwin':
            frameworks = ROOT / 'libs/mac/Frameworks'
            env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
            project += f'''QMAKE_MACOSX_DEPLOYMENT_TARGET = 26.0
QMAKE_CXXFLAGS += -F"{frameworks}"
INCLUDEPATH += "{frameworks}/SDL2.framework/Versions/A/Headers"
LIBS += -F"{frameworks}" -framework SDL2
QMAKE_RPATHDIR += "{frameworks}"
'''
        else:
            project += 'CONFIG += link_pkgconfig\nPKGCONFIG += sdl2\n'
        (work / 'test.pro').write_text(project)
        commands = [
            [env.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'],
            [env.get('MAKE', 'make'), '-j2'],
            [str(work / ('audio-idle.exe' if sys.platform == 'win32' else 'audio-idle'))],
        ]
        for command in commands:
            subprocess.run(command, cwd=work, env=env, check=True, timeout=120)


if __name__ == '__main__':
    main()
