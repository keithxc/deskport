#!/usr/bin/env python3
"""Audit compiled helpers and a synthetic NSIS fixture; never execute Windows code."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import argparse
import hashlib
import re
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--helpers', type=Path, required=True)
parser.add_argument('--display-helper', type=Path, required=True)
parser.add_argument('--output-directory', type=Path, required=True)
args = parser.parse_args()
out = args.output_directory.resolve()
out.mkdir(parents=True, exist_ok=False)
payload = out / 'payload'
(payload / 'host').mkdir(parents=True)
(payload / 'driver').mkdir()
for name in ['deskport-display-service.exe', 'deskport-display-recovery.exe']:
    shutil.copyfile(args.helpers / name, payload / 'host' / name)
for name in ['deskport-maintenance.exe', 'deskport-driver-setup.exe']:
    shutil.copyfile(args.helpers / name, payload / name)
shutil.copyfile(args.display_helper, payload / 'host/deskport-display.exe')
summary = []
for path in sorted(payload.rglob('*.exe')):
    data = path.read_bytes()
    assert data[:2] == b'MZ', path
    offset = struct.unpack_from('<I', data, 60)[0]
    assert data[offset:offset + 4] == b'PE\0\0'
    assert struct.unpack_from('<H', data, offset + 4)[0] == 0x8664
    pe = subprocess.check_output(['objdump', '-p', str(path)], text=True)
    imports = re.findall(r'DLL Name: (.+)', pe)
    assert not any(re.search(r'qt[56]|libstdc|libgcc|libmcfgthread|libwinpthread', dll, re.I) for dll in imports), imports
    summary.append(f'{path.relative_to(payload)} {hashlib.sha256(data).hexdigest()} imports={",".join(imports)}')
(out / 'helper-audit.txt').write_text('\n'.join(summary) + '\n')

# Explicitly synthetic client/host/driver placeholders. No real host, driver,
# credentials, source materials or private release output is reused or produced.
shutil.copyfile(args.helpers / 'deskport-maintenance.exe', payload / 'DeskPort.exe')
shutil.copyfile(args.helpers / 'deskport-display-service.exe', payload / 'host/deskport-host.exe')
(payload / 'driver/MttVDD.inf').write_text('; Synthetic invalid driver. Never install this fixture.\n')
(payload / 'driver/MttVDD.dll').write_bytes(b'SYNTHETIC-NOT-A-DRIVER')
(payload / 'LICENSE.txt').write_text('Synthetic NSIS compilation fixture. Do not install.\n')
(payload / 'SYNTHETIC-DO-NOT-INSTALL.txt').write_text('Build/extraction test only; not a DeskPort package.\n')
shutil.copyfile(root / 'app/deskport.ico', payload / 'deskport.ico')
setup = out / 'Synthetic-DeskPort-service-fixture.exe'
with (out / 'nsis.log').open('w') as log:
    subprocess.run(['makensis', '-V2', '-DVERSION=0.0.0', '-DPAYLOAD=' + str(payload),
                    '-DOUTFILE=' + str(setup), '-DAUDIT_UNINSTALLER=' + str(out / 'Uninstall.exe'),
                    str(root / 'winbuild/scripts/deskport-full.nsi')], check=True, stdout=log, stderr=subprocess.STDOUT)
with (out / 'extract.log').open('w') as log:
    subprocess.run(['7z', 'x', '-y', str(setup), '-o' + str(out / 'extracted')], check=True, stdout=log)
for relative in ['host/deskport-display-service.exe', 'host/deskport-display-recovery.exe',
                 'host/deskport-display.exe', 'deskport-maintenance.exe']:
    assert (out / 'extracted' / relative).read_bytes() == (payload / relative).read_bytes(), relative
assert (out / 'Uninstall.exe').is_file()
print('PASS x64/static-runtime helper audits and synthetic NSIS compile/extraction; no Windows binary executed')
