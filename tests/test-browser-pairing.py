#!/usr/bin/env python3
"""Run real persistent-browser pairing with isolated native capture.

Accepts the same arguments as test-browser-native.py. The fixture uses a private
local control socket to restart/revoke only its own HTTPS gateway. Example:
  python3 tests/test-browser-pairing.py --help
All output, including private browser profiles, stays in --output.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import runpy
import sys

scripts = Path(__file__).resolve().parent
sys.argv.extend(['--pairing-control', '--driver', str(scripts/'test-browser-pairing.mjs')])
runpy.run_path(str(scripts/'test-browser-native.py'), run_name='__main__')
