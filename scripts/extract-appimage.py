#!/usr/bin/env python3
"""Extract type-2 AppImages without invoking a host-specific binfmt handler."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import pathlib
import subprocess
import sys

source, destination = map(pathlib.Path, sys.argv[1:])
data = source.read_bytes()
offset = 0
while True:
    offset = data.find(b'hsqs', offset)
    if offset < 0:
        raise SystemExit(f'No valid SquashFS filesystem in {source}')
    check = subprocess.run(['unsquashfs', '-s', '-o', str(offset), str(source)],
                           capture_output=True)
    if check.returncode == 0:
        break
    offset += 4
subprocess.run(['unsquashfs', '-no-progress', '-d', str(destination), '-o',
                str(offset), str(source)], check=True)
