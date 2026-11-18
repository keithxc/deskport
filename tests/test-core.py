#!/usr/bin/env python3
"""Check the pinned core and the production Qt workspace adapter."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import json
import io
import tarfile
import tempfile
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--skip-deferred-seamless', action='store_true', help='Skip deferred Seamless fixtures when the task explicitly excludes them')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
core = root / 'shared/deskport-core'
if not (core / 'include/deskport/workspace.h').exists():
    raise SystemExit('Initialize shared/deskport-core with git submodule update --init')
if (core / '.git').exists():
    revision = subprocess.check_output(['git', '-C', str(core), 'rev-parse', 'HEAD'], text=True).strip()
    lock = json.loads((root / 'flake.lock').read_text())
    node = lock['nodes']['root']['inputs']['deskport-core']
    pinned = lock['nodes'][node]['locked']
    if pinned.get('type') == 'path':
        # Private test delivery preloads an immutable core snapshot instead of
        # publishing a source branch merely to make a GitHub pin downloadable.
        with tempfile.TemporaryDirectory(prefix='deskport-core-pin-') as tmp:
            archive = subprocess.check_output(['git', '-C', str(core), 'archive', revision])
            with tarfile.open(fileobj=io.BytesIO(archive)) as source:
                source.extractall(tmp, filter='data')
            digest = subprocess.check_output(['nix', 'hash', 'path', tmp], text=True).strip()
            if digest != pinned['narHash']:
                raise SystemExit('Core Git tree and private snapshot NAR pin differ')
    elif revision != pinned.get('rev'):
        raise SystemExit('Core submodule and flake.lock differ; update both pins before building')
subprocess.run([sys.executable, str(core / 'tests/test_session_graph.py')], check=True)
subprocess.run([sys.executable, str(core / 'tests/test_i18n.py')], check=True)
if not args.skip_deferred_seamless:
    subprocess.run([sys.executable, str(core / 'tests/test_seamless_protocol.py')], check=True)
subprocess.run([sys.executable, str((core) / 'portable/test_catalog.py')], check=True)
subprocess.run([sys.executable, str(core / 'tests/test_workspace.py'),
                '--qt-header', str(root / 'app/backend/workspaceresolution.h')], check=True)
