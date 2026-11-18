#!/usr/bin/env python3
"""Materialize the verified, repository-owned host source without network access."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import hashlib
import json
from pathlib import Path
import shutil
import stat
import sys
import tarfile


# These empty upstream directory placeholders are not user retention markers.
# Bind the exception to the reviewed archive; a vendor update must be reviewed
# again. Keep every unknown .keep and never modify the original source archive.
UPSTREAM_EMPTY_PLACEHOLDERS = {
    '63d7b9c71e0ed759b85ddfe1d8e65520a5c36454703ec524c5d831c4e2ef204d': (
        'third-party/build-deps/patches/FFmpeg/FFmpeg/SVT-AV1/.keep',
        'third-party/build-deps/patches/FFmpeg/FFmpeg/libva/.keep',
        'third-party/build-deps/patches/FFmpeg/FFmpeg/nv-codec-headers/.keep',
        'third-party/build-deps/patches/FFmpeg/FFmpeg/x264/.keep',
        'third-party/build-deps/patches/FFmpeg/FFmpeg/x265/.keep',
        'third-party/moonlight-common-c/enet/m4/.keep',
    ),
}


def remove_upstream_empty_placeholders(target, archive_sha256):
    """Remove only reviewed empty regular files in a verified generated tree."""
    candidates = []
    for relative in UPSTREAM_EMPTY_PLACEHOLDERS.get(archive_sha256, ()):
        path = target / relative
        for parent in path.parents:
            if parent == target:
                break
            if parent.is_symlink():
                raise SystemExit(f'Upstream placeholder has a symlink parent: {relative}')
        try:
            metadata = path.lstat()
        except FileNotFoundError:
            continue
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_size != 0:
            raise SystemExit(f'Upstream placeholder is not an empty regular file: {relative}')
        candidates.append(path)
    # Validate the whole set before changing any file.
    for path in candidates:
        path.unlink()


def prepare_source(root, target):
    manifest = json.loads((root / 'host/vendor/sunshine.json').read_text())
    archive = root / 'host/vendor' / manifest['archive']
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    if checksum != manifest['sha256']:
        raise SystemExit('Vendored host checksum mismatch')
    target = target.resolve()
    # Do not replace a caller's checkout or unknown directory.
    marker = target / '.deskport-vendored-source'
    if target.exists() and not marker.is_file():
        raise SystemExit(f'Refusing to overwrite unmanaged source directory: {target}')
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    with tarfile.open(archive) as source:
        source.extractall(target, filter='data')
    # Existing reviewed overlays use git apply, which also works outside a Git
    # repository. This is regenerable build input, not an independent checkout.
    if any(path.name == '.git' for path in target.rglob('.git')):
        raise SystemExit('Vendored host archive unexpectedly contains Git metadata')
    remove_upstream_empty_placeholders(target, checksum)
    marker.write_text(checksum + '\n')
    print(f'Prepared vendored host at {target}')


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f'Usage: {sys.argv[0]} TARGET')
    prepare_source(Path(__file__).resolve().parents[1], Path(sys.argv[1]))


if __name__ == '__main__':
    main()
