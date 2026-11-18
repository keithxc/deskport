#!/usr/bin/env python3
"""Portable regression for native timestamps and protocol frame identity."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
checkout = subprocess.check_output(['bash', str(root / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
output = Path(os.environ.get('DESKPORT_TEST_DIR', Path(checkout) / 'tests'))
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='deskport-vt-clock-', dir=output) as tmp:
    binary = Path(tmp) / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root), str(root / 'tests/vt-clock.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
