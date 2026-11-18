#!/usr/bin/env python3
"""Exercise path policy adapters without builds, devices, or disk utilities."""
import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_storage


class Helper:
    def __init__(self, root):
        self.root = Path(root)
        self.validated = []

    def workspace_root(self): return ROOT.parent
    def host_policy(self, unused):
        return {'require_external_build_root': True, 'archive_mount': '/Volumes/mm4_2t'}
    def native_platform(self): return 'macos-arm64'
    def paths(self, args):
        if args.build_root is not None:
            raise AssertionError('A final output was passed as a shared base root')
        root = self.root / args.project
        checkout = root / 'checkout-123'
        platform = checkout / args.task / args.platform
        return dict(root=root, checkout=checkout, task=checkout / args.task,
                    platform=platform, **{key: platform / key for key in
                    ('work', 'cache', 'tests', 'logs', 'dist', 'sources', 'evidence', 'releases', 'rollback')})
    def verify_external_root(self, path, policy):
        self.validated.append(path)
        if not Path(path).is_relative_to(self.root):
            raise ValueError('external output required')


class LegacyHelper:
    """Old deployed helper has paths/native_platform but no newer policy API."""
    def native_platform(self): return 'linux-x86_64'
    def paths(self, args):
        return Helper(Path.home() / 'mygit/build').paths(args)


class CurrentOtherHost(Helper):
    def host_policy(self, unused): return {}


class StorageTests(unittest.TestCase):
    def setUp(self):
        self.root = Path('/Volumes/mm4_2t/data/mygit-build/storage-test-no-write')
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(('DESKPORT_', 'MYGIT_', 'CCACHE_', 'SCCACHE_', 'CARGO_'))}
        self.env.pop('JAVA_TOOL_OPTIONS', None)

    def test_legacy_helper_other_host_accepts_exact_subdirectory(self):
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=LegacyHelper()), patch.object(build_storage, 'on_mm4', return_value=False):
            storage = build_storage.Storage(task='unit', target='linux-x86_64')
            output = storage.paths['cache'] / 'exact-child'
            self.assertEqual(storage.validate(output), output)
            self.assertEqual(storage.environment()['MYGIT_CACHE_DIR'], str(storage.paths['cache']))

    def test_legacy_helper_on_mm4_fails_closed_before_mkdir(self):
        with patch.object(build_storage, 'shared_helper', return_value=LegacyHelper()), patch.object(build_storage, 'on_mm4', return_value=True), patch.object(Path, 'mkdir', side_effect=AssertionError('created output')):
            with self.assertRaisesRegex(ValueError, 'policy API'):
                build_storage.Storage()

    def test_current_helper_other_host_accepts_exact_output_subdirectory(self):
        helper = CurrentOtherHost(Path.home() / 'mygit/build')
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=helper), patch.object(build_storage, 'on_mm4', return_value=False):
            storage = build_storage.Storage(task='unit', target='linux-x86_64')
            output = storage.paths['cache'] / 'existing-approved-child'
            self.assertEqual(storage.validate(output), output)
            self.assertEqual(storage.environment()['MYGIT_CACHE_DIR'], str(storage.paths['cache']))

    def test_missing_helper_other_host_uses_writable_temp_not_home(self):
        env = dict(self.env, HOME='/nonexistent/unwritable', TMPDIR='/private/tmp')
        with patch.dict(os.environ, env, clear=True), patch.object(build_storage, 'shared_helper', return_value=None), patch.object(build_storage, 'on_mm4', return_value=False), patch('tempfile.gettempdir', return_value='/private/tmp'):
            storage = build_storage.Storage()
            values = storage.environment()
            self.assertTrue(values['TMPDIR'].startswith('/private/tmp/'))
            self.assertTrue(values['MYGIT_CACHE_DIR'].startswith('/private/tmp/'))

    def test_windows_dependency_mode_is_not_an_output_path(self):
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=LegacyHelper()), patch.object(build_storage, 'on_mm4', return_value=False):
            storage = build_storage.Storage()
            for mode in ('0', '1'):
                with self.subTest(mode=mode), patch.dict(os.environ, {
                    'DESKPORT_WINDOWS_DEPENDENCY_BUILD': mode,
                    'DESKPORT_WINDOWS_BUILD_ROOT': str(storage.paths['work']),
                }), patch.object(storage, 'validate', wraps=storage.validate) as validate:
                    storage.environment()
                    self.assertNotIn(mode, [str(call.args[0]) for call in validate.call_args_list])
                    self.assertIn(str(storage.paths['work']),
                                  [str(call.args[0]) for call in validate.call_args_list])

    def test_windows_dependency_mode_keeps_source_output_rejection(self):
        env = dict(self.env, DESKPORT_WINDOWS_DEPENDENCY_BUILD='1',
                   DESKPORT_WINDOWS_BUILD_ROOT=str(ROOT / 'runs.noindex/first/windows/work'))
        with patch.dict(os.environ, env, clear=True), patch.object(build_storage, 'shared_helper', return_value=LegacyHelper()), patch.object(build_storage, 'on_mm4', return_value=False):
            with self.assertRaisesRegex(ValueError, 'Output inside a source repository'):
                build_storage.Storage().environment()

    def test_explicit_internal_output_is_rejected_before_mkdir(self):
        helper = Helper(self.root)
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=helper), patch.object(build_storage, 'on_mm4', return_value=True), patch.object(Path, 'mkdir', side_effect=AssertionError('created output')):
            storage = build_storage.Storage()
            with patch.object(sys, 'argv', ['test.py', '--output', '/Users/example/internal']):
                with self.assertRaises(ValueError):
                    build_storage.check_arguments(storage, sys.argv[1:])
            accepted = storage.paths['tests'] / 'exact-child'
            self.assertEqual(storage.validate(accepted), accepted)
            self.assertNotIn('/deskport/deskport/', str(accepted))

    def test_tool_flags_preserve_selectors_and_map_real_caches(self):
        helper = Helper(self.root)
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=helper), patch.object(build_storage, 'on_mm4', return_value=True):
            storage = build_storage.Storage(task='unit', target='macos-arm64')
            args = build_storage.xcode_command(storage, ['-scheme', 'DeskPort', '-only-testing:Suite/test', 'test'])
            self.assertIn('-only-testing:Suite/test', args)
            for flag in ('-derivedDataPath', '-clonedSourcePackagesDirPath', '-packageCachePath'):
                self.assertIn(flag, args)
                self.assertTrue(Path(args[args.index(flag) + 1]).is_relative_to(self.root))
            gradle = build_storage.gradle_command(storage, ['gradlew', 'testDebugUnitTest', '--tests', 'example.Test'])
            self.assertIn('example.Test', gradle)
            self.assertIn('--project-cache-dir', gradle)
            with self.assertRaises(ValueError):
                build_storage.gradle_command(storage, ['gradlew', '--project-cache-dir', '/Users/example/cache'])
            with self.assertRaises(ValueError):
                build_storage.xcode_command(storage, ['-derivedDataPath', '/Users/example/DerivedData'])

    def test_screenshot_options_before_positional_paths_remain_supported(self):
        helper = Helper(self.root)
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=helper), patch.object(build_storage, 'on_mm4', return_value=True):
            storage = build_storage.Storage()
            output = str(storage.paths['tests'] / 'screenshots')
            argv = ['--device', 'iphone', '--languages', 'en', '/input/result.xcresult', output]
            with patch.object(sys, 'argv', ['export-store-screenshots.py', *argv]):
                build_storage.check_arguments(storage, argv)
            with patch.object(sys, 'argv', ['export-store-screenshots.py', *argv[:-1], '/Users/example/out']):
                with self.assertRaises(ValueError):
                    build_storage.check_arguments(storage, argv[:-1] + ['/Users/example/out'])

    def test_short_tmp_is_task_and_project_scoped(self):
        helper = Helper(Path('/Volumes/mm4_2t/data/mygit-build'))
        with patch.dict(os.environ, self.env, clear=True), patch.object(build_storage, 'shared_helper', return_value=helper), patch.object(build_storage, 'on_mm4', return_value=True):
            first = build_storage.Storage(task='one').environment()['TMPDIR']
            second = build_storage.Storage(task='two').environment()['TMPDIR']
            self.assertNotEqual(first, second)
            self.assertLess(len(os.fsencode(first + '/socket-fixture-12345678/serial.sock')), 104)


if __name__ == '__main__':
    unittest.main()
