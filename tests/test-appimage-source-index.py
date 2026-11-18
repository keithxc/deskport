#!/usr/bin/env python3
"""Check superseded-source indexing without apt or network access."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import contextlib
import hashlib
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from unittest.mock import patch

script = Path(__file__).resolve().parents[1].joinpath('scripts', 'appimage-source-index.py')
digest = 'a' * 64
valid = (f'Source: fixture\nVersion: 1:2.3-4\nChecksums-Sha256:\n'
         f' {digest} 42 fixture_2.3.orig.tar.xz\n').encode()
with tempfile.TemporaryDirectory(prefix='deskport-source-index-') as temporary:
    root = Path(temporary)
    notices = root / 'runtime-licenses'
    notices.mkdir()
    (notices / 'packages.json').write_text(json.dumps({'fixture': {
        'binary': 'fixture', 'version': '1:2.3-4', 'source': 'fixture',
        'source_version': '1:2.3-4'}}))
    destination = root / 'index.json'
    for data, expected in [(valid, True),
                           (valid.replace(b'Source: fixture', b'Source: other'), False),
                           (valid.replace(b'Checksums-Sha256:', b'Checksums-Sha1:'), False),
                           (valid.replace(b'fixture_2.3.orig.tar.xz', b'../unsafe.tar.xz'), False)]:
        with patch.object(sys, 'argv', [str(script), str(root), str(destination)]), \
             patch('subprocess.check_output', side_effect=subprocess.CalledProcessError(100, ['apt-get'])), \
             patch('urllib.request.urlopen', return_value=io.BytesIO(data)) as download, \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                runpy.run_path(str(script), run_name='__main__')
            except RuntimeError:
                assert not expected
            else:
                assert expected
                result = json.loads(destination.read_text())[0]
                assert result['version'] == '1:2.3-4'
                dsc, archive = result['archives']
                assert dsc['filename'] == 'fixture_2.3-4.dsc'
                assert dsc['bytes'] == len(data)
                assert dsc['checksum'] == 'SHA256:' + hashlib.sha256(data).hexdigest()
                assert '/fixture/1%3A2.3-4/' in dsc['url']
                assert archive['bytes'] == 42 and archive['checksum'] == 'SHA256:' + digest
                assert archive['url'].endswith('/fixture_2.3.orig.tar.xz')
            assert download.call_count == 1
print('PASS: archived source identity, epoch filenames, SHA-256 and unsafe-record rejection')

with tempfile.TemporaryDirectory(prefix='deskport-modified-source-index-') as temporary:
    root = Path(temporary)
    notices = root / 'usr/share/doc/deskport/runtime-licenses'
    notices.mkdir(parents=True)
    patch_file = root / 'usr/share/doc/deskport/qtwayland-backport/screen.patch'
    patch_file.parent.mkdir(parents=True)
    patch_file.write_bytes(b'official fixture backport')
    library = root / 'usr/lib/libQt6WaylandClient.so.6'
    library.parent.mkdir(parents=True)
    library.write_bytes(b'modified shared library fixture')
    def material(path):
        return dict(path=str(path.relative_to(root)), bytes=path.stat().st_size,
                    checksum='SHA256:' + hashlib.sha256(path.read_bytes()).hexdigest())
    original = dict(binary='qtwayland-distro', version='6.4.2', source='qtwayland',
                    source_version='6.4.2', archives=[dict(url='https://example.invalid/qtwayland.tar.xz',
                        filename='qtwayland.tar.xz', bytes=42, checksum='SHA256:' + digest)])
    modified = dict(original, binary='qtwayland-backport', version='6.4.2-deskport.1',
                    modified=True, materials=[material(patch_file)], runtime_files=[material(library)],
                    modifications=['official screen-lifetime backport'])
    (notices / 'packages.json').write_text(json.dumps({'original': original, 'modified': modified}))
    destination = root / 'index.json'
    with patch.object(sys, 'argv', [str(script), str(root), str(destination)]), \
         patch('subprocess.check_output') as apt, patch('urllib.request.urlopen') as download, \
         contextlib.redirect_stdout(io.StringIO()):
        runpy.run_path(str(script), run_name='__main__')
        assert not apt.called and not download.called
    records = json.loads(destination.read_text())
    assert len(records) == 2, records
    record = next(item for item in records if item.get('modified'))
    assert record['binary_version'] == '6.4.2-deskport.1', record
    assert record['materials'] == modified['materials'], record
    assert record['runtime_files'] == modified['runtime_files'], record
    assert record['modifications'] == modified['modifications'], record
    for path in (patch_file, library):
        original_bytes = path.read_bytes()
        path.write_bytes(b'tampered input')
        with patch.object(sys, 'argv', [str(script), str(root), str(destination)]), \
             contextlib.redirect_stdout(io.StringIO()):
            try:
                runpy.run_path(str(script), run_name='__main__')
            except RuntimeError as error:
                assert 'checksum mismatch' in str(error), error
            else:
                raise AssertionError('Modified corresponding materials were accepted')
        path.write_bytes(original_bytes)
print('PASS: distinct modified provenance, retained source/patch metadata and material/binary integrity')
