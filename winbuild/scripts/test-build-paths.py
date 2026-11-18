#!/usr/bin/env python3
"""Exercise Windows run/cache ownership without compiling or downloading."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tarfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
SCRIPTS = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS))
import build_paths
from material_archive import material_filter
spec = importlib.util.spec_from_file_location('fetch_inputs', SCRIPTS / 'fetch-inputs.py')
fetch_inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch_inputs)


class WindowsPaths(unittest.TestCase):
    def setUp(self):
        checkout = subprocess.check_output(['bash', str(SCRIPTS.parents[1] / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
        test_root = Path(checkout) / 'tests/windows-build-paths'
        test_root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=test_root)
        self.root = Path(self.temporary.name)
        # Retain host storage policy and temporary-output routing while clearing
        # inherited DeskPort build overrides for each fixture.
        inherited = {key: os.environ[key] for key in (
            'HOME', 'PATH', 'MYGIT_BUILD_ROOT', 'MYGIT_PATH_RESOLVER',
            'TMPDIR', 'TMP', 'TEMP', 'PYTHONDONTWRITEBYTECODE',
        ) if key in os.environ}
        self.environment = patch.dict(os.environ, {
            **inherited,
            'DESKPORT_BUILD_ROOT': str(self.root / 'build'),
            'DESKPORT_DEPENDENCY_CACHE_DIR': str(self.root / 'dependencies'),
            'DESKPORT_RUN_ID': 'first',
        }, clear=True)
        # Shells need the normal test runner PATH, but no inherited build overrides.
        self.old_path = os.environ.get('PATH', '/usr/bin:/bin')
        self.environment.start()
        os.environ['PATH'] = self.old_path

    def tearDown(self):
        self.environment.stop()
        self.temporary.cleanup()

    def test_runs_share_only_dependencies_and_survive_other_run_removal(self):
        first = build_paths.initialize(build_paths.paths())
        (first['cache'] / 'prefix/lib').mkdir(parents=True)
        library = first['cache'] / 'prefix/lib/example.a'
        library.write_bytes(b'fixture')
        os.environ['DESKPORT_RUN_ID'] = 'second'
        second = build_paths.initialize(build_paths.paths())
        self.assertEqual(first['cache'], second['cache'])
        self.assertNotEqual(first['work'], second['work'])
        record = json.loads((second['work'] / '.deskport-windows-paths.json').read_text())
        for relative, target in record['links'].items():
            self.assertEqual((second['work'] / relative).resolve(), Path(target).resolve())
            self.assertNotIn('runs.noindex', Path(target).parts)
        for relative in ('build-app', 'full/sunshine-prepared', 'full/host-build-prepared', 'full/deskport-maintenance.exe', 'stage'):
            self.assertFalse((second['work'] / relative).is_symlink())
        shutil.rmtree(first['work'].parent)
        self.assertEqual((second['work'] / 'prefix/lib/example.a').read_bytes(), b'fixture')
        self.assertFalse(second['dist'].exists(), 'Packaging owns creation of its output directory')
        self.assertEqual(second['logs'], second['work'].parent / 'logs')

    def test_wrong_existing_link_is_rejected(self):
        config = build_paths.initialize(build_paths.paths())
        (config['work'] / 'prefix').unlink()
        (config['work'] / 'prefix').symlink_to(self.root / 'unrelated')
        with self.assertRaisesRegex(SystemExit, 'unexpected Windows cache link'):
            build_paths.initialize(config)

    def test_existing_unmanaged_directory_is_not_adopted(self):
        config = build_paths.paths()
        (config['work'] / 'prefix').mkdir(parents=True)
        (config['work'] / 'prefix/keep').write_text('do not replace')
        with self.assertRaisesRegex(SystemExit, 'replace existing Windows output'):
            build_paths.initialize(config)
        self.assertEqual((config['work'] / 'prefix/keep').read_text(), 'do not replace')

    def test_cache_cannot_live_in_run(self):
        config = build_paths.paths()
        os.environ['DESKPORT_DEPENDENCY_CACHE_DIR'] = str(config['work'] / 'cache')
        with self.assertRaisesRegex(SystemExit, 'must not be inside a build run'):
            build_paths.paths()

    def test_dependency_input_override_changes_cache_identity(self):
        override = self.root / 'input.tar'
        override.write_bytes(b'first input')
        os.environ['S_ZLIB'] = str(override)
        first = build_paths.paths()['cache']
        override.write_bytes(b'changed input')
        self.assertNotEqual(first, build_paths.paths()['cache'])

    def test_core_pin_only_reuses_verified_cache_without_moving_paths(self):
        old = self.root / 'old-source'
        for relative in build_paths.CACHE_RECIPES:
            target = old / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(build_paths.SOURCE / relative, target)
        lock_path = old / 'flake.lock'
        lock = json.loads(lock_path.read_text())
        lock['nodes']['deskport-core']['locked']['rev'] = 'old-application-core'
        lock_path.write_text(json.dumps(lock))
        cache = self.root / 'dependencies' / build_paths.recipe_key(old)
        (cache / 'qt-static/lib').mkdir(parents=True)
        (cache / 'scripts').symlink_to(old / 'winbuild/scripts')
        self.assertEqual(build_paths.paths()['cache'], cache)
        initialized = build_paths.initialize(build_paths.paths())
        self.assertEqual((initialized['cache'] / 'scripts').resolve(), old / 'winbuild/scripts')
        # The same policy cannot admit a dependency/toolchain change.
        (old / 'winbuild/inputs.json').write_text('{}')
        self.assertNotEqual(build_paths.paths()['cache'], cache)

    def test_existing_placement_keeps_generated_objects(self):
        root = self.root / 'placements'
        root.mkdir()
        archive = self.root / 'input'
        archive.write_bytes(b'fixture')
        entry = {'name': 'fixture', 'extract': 'full/media-source/fixture', 'sha256': 'fixture'}
        destination = root / entry['extract']
        def initial_place(*args):
            destination.mkdir(parents=True)
            (destination / 'source').write_text('source')
            return destination
        with patch.object(fetch_inputs, 'place', side_effect=initial_place) as place:
            fetch_inputs.place_once(entry, archive, root)
            (destination / 'generated.o').write_bytes(b'compiled')
            fetch_inputs.place_once(entry, archive, root)
            self.assertEqual(place.call_count, 1)
        self.assertTrue((destination / 'generated.o').is_file())

    def test_replacing_parent_invalidates_nested_input_placement(self):
        root = self.root / 'placements'
        root.mkdir()
        marker = root / '.input-placements.json'
        marker.write_text(json.dumps({'source-inputs/lib/dependency': 'old'}))
        entry = {'name': 'fixture', 'extract': 'source-inputs/lib', 'sha256': 'changed'}
        with patch.object(fetch_inputs, 'place', return_value=root / entry['extract']):
            fetch_inputs.place_once(entry, self.root / 'archive', root)
        self.assertNotIn('source-inputs/lib/dependency', json.loads(marker.read_text()))

    def test_dependency_recipe_uses_canonical_cache_paths(self):
        config = build_paths.paths()
        env = dict(os.environ, DESKPORT_WINDOWS_CACHE_LOCK_HELD='1')
        result = subprocess.run([
            'bash', '-c',
            # The second source inherits every export from the first recipe.
            'export DESKPORT_WINDOWS_DEPENDENCY_BUILD=1; '
            'for pass in first second; do source "$1"; '
            'printf "%s\\n" "$WB" "$PREFIX" "$WORK" "$LOGS"; done',
            'test', str(SCRIPTS / 'env.sh'),
        ], env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [str(config['cache']), str(config['cache'] / 'prefix'),
                                                     str(config['cache'] / 'work'), str(config['logs'])] * 2)

    def test_common_resolver_failure_is_propagated(self):
        # A failed source must not be masked by successful printf commands or
        # replaced with relative defaults under the caller's source checkout.
        os.environ['DESKPORT_BUILD_ROOT'] = str(build_paths.SOURCE)
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            build_paths.common_paths()
        self.assertEqual(failure.exception.returncode, 2)
        self.assertIn(b'Output', failure.exception.stderr)
        self.assertEqual(failure.exception.stdout, b'')

    def test_python_cache_lock_excludes_a_second_writer(self):
        config = build_paths.initialize(build_paths.paths())
        lock = build_paths.hold_cache_lock(config)
        try:
            result = subprocess.run([
                sys.executable, '-c',
                'import fcntl,sys; f=open(sys.argv[1], "a"); fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)',
                str(config['cache'] / '.build.lock'),
            ], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('BlockingIOError', result.stderr)
        finally:
            lock.close()

    def test_material_archives_have_unique_members(self):
        source = self.root / 'source'
        (source / 'scripts/__pycache__').mkdir(parents=True)
        (source / 'scripts/helper.py').write_text('print("fixture")\n')
        (source / 'scripts/__pycache__/helper.pyc').write_bytes(b'not-source')
        # Exercise both material kinds, with a fresh filter for each archive.
        for kind in ('source', 'relink'):
            target = self.root / (kind + '.tar.gz')
            with tarfile.open(target, 'w:gz', dereference=True) as archive:
                include = material_filter()
                archive.add(source / 'scripts/helper.py', arcname='deskport/scripts/helper.py', filter=include)
                archive.add(source / 'scripts', arcname='deskport/scripts', filter=include)
            with tarfile.open(target) as archive:
                names = archive.getnames()
                self.assertEqual(len(names), len(set(names)))
                self.assertIn('deskport/scripts/helper.py', names)
                self.assertFalse(any('__pycache__' in name for name in names))

    def test_conflicting_material_member_is_rejected(self):
        include = material_filter()
        first = tarfile.TarInfo('same.txt')
        first.size = 1
        second = tarfile.TarInfo('same.txt')
        second.size = 2
        include(first)
        with self.assertRaisesRegex(ValueError, 'Conflicting material archive member'):
            include(second)

    def test_bootstrap_help_does_not_create_outputs(self):
        result = subprocess.run(['bash', str(SCRIPTS / 'bootstrap.sh'), '--help'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--mode build|package', result.stdout)
        self.assertFalse((self.root / 'build').exists())


if __name__ == '__main__':
    unittest.main()
