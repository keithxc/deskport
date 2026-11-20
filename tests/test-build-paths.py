#!/usr/bin/env python3
"""Check managed desktop paths and portable mounts without starting a build."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = Path(os.environ.get('DESKPORT_TEST_DIR') or
    (subprocess.check_output(['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'],
                             text=True).strip() + '/tests'))


class BuildPathsTests(unittest.TestCase):
    def setUp(self):
        BASE.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix='paths-', dir=BASE)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith('DESKPORT_')}
        self.environment['DESKPORT_BUILD_ROOT'] = str(self.directory / 'outputs')

    def resolve(self, name, **environment):
        return subprocess.run(['bash', str(ROOT / 'scripts/build-paths.sh'), name],
                              env={**self.environment, **environment}, text=True,
                              capture_output=True)

    def test_printing_paths_does_not_create_them(self):
        result = self.resolve('work', DESKPORT_RUN_ID='audit-1', DESKPORT_TARGET='macos')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.strip().endswith('/runs.noindex/audit-1/macos/work'))
        self.assertFalse((self.directory / 'outputs').exists())

    def test_run_ids_are_single_components(self):
        for run_id in ('', '.', '..', '../escape', 'one/two', 'a b', 'a\nb', 'a$b'):
            with self.subTest(run_id=run_id):
                self.assertEqual(self.resolve('work', DESKPORT_RUN_ID=run_id,
                                              DESKPORT_TARGET='macos').returncode, 2)
        self.assertEqual(self.resolve('work', DESKPORT_RUN_ID='good').returncode, 2)

    def test_source_and_store_outputs_are_rejected(self):
        for path in ('build', str(ROOT / 'build'), str(ROOT / 'build/../dist'), '/nix/store/test-output'):
            with self.subTest(path=path):
                self.assertEqual(self.resolve('checkout', DESKPORT_BUILD_ROOT=path).returncode, 2)
        link = self.directory / 'source-link'
        link.symlink_to(ROOT, target_is_directory=True)
        self.assertEqual(self.resolve('checkout', DESKPORT_BUILD_ROOT=str(link / 'build')).returncode, 2)
        self.assertEqual(self.resolve('macos-build', DESKPORT_MACOS_BUILD_DIR='build').returncode, 2)
        self.assertEqual(self.resolve('dependency-cache', DESKPORT_DEPENDENCY_CACHE_DIR='cache').returncode, 2)

    def test_target_layout_and_cache_override(self):
        for target in ('macos', 'linux-nix', 'linux-portable', 'windows'):
            for purpose in ('work', 'dist', 'logs', 'tests'):
                with self.subTest(target=target, purpose=purpose):
                    result = self.resolve(purpose, DESKPORT_RUN_ID='run.1', DESKPORT_TARGET=target)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue(result.stdout.strip().endswith(f'/runs.noindex/run.1/{target}/{purpose}'))
        cache = self.directory / 'dependencies'
        self.assertEqual(self.resolve('dependency-cache', DESKPORT_RUN_ID='run.1',
                                     DESKPORT_TARGET='macos',
                                     DESKPORT_DEPENDENCY_CACHE_DIR=str(cache)).stdout.strip(), str(cache))

    def test_sourced_resolver_reuses_exported_environment(self):
        environment = dict(self.environment, DESKPORT_RUN_ID='inherited',
                           DESKPORT_TARGET='windows', DESKPORT_WINDOWS_DEPENDENCY_BUILD='1')
        result = subprocess.run([
            'bash', '-c',
            'for pass in first second; do source "$1" || exit $?; '
            'printf "%s\\n" "$DESKPORT_BUILD_ROOT" "$DESKPORT_CHECKOUT_BUILD_ROOT" '
            '"$DESKPORT_WORK_DIR" "$DESKPORT_DEPENDENCY_CACHE_DIR"; '
            'bash "$1" work || exit $?; done',
            'test', str(ROOT / 'scripts/build-paths.sh'),
        ], env=environment, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(len(lines), 10, result.stdout)
        self.assertEqual(lines[:5], lines[5:])
        self.assertEqual(lines[0], environment['DESKPORT_BUILD_ROOT'])
        self.assertTrue(lines[2].endswith('/runs.noindex/inherited/windows/work'))
        self.assertEqual(lines[2], lines[4], 'Child resolver must inherit the same output root')
        self.assertFalse((self.directory / 'outputs').exists())

    def test_legacy_paths_and_host_variable_are_compatible(self):
        result = self.resolve('macos-build', DESKPORT_NIX_DEPS='1')
        self.assertTrue(result.stdout.strip().endswith('/macos-arm64.noindex/nix/build'))
        override = str(self.directory / 'legacy-build.noindex')
        self.assertEqual(self.resolve('macos-build', DESKPORT_MACOS_BUILD_DIR=override).stdout.strip(), override)
        result = subprocess.run(['bash', '-c', 'source "$1"; test -z "${DESKPORT_HOST_SOURCE_DIR+x}"',
                                 'test', str(ROOT / 'scripts/build-paths.sh')], env=self.environment)
        self.assertEqual(result.returncode, 0)

    def portable(self, result=0):
        binary = self.directory / 'bin'
        binary.mkdir(exist_ok=True)
        podman = binary / 'podman'
        podman.write_text(f'#!/bin/sh\nprintf "%s\\n" "$@" > "$CAPTURE"\nexit {result}\n')
        podman.chmod(0o755)
        environment = {**self.environment, 'DESKPORT_RUN_ID': 'portable-test',
                       'DESKPORT_TARGET': 'linux-portable',
                       'DESKPORT_DEPENDENCY_CACHE_DIR': str(self.directory / 'dependencies'),
                       'PATH': str(binary) + os.pathsep + os.environ['PATH'],
                       'CAPTURE': str(self.directory / 'podman-args')}
        completed = subprocess.run(['bash', str(ROOT / 'scripts/package-linux.sh')], env=environment,
                                   text=True, capture_output=True)
        work = Path(self.resolve('work', **{k: v for k, v in environment.items()
                                           if k.startswith('DESKPORT_')}).stdout.strip())
        return completed, work

    def test_portable_shared_cache_and_dist_mounts(self):
        completed, work = self.portable()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertFalse((work / 'cache').is_symlink())
        self.assertFalse((work / 'output').is_symlink())
        arguments = (self.directory / 'podman-args').read_text().splitlines()
        self.assertIn(f'{self.directory}/dependencies:/work/cache', arguments)
        self.assertIn(f'{work.parent}/dist:/work/output', arguments)
        self.assertFalse((self.directory / 'dependencies/.deskport-build.lock').exists())
        # The same managed layout is reusable, including after a failed command.
        completed, _ = self.portable(result=19)
        self.assertEqual(completed.returncode, 19, completed.stderr)
        self.assertFalse((self.directory / 'dependencies/.deskport-build.lock').exists())

    def test_legacy_portable_works_with_system_bash(self):
        binary = self.directory / 'bin'
        binary.mkdir()
        podman = binary / 'podman'
        podman.write_text('#!/bin/sh\nexit 0\n')
        podman.chmod(0o755)
        environment = {**self.environment,
                       'PATH': str(binary) + os.pathsep + os.environ['PATH']}
        bash = '/bin/bash' if Path('/bin/bash').exists() else shutil.which('bash', path=environment['PATH'])
        self.assertIsNotNone(bash, 'Bash is required: /bin/bash is absent and bash was not found in PATH')
        completed = subprocess.run([bash, str(ROOT / 'scripts/package-linux.sh')],
                                   env=environment, text=True, capture_output=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_portable_rejects_wrong_cache_link(self):
        completed, work = self.portable()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        (work / 'cache').rmdir()
        (work / 'cache').symlink_to(self.directory / 'unexpected')
        completed, _ = self.portable()
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(os.readlink(work / 'cache'), str(self.directory / 'unexpected'))

    def test_portable_does_not_steal_dependency_lock(self):
        cache = self.directory / 'dependencies'
        (cache / '.deskport-build.lock').mkdir(parents=True)
        (cache / '.deskport-build.lock/pid').write_text('other-owner\n')
        completed, _ = self.portable()
        self.assertEqual(completed.returncode, 2)
        self.assertEqual((cache / '.deskport-build.lock/pid').read_text(), 'other-owner\n')

    def test_host_patches_do_not_require_git_metadata(self):
        source = self.directory / 'host-source'
        source.mkdir()
        (source / 'example.txt').write_text('before\n')
        patch = self.directory / 'overlay.patch'
        patch.write_text('diff --git a/example.txt b/example.txt\n'
                         '--- a/example.txt\n+++ b/example.txt\n'
                         '@@ -1 +1 @@\n-before\n+after\n')
        subprocess.run(['git', '-C', str(source), 'apply', '--check', str(patch)], check=True)
        subprocess.run(['git', '-C', str(source), 'apply', str(patch)], check=True)
        self.assertEqual((source / 'example.txt').read_text(), 'after\n')
        self.assertFalse((source / '.git').exists())


if __name__ == '__main__':
    unittest.main()
