#!/usr/bin/env python3
"""Offline checks for coherent, selective host graphics runtime selection."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'linux', 'Linux graphics runtime requires GNU coreutils')
class GraphicsRuntime(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='deskport-graphics-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        self.system = self.root / 'system'
        self.system.mkdir()
        self.dri = self.system / 'dri'
        self.dri.mkdir()
        for name in ('libc.so.6', 'libm.so.6', 'libstdc++.so.6', 'libdrm.so.2'):
            (self.system / name).touch()
        (self.dri / 'radeonsi_drv_video.so').touch()
        self.script = self.root / 'graphics.sh'
        text = Path(__file__).resolve().parents[1].joinpath('scripts', 'appimage-graphics-env.sh').read_text()
        text = text.replace('dp_tools=/run/current-system/sw/bin:/usr/bin:/bin:/usr/sbin:/sbin',
                            'dp_tools=' + shlex.quote(str(self.tools) + ':/usr/bin:/bin'))
        self.script.write_text(text)
        self.prefix = self.root / 'relocated app/usr'
        (self.prefix / 'shared/bin').mkdir(parents=True)
        (self.prefix / 'shared/bin/deskport').touch()
        self.executable('getconf', '#!/bin/sh\nprintf "glibc ${MOCK_GLIBC:-2.42}\\n"\n')
        self.executable('ldd', '#!/bin/sh\n'
            + f'''printf "libc.so.6 => {self.system}/libc.so.6 (0x1)\\n"
               printf "libstdc++.so.6 => {self.system}/libstdc++.so.6 (0x2)\\n"
               printf "libdrm.so.2 => {self.system}/libdrm.so.2 (0x3)\\n"
               printf "{self.system}/ld-linux-x86-64.so.2 (0x4)\\n"\n'''
            + 'if [ "${MOCK_MISSING:-0}" = 1 ]; then printf "missing.so => not found\\n"; fi\n')
        self.loader = self.system / 'ld-linux-x86-64.so.2'
        self.loader.write_text('#!/bin/sh\nexit "${MOCK_LOADER_FAILURE:-0}"\n')
        self.loader.chmod(0o755)

    def executable(self, name, text):
        path = self.tools / name
        path.write_text(text)
        path.chmod(0o755)

    def run_selection(self, **overrides):
        env = dict(os.environ, HOME=str(self.root), XDG_CACHE_HOME=str(self.root / 'cache with spaces'),
                   LIBVA_DRIVERS_PATH=str(self.dri), DESKPORT_APPIMAGE_RUNTIME='auto')
        env.update(overrides)
        env.pop('SHARUN_LDNAME', None)
        env.pop('SHARUN_EXTRA_LIBRARY_PATH', None)
        env.pop('DESKPORT_APPIMAGE_GRAPHICS_RUNTIME', None)
        command = '. "$1"; deskport_graphics_env "$2" deskport; printf "%s\\n%s\\n%s\\n" "${SHARUN_LDNAME:-}" "${SHARUN_EXTRA_LIBRARY_PATH:-}" "${DESKPORT_APPIMAGE_GRAPHICS_RUNTIME:-}"'
        return subprocess.run(['sh', '-eu', '-c', command, 'test', str(self.script), str(self.prefix)],
                              env=env, capture_output=True, text=True, timeout=10, check=True)

    def test_modern_host_selects_matching_loader_and_only_graphics_closure(self):
        result = self.run_selection()
        loader, directory, persistent = result.stdout.splitlines()
        self.assertEqual(loader, str(self.loader))
        self.assertEqual(persistent, directory)
        cache = Path(directory)
        for name in ('libc.so.6', 'libm.so.6', 'libstdc++.so.6', 'libdrm.so.2'):
            self.assertEqual((cache / name).resolve(), self.system / name)
        self.assertFalse((cache / 'libQt6Core.so.6').exists())
        self.assertFalse((cache / 'libavcodec.so.60').exists())
        self.assertFalse((cache / 'libpipewire-0.3.so.0').exists())
        repeated = self.run_selection()
        self.assertEqual(result.stdout, repeated.stdout)
        self.assertFalse(list(cache.parent.glob('.resolve.*')))

    def test_old_host_keeps_private_runtime(self):
        result = self.run_selection(MOCK_GLIBC='2.35')
        self.assertEqual(result.stdout, '\n\n\n')
        self.assertFalse((self.root / 'cache with spaces').exists())

    def test_cache_reuse_does_not_require_diffutils(self):
        tools = self.root / 'minimal-tools'
        tools.mkdir()
        for name in ('sh', 'env', 'mkdir', 'mktemp', 'awk', 'sort', 'readlink',
                     'ln', 'rm', 'cksum', 'cat', 'mv'):
            (tools / name).symlink_to(shutil.which(name))
        self.assertFalse((tools / 'cmp').exists())
        first = self.run_selection(PATH=str(tools))
        second = self.run_selection(PATH=str(tools))
        self.assertEqual(first.stdout, second.stdout)
        self.assertIn(str(self.loader), second.stdout)

    def test_multilib_drivers_do_not_mix_a_32bit_libc_into_the_cohort(self):
        dri32 = self.root / 'dri32'
        dri32.mkdir()
        (dri32 / 'radeonsi_drv_video.so').touch()
        mock = (self.tools / 'ldd').read_text()
        mock = mock.replace('#!/bin/sh\n', '#!/bin/sh\n'
            + 'case "$1" in *dri32*) printf "libc.so.6 => /lib/libc.so.6 (0x1)\\n/lib/ld-linux.so.2 (0x2)\\n"; exit 0 ;; esac\n')
        self.executable('ldd', mock)
        result = self.run_selection(LIBVA_DRIVERS_PATH=str(self.dri) + ':' + str(dri32))
        loader, directory, persistent = result.stdout.splitlines()
        self.assertEqual(loader, str(self.loader))
        self.assertEqual(persistent, directory)
        self.assertNotIn('/lib/libc.so.6', (Path(directory) / 'inputs').read_text())

    def test_explicit_private_runtime_does_not_probe_the_host(self):
        result = self.run_selection(DESKPORT_APPIMAGE_RUNTIME='private')
        self.assertEqual(result.stdout, '\n\n\n')
        self.assertFalse((self.root / 'cache with spaces').exists())

    def test_incomplete_driver_closure_is_not_selected(self):
        result = self.run_selection(MOCK_MISSING='1')
        self.assertEqual(result.stdout, '\n\n\n')
        self.assertFalse(list((self.root / 'cache with spaces/DeskPort/graphics-runtime').glob('.resolve.*')))

    def test_loader_preflight_failure_keeps_private_runtime(self):
        result = self.run_selection(MOCK_LOADER_FAILURE='1')
        self.assertEqual(result.stdout, '\n\n\n')
        self.assertIn('incompatible host cohort', result.stderr)


if __name__ == '__main__':
    unittest.main()
