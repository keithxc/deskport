#!/usr/bin/env python3
"""Link lifecycle assertions against a freshly built macOS desktop app.

Usage: nix develop -c python3 tests/test-mdns-lifecycle.py BUILD/app --work-dir OUTPUT
Rebuild the desktop after production changes first. This compiles only the
fixture, reuses Makefile.Release's objects/libraries, and leaves the app intact.
"""

import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage

import argparse
import os
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='Built app directory containing Makefile.Release')
    parser.add_argument('--work-dir', required=True, type=Path, help='Exclusive external fixture directory')
    args = parser.parse_args()
    storage = build_storage.bootstrap()
    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve(strict=True)
    if not (build / 'Makefile.Release').is_file():
        parser.error('build must contain Makefile.Release; build the desktop first')
    output = storage.validate(args.work_dir)
    if output.is_relative_to(root):
        parser.error('--work-dir must be outside source')
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='lifecycle-', dir=output))
    print(f'Test artifacts: {work}', flush=True)
    for name in ('tmp', 'cache', 'config', 'data'):
        (work / name).mkdir()
    env = dict(os.environ, TMPDIR=str(work / 'tmp'), TMP=str(work / 'tmp'),
               TEMP=str(work / 'tmp'), XDG_CACHE_HOME=str(work / 'cache'),
               XDG_CONFIG_HOME=str(work / 'config'), XDG_DATA_HOME=str(work / 'data'),
               QML_DISABLE_DISK_CACHE='1', PYTHONDONTWRITEBYTECODE='1')
    for key in ('CLANG_MODULE_CACHE_PATH', 'SWIFT_MODULECACHE_PATH', 'SWIFT_MODULE_CACHE_PATH',
                'COMPILATION_CACHE_CAS_PATH', 'CCACHE_DIR', 'SCCACHE_DIR', 'PYTHONPYCACHEPREFIX'):
        env[key] = str(work / 'cache' / key.lower())
    frameworks = str(build / 'DeskPort.app/Contents/Frameworks')
    env.update(DYLD_FRAMEWORK_PATH=frameworks, DYLD_LIBRARY_PATH=frameworks)
    env.pop('DESKPORT_SESSION_ENDPOINT', None)

    # Include compiler/link settings without building or replacing app targets.
    def make_path(path):
        return str(path).replace('$', '$$').replace('#', r'\#').replace(' ', r'\ ')

    makefile = work / 'test.mk'
    makefile.write_text(f'''include {make_path(build / 'Makefile.Release')}
.PHONY: mdns-lifecycle-regression
mdns-lifecycle-regression:
\t$(CXX) -c $(CXXFLAGS) $(INCPATH) "{root}/tests/mdns-lifecycle.cpp" -o "{work}/test.o"
\t$(LINK) $(LFLAGS) -o "{work}/mdns-lifecycle" "{work}/test.o" $(filter-out %/main.o main.o,$(OBJECTS)) $(LIBS)
''')
    commands = [(['make', '-f', str(makefile), 'mdns-lifecycle-regression'], build, 120),
                ([str(work / 'mdns-lifecycle'), str(work / 'config')], work, 60)]
    for index, (command, cwd, timeout) in enumerate(commands):
        log_path = work / f'step-{index}.log'
        with log_path.open('w') as log:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        if result.returncode or index == 1:
            print(log_path.read_text())
        if result.returncode:
            return result.returncode
    return 0


if __name__ == '__main__':
    sys.exit(main())
