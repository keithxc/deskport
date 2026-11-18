#!/usr/bin/env python3
"""Offline checks for a viewer-only modified Qt Wayland runtime and materials."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True


spec = importlib.util.spec_from_file_location('runtime',
    Path(__file__).resolve().parents[1].joinpath('scripts', 'bundle-appimage-runtime.py'))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class QtWaylandInputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='deskport-qtwayland-inputs-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'modified Qt build'
        self.prefix = self.root / 'relocated AppDir/usr'
        (self.stage / 'usr/lib').mkdir(parents=True)
        (self.prefix / 'lib').mkdir(parents=True)
        self.library = self.stage / 'usr/lib/libQt6WaylandClient.so.6'
        self.library.write_bytes(b'patched Qt Wayland fixture')
        self.old = self.prefix / 'lib/libQt6WaylandClient.so.6.4.2'
        self.old.write_bytes(b'original distro library')
        (self.prefix / 'lib/libQt6WaylandClient.so.6').symlink_to(self.old.name)
        self.host_library = self.prefix / 'libexec/sunshine/usr/lib/libQt6WaylandClient.so.6'
        self.host_library.parent.mkdir(parents=True)
        self.host_library.write_bytes(b'isolated host runtime fixture')
        materials = []
        for name in ('COPYING', 'LICENSES/LGPL-3.0-only.txt', 'source.tar.xz',
                     'qt-wayland-screen-lifetime.patch', 'build-appimage-qt-wayland.sh', 'RELINK.md'):
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('fixture ' + name).encode())
            materials.append(dict(path=name, bytes=path.stat().st_size,
                                  checksum=runtime.file_checksum(path)))
        self.metadata = dict(binary='qtwayland-deskport-backport', version='6.4.2-deskport.1',
                             source='qtwayland', source_version='6.4.2', modified=True,
                             archives=[dict(url='https://example.invalid/qtwayland-6.4.2.tar.xz',
                                            filename='qtwayland-6.4.2.tar.xz', bytes=42,
                                            checksum='SHA256:' + 'a' * 64)], materials=materials)
        self.write_metadata()

    def write_metadata(self):
        (self.stage / 'source.json').write_text(json.dumps(self.metadata))

    def test_replace_only_viewer_and_preserve_verified_materials(self):
        metadata, library = runtime.copy_qt_wayland_runtime(self.prefix, self.stage)
        self.assertEqual(library, self.library)
        self.assertFalse(self.old.exists())
        installed = self.prefix / 'lib/libQt6WaylandClient.so.6'
        self.assertEqual(installed.read_bytes(), self.library.read_bytes())
        self.assertFalse(installed.is_symlink())
        self.assertEqual(self.host_library.read_bytes(), b'isolated host runtime fixture')
        self.assertTrue(metadata['modified'])
        for item in metadata['materials'] + metadata['runtime_files']:
            path = self.prefix.parent / item['path']
            self.assertEqual(path.stat().st_size, item['bytes'])
            self.assertEqual(runtime.file_checksum(path), item['checksum'])
        self.assertTrue((self.prefix / 'share/doc/deskport/qtwayland-backport/source.json').is_file())

    def test_modified_library_cannot_claim_an_unmodified_package(self):
        self.metadata['modified'] = False
        self.write_metadata()
        with self.assertRaisesRegex(RuntimeError, 'recorded as modified'):
            runtime.copy_qt_wayland_runtime(self.prefix, self.stage)
        self.assertEqual(self.old.read_bytes(), b'original distro library')

    def test_missing_or_modified_patch_fails_before_replacing_library(self):
        patch = self.stage / 'qt-wayland-screen-lifetime.patch'
        original = patch.read_bytes()
        for payload in (None, b'changed patch'):
            with self.subTest(payload=payload):
                if payload is None:
                    patch.unlink()
                else:
                    patch.write_bytes(payload)
                with self.assertRaises(RuntimeError):
                    runtime.copy_qt_wayland_runtime(self.prefix, self.stage)
                self.assertEqual(self.old.read_bytes(), b'original distro library')
                self.assertFalse((self.prefix / 'share/doc/deskport/qtwayland-backport').exists())
                patch.write_bytes(original)

    def test_materials_must_remain_inside_the_runtime_prefix(self):
        self.metadata['materials'][0]['path'] = '../COPYING'
        self.write_metadata()
        with self.assertRaisesRegex(RuntimeError, 'material path'):
            runtime.copy_qt_wayland_runtime(self.prefix, self.stage)
        self.assertEqual(self.old.read_bytes(), b'original distro library')


if __name__ == '__main__':
    unittest.main()
