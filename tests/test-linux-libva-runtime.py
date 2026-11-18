#!/usr/bin/env python3
"""Check VA runtime file/source atomicity, without executing the fixture ELFs."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('deskport_va_runtime',
    Path(__file__).resolve().parents[1].joinpath('scripts', 'bundle-appimage-runtime.py'))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class VaRuntimeFiles(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='deskport-va-files-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.prefix = self.root / 'candidate/usr'
        (self.prefix / 'lib').mkdir(parents=True)
        self.old = self.prefix / 'lib/libva-wayland.so.2.2000.0'
        self.old.write_bytes(b'old-platform-adapter')
        self.stage = self.root / 'stage'
        (self.stage / 'usr/lib').mkdir(parents=True)
        self.metadata = dict(source='libva', source_version='2.23.0', archives=[])
        (self.stage / 'source.json').write_text(json.dumps(self.metadata))
        (self.stage / 'COPYING').write_text('Synthetic upstream license fixture\n')
        for name in runtime.LIBVA_LIBRARIES:
            (self.stage / 'usr/lib' / name).write_bytes(b'\x7fELFsynthetic-' + name.encode())

    def test_missing_adapter_preserves_the_entire_previous_runtime(self):
        (self.stage / 'usr/lib/libva-wayland.so.2').unlink()
        with self.assertRaisesRegex(RuntimeError, 'Missing pinned VA-API runtime'):
            runtime.install_libva_runtime(self.prefix, self.stage)
        self.assertEqual(self.old.read_bytes(), b'old-platform-adapter')
        self.assertEqual(list((self.prefix / 'lib').iterdir()), [self.old])

    def test_all_adapters_replace_together_with_matching_source_and_license(self):
        runtime.install_libva_runtime(self.prefix, self.stage)
        self.assertFalse(self.old.exists())
        documents = self.prefix / 'share/doc/deskport/libva-runtime'
        metadata = json.loads((documents / 'source.json').read_text())
        self.assertEqual(metadata['source_version'], '2.23.0')
        self.assertEqual((documents / 'COPYING').read_bytes(), (self.stage / 'COPYING').read_bytes())
        self.assertEqual(len(metadata['runtime_files']), 4)
        for item in metadata['runtime_files']:
            target = self.prefix.parent / item['path']
            self.assertEqual(target.read_bytes(), (self.stage / 'usr/lib' / target.name).read_bytes())
            self.assertEqual(item['checksum'], 'SHA256:' + hashlib.sha256(target.read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
