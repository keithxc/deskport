#!/usr/bin/env python3
"""Execute production SCM installer logic with API doubles; optionally build a native launch fixture."""
import argparse
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory', required=True, type=Path)
parser.add_argument('--compile-nsis-fixture', action='store_true')
args = parser.parse_args()
out = args.output_directory.resolve()
out.mkdir(parents=True, exist_ok=True)
production = (root / 'host/windows/display-service-install.h').read_text()
(out / 'production-service-install.h').write_text('\n'.join(
    line for line in production.splitlines() if not line.startswith(('#include ', '#pragma '))))
binary = out / 'windows-service-install'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(out), str(root / 'tests/windows-service-install.cpp'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
installer = (root / 'winbuild/scripts/deskport-full.nsi').read_text()
commands = re.findall(r'^  !insertmacro CheckedExecWait (.+)$', installer, re.M)
assert len(commands) == 11, commands
assert not re.search(r'^\s*ExecWait\s', installer, re.M)
for command in commands:
    following = installer.split('!insertmacro CheckedExecWait ' + command, 1)[1]
    assert following.lstrip().startswith('${If} $0'), command
assert 'ClearErrors\n  SetOutPath "$INSTDIR"\n  File /r' in installer
assert installer.index('could not copy its complete payload') < installer.index('register-display-service ')
assert 'HKCU "Software\\DeskPort' not in installer  # identity/settings are never deleted
print('PASS all 11 helper launches fail closed; partial copy aborts before service registration')
if args.compile_nsis_fixture:
    subprocess.run(['makensis', '-V2', '-DSOURCE=' + str(root),
                    '-DOUTFILE=' + str(out / 'checked-exec-fixture.exe'),
                    str(root / 'tests/windows-installer-exec.nsi')], check=True)
    print('PASS native process-launch fixture compiled; execution is a separate Windows check')
