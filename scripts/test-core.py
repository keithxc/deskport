#!/usr/bin/env python3
"""Check the pinned core and the production Qt workspace adapter."""
import json
import io
import tarfile
import tempfile
from pathlib import Path
import subprocess
import sys

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
subprocess.run([sys.executable, str((core) / 'portable/test_catalog.py')], check=True)
subprocess.run([sys.executable, str(core / 'tests/test_workspace.py'),
                '--qt-header', str(root / 'app/backend/workspaceresolution.h')], check=True)
