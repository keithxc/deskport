#!/usr/bin/env python3
"""Verify narrowly scoped upstream placeholder handling in generated host trees."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
# Loading the command for unit tests must not create bytecode in the checkout.
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('prepare_host_source', ROOT / 'scripts/prepare-host-source.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
CHECKSUM, PLACEHOLDERS = next(iter(prepare.UPSTREAM_EMPTY_PLACEHOLDERS.items()))


class UpstreamPlaceholders(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = os.environ.get('DESKPORT_TEST_DIR')
        if not output:
            checkout = subprocess.check_output(
                ['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
            output = str(Path(checkout) / 'tests')
        cls.test_root = Path(output)
        cls.test_root.mkdir(parents=True, exist_ok=True)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='host-placeholders-', dir=self.test_root)
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.target = self.work / 'generated'
        self.target.mkdir()

    def placeholders(self):
        for relative in PLACEHOLDERS:
            path = self.target / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

    def test_reviewed_vendor_archive_has_exact_empty_placeholders(self):
        vendor = ROOT / 'host/vendor'
        manifest = json.loads((vendor / 'sunshine.json').read_text())
        archive = vendor / manifest['archive']
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), CHECKSUM)
        self.assertEqual(manifest['sha256'], CHECKSUM)
        with tarfile.open(archive) as source:
            members = [member for member in source if Path(member.name).name == '.keep']
        self.assertEqual({member.name for member in members}, set(PLACEHOLDERS))
        self.assertEqual(len(members), 6)
        self.assertTrue(all(member.isfile() and member.size == 0 for member in members))

    def test_only_exact_empty_placeholders_are_removed(self):
        self.placeholders()
        unknown = (self.target / '.keep', self.target / 'user-retained/.keep')
        unknown[0].touch()
        unknown[1].parent.mkdir()
        unknown[1].write_text('Retain this source\n')
        prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        for relative in PLACEHOLDERS:
            self.assertFalse((self.target / relative).exists())
            self.assertTrue((self.target / relative).parent.is_dir())
        self.assertEqual(unknown[0].read_bytes(), b'')
        self.assertEqual(unknown[1].read_text(), 'Retain this source\n')

    def test_materialized_vendor_tree_has_no_upstream_retention_markers(self):
        generated = self.work / 'verified-source'
        with contextlib.redirect_stdout(io.StringIO()):
            prepare.prepare_source(ROOT, generated)
        self.assertEqual(list(generated.rglob('.keep')), [])
        self.assertEqual(list(generated.rglob('.git')), [])
        self.assertEqual((generated / '.deskport-vendored-source').read_text(), CHECKSUM + '\n')
        for relative in PLACEHOLDERS:
            self.assertTrue((generated / relative).parent.is_dir())

    def test_nonempty_known_path_is_rejected_before_any_removal(self):
        self.placeholders()
        (self.target / PLACEHOLDERS[-1]).write_text('Retain this source\n')
        with self.assertRaisesRegex(SystemExit, 'not an empty regular file'):
            prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        self.assertTrue(all((self.target / relative).is_file() for relative in PLACEHOLDERS))

    def test_directory_at_known_path_is_rejected(self):
        path = self.target / PLACEHOLDERS[0]
        path.mkdir(parents=True)
        with self.assertRaisesRegex(SystemExit, 'not an empty regular file'):
            prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        self.assertTrue(path.is_dir())

    def test_symlink_at_known_path_is_rejected(self):
        outside = self.work / 'outside.keep'
        outside.touch()
        path = self.target / PLACEHOLDERS[0]
        path.parent.mkdir(parents=True)
        path.symlink_to(outside)
        with self.assertRaisesRegex(SystemExit, 'not an empty regular file'):
            prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        self.assertTrue(path.is_symlink())
        self.assertTrue(outside.is_file())

    def test_symlink_parent_is_rejected(self):
        outside = self.work / 'outside'
        outside.mkdir()
        (outside / '.keep').touch()
        path = self.target / PLACEHOLDERS[0]
        path.parent.parent.mkdir(parents=True)
        path.parent.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(SystemExit, 'symlink parent'):
            prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        self.assertTrue((outside / '.keep').is_file())

    def test_unreviewed_archive_keeps_all_placeholders(self):
        self.placeholders()
        prepare.remove_upstream_empty_placeholders(self.target, '0' * 64)
        self.assertTrue(all((self.target / relative).is_file() for relative in PLACEHOLDERS))

    def test_missing_placeholders_are_allowed(self):
        prepare.remove_upstream_empty_placeholders(self.target, CHECKSUM)
        self.assertEqual(list(self.target.iterdir()), [])

    def test_checksum_failure_preserves_existing_managed_tree(self):
        vendor = self.work / 'repository/host/vendor'
        vendor.mkdir(parents=True)
        (vendor / 'source.tar.gz').write_bytes(b'unverified source archive')
        (vendor / 'sunshine.json').write_text(json.dumps({'archive': 'source.tar.gz', 'sha256': CHECKSUM}))
        marker = self.target / '.deskport-vendored-source'
        marker.write_text('existing marker\n')
        retained = self.target / '.keep'
        retained.write_text('retained\n')
        with self.assertRaisesRegex(SystemExit, 'checksum mismatch'):
            prepare.prepare_source(self.work / 'repository', self.target)
        self.assertEqual(marker.read_text(), 'existing marker\n')
        self.assertEqual(retained.read_text(), 'retained\n')


if __name__ == '__main__':
    unittest.main()
