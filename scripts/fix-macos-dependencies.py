#!/usr/bin/env python3
"""Complete and relocate a Homebrew Qt deployment before signing it.

Run separately on each application before nesting and signing its bundle.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

app = Path(sys.argv[1]).resolve()
frameworks = app / 'Contents/Frameworks'
frameworks.mkdir(exist_ok=True)
magic = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}
def machos(root):
    for path in root.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as f:
                if f.read(4) in magic:
                    yield path

def resolve(dep, binary):
    if dep.startswith('@loader_path/'):
        candidate = binary.parent / dep.removeprefix('@loader_path/')
        if candidate.exists():
            return candidate.resolve()
    if dep.startswith('@executable_path/'):
        candidate = app / 'Contents/MacOS' / dep.removeprefix('@executable_path/')
        if candidate.exists():
            return candidate.resolve()
    suffix = dep.removeprefix('@rpath/').removeprefix('@loader_path/')
    for candidate in [frameworks / suffix, Path('/opt/homebrew/lib') / suffix, Path(dep)]:
        if candidate.exists():
            return candidate.resolve()
    for candidate in Path('/opt/homebrew/opt').glob('*/lib/' + suffix):
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError(f'Cannot resolve {dep} from {binary}')

processed = set()
while True:
    pending = [p for p in machos(app) if p not in processed]
    if not pending:
        break
    for binary in pending:
        binary.chmod(binary.stat().st_mode | 0o200)
        output = subprocess.check_output(['otool', '-arch', 'arm64', '-L', str(binary)], text=True)
        edits = []
        for line in output.splitlines()[1:]:
            dep = line.strip().split(' (')[0]
            if dep.startswith(('/System/', '/usr/lib/')):
                continue
            source = resolve(dep, binary)
            target = source
            if not source.is_relative_to(app):
                parts = source.parts
                framework = next((i for i, part in enumerate(parts) if part.endswith('.framework')), None)
                if framework is not None:
                    src_root = Path(*parts[:framework + 1])
                    dest_root = frameworks / src_root.name
                    if not dest_root.exists():
                        subprocess.run(['ditto', str(src_root), str(dest_root)], check=True)
                    target = dest_root / Path(*parts[framework + 1:])
                else:
                    target = frameworks / source.name
                    if not target.exists():
                        shutil.copy2(source, target)
            if target.resolve() == binary.resolve():
                continue
            new = '@loader_path/' + os.path.relpath(target, binary.parent)
            if new != dep:
                edits.extend(['-change', dep, new])
        identity = subprocess.check_output(['otool', '-arch', 'arm64', '-D', str(binary)], text=True).splitlines()
        if len(identity) > 1 and binary.is_relative_to(frameworks):
            edits.extend(['-id', '@rpath/' + str(binary.relative_to(frameworks))])
        commands = subprocess.check_output(['otool', '-arch', 'arm64', '-l', str(binary)], text=True).splitlines()
        for index, line in enumerate(commands):
            if line.strip() == 'cmd LC_RPATH':
                path = commands[index + 2].strip().removeprefix('path ').split(' (offset')[0]
                if path.startswith(('/opt/homebrew/', '/usr/local/', '/nix/', '/Users/')):
                    edits.extend(['-delete_rpath', path])
        if edits:
            subprocess.run(['install_name_tool', *edits, str(binary)], check=True, stderr=subprocess.DEVNULL)
        processed.add(binary)
print(f'Relocated {len(processed)} Mach-O files into the application')
