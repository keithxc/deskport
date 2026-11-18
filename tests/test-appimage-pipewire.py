#!/usr/bin/env python3
"""Offline tests for the dynamically loaded AppImage PipeWire inputs."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('runtime',
    Path(__file__).resolve().parents[1].joinpath('scripts', 'bundle-appimage-runtime.py'))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class PipeWireInputs(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='deskport-pipewire-inputs-')
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.system = root / 'system'
        self.share = root / 'share'
        self.prefix = root / 'relocated path with spaces/usr'
        sources = [self.system / 'spa-0.2' / name for name in runtime.SPA_PLUGINS]
        sources += [self.system / 'pipewire-0.3' / f'libpipewire-module-{name}.so'
                    for name in runtime.PIPEWIRE_MODULES]
        sources += [self.share / name for name in ('client.conf', 'client-rt.conf')]
        for source in sources:
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(str(source.relative_to(root)).encode())
        self.sources = sources

    def test_complete_relocatable_client_runtime(self):
        inputs = runtime.copy_pipewire_runtime(self.prefix, self.system, self.share)
        self.assertEqual(len(inputs), 12)
        self.assertEqual({source for source, _ in inputs}, set(self.sources))
        for source, target in inputs:
            self.assertTrue(target.is_relative_to(self.prefix))
            self.assertEqual(source.read_bytes(), target.read_bytes())
        self.assertFalse((self.prefix / 'bin/pipewire').exists())
        self.assertFalse((self.prefix / 'share/pipewire/pipewire.conf').exists())

    def test_every_missing_input_fails_before_copy(self):
        for source in self.sources:
            with self.subTest(source=source):
                original = source.read_bytes()
                source.unlink()
                try:
                    with self.assertRaisesRegex(RuntimeError, 'Missing PipeWire client runtime input'):
                        runtime.copy_pipewire_runtime(self.prefix, self.system, self.share)
                    self.assertFalse(self.prefix.exists())
                finally:
                    source.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
