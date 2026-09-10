#!/usr/bin/env python3
"""Reject runtime references to the build machine, including nested components."""
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
errors = []
for path in root.rglob('*'):
    if path.is_symlink() or not path.is_file():
        continue
    kind = subprocess.check_output(['/usr/bin/file', '-b', str(path)], text=True)
    if 'Mach-O' not in kind:
        continue
    signature = subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(path)], capture_output=True, text=True)
    if signature.returncode:
        errors.append(f'{path.relative_to(root)}: invalid code signature: {signature.stderr.strip()}')
    output = subprocess.check_output(['/usr/bin/otool', '-arch', 'arm64', '-L', str(path)], text=True)
    for line in output.splitlines()[1:]:
        dependency = line.strip().split(' (')[0]
        if dependency.startswith(('/opt/homebrew/', '/usr/local/', '/nix/', '/Users/')):
            errors.append(f'{path.relative_to(root)}: {dependency}')
if errors:
    raise SystemExit('\n'.join(errors))
print('PASS: every Mach-O signature verified; no Homebrew, Nix or user-directory linked libraries in bundle')
