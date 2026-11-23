#!/usr/bin/env python3
"""Off-screen Metal P010 equivalence and timings; synthetic buffers only."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage

import argparse
import os
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
# Validate without bootstrap's shared cache preparation: this test owns only
# its explicit output directory, including every compiler temporary/cache.
scratch = build_storage.Storage().validate(args.output)
scratch.mkdir(parents=True, exist_ok=False)
for name in ('tmp', 'modules', 'cache'):
    (scratch / name).mkdir()
env = dict(os.environ, TMPDIR=str(scratch / 'tmp'),
           TMP=str(scratch / 'tmp'), TEMP=str(scratch / 'tmp'),
           CLANG_MODULE_CACHE_PATH=str(scratch / 'modules'),
           XDG_CACHE_HOME=str(scratch / 'cache'), PYTHONDONTWRITEBYTECODE='1')
root = Path(__file__).resolve().parents[1]
binary = scratch / 'metal-color-conversion'
subprocess.run(['xcrun', '--no-cache', 'clang', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                '-I', str(root), str(root / 'tests/metal-color-conversion.m'),
                '-framework', 'Foundation', '-framework', 'CoreVideo',
                '-framework', 'Metal', '-o', str(binary)], env=env, check=True)
with (scratch / 'report.json').open('w') as report:
    subprocess.run([str(binary)], env=env, stdout=report, check=True, timeout=180)
print((scratch / 'report.json').read_text(), end='')
