#!/usr/bin/env python3
"""Native regression for a selected AppImage runtime across sharun children.

Set DESKPORT_TEST_SHARUN to the pinned packaging input. The fixture uses synthetic
libraries and processes only; it does not start a desktop or inspect user state.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True


spec = importlib.util.spec_from_file_location('appimage_runtime',
    Path(__file__).resolve().parents[1].joinpath('scripts', 'bundle-appimage-runtime.py'))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


@unittest.skipUnless(sys.platform == 'linux' and os.environ.get('DESKPORT_TEST_SHARUN'),
                     'Linux and the pinned DESKPORT_TEST_SHARUN input are required')
class SharunChildren(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='deskport-sharun-runtime-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'relocated app/usr'
        self.binaries = self.prefix / 'shared/bin'
        self.private = self.prefix / 'shared/lib'
        self.public = self.prefix / 'bin'
        self.cohort = self.root / 'host cohort with spaces'
        for directory in (self.binaries, self.private, self.public, self.cohort):
            directory.mkdir(parents=True)
        shutil.copy2(os.environ['DESKPORT_TEST_SHARUN'], self.prefix / 'sharun')
        source = self.root / 'marker.c'
        source.write_text('int deskport_test_marker(void) { return MARKER; }\n')
        for directory, marker in ((self.private, 17), (self.cohort, 41)):
            self.compile('-shared', '-fPIC', f'-DMARKER={marker}', str(source),
                         '-Wl,-soname,libdeskport-test-marker.so.1',
                         '-o', str(directory / 'libdeskport-test-marker.so.1'))
        executable = self.root / 'process.c'
        executable.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern int deskport_test_marker(void);
int main(int argc, char **argv) {
    int marker = deskport_test_marker();
    printf("marker=%d\\n", marker);
    fflush(stdout);
    if (marker != atoi(getenv("DESKPORT_TEST_MARKER"))) return 77;
    const char *extra = getenv("SHARUN_EXTRA_LIBRARY_PATH");
    if (extra && *extra) return 78;
    if (argc > 1) {
        execl(argv[1], argv[1], (char *)0);
        return 79;
    }
    return 0;
}
''')
        self.compile(str(executable), '-L' + str(self.private),
                     '-l:libdeskport-test-marker.so.1',
                     '-o', str(self.binaries / 'deskport'))
        shutil.copy2(self.binaries / 'deskport', self.binaries / 'deskport-display')
        for name in ('deskport', 'deskport-display'):
            os.link(self.prefix / 'sharun', self.public / name)
        (self.private / 'lib.path').write_text('+\n')
        headers = subprocess.check_output(['readelf', '-l', str(self.binaries / 'deskport')],
                                          text=True)
        self.loader = re.search(r'Requesting program interpreter: ([^]]+)', headers).group(1)
        runtime.write_sharun_environment(self.prefix)

    def compile(self, *arguments):
        subprocess.run([os.environ.get('CC', 'cc'), *arguments], check=True,
                       capture_output=True, text=True, timeout=30)

    def run_fixture(self, modern=True):
        env = dict(os.environ, SHARUN_LDNAME=self.loader,
                   DESKPORT_TEST_MARKER='41' if modern else '17')
        for name in ('SHARUN_DIR', 'SHARUN_EXTRA_LIBRARY_PATH',
                     'DESKPORT_APPIMAGE_GRAPHICS_RUNTIME', 'LD_LIBRARY_PATH', 'LD_PRELOAD'):
            env.pop(name, None)
        if modern:
            env['SHARUN_EXTRA_LIBRARY_PATH'] = str(self.cohort)
            env['DESKPORT_APPIMAGE_GRAPHICS_RUNTIME'] = str(self.cohort)
        return subprocess.run([str(self.public / 'deskport'), str(self.public / 'deskport-display')],
                              env=env, capture_output=True, text=True, timeout=10)

    def test_selected_cohort_survives_display_helper_launch(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, 'marker=41\nmarker=41\n')

    def test_missing_restore_reproduces_the_child_runtime_mismatch(self):
        (self.prefix / '.env').unlink()
        result = self.run_fixture()
        self.assertEqual(result.returncode, 77, result.stdout + result.stderr)
        self.assertEqual(result.stdout, 'marker=41\nmarker=17\n')

    def test_private_runtime_stays_private_for_the_helper(self):
        result = self.run_fixture(modern=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, 'marker=17\nmarker=17\n')


if __name__ == '__main__':
    unittest.main()
