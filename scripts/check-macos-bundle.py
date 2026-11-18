#!/usr/bin/env python3
"""Reject runtime references to the build machine, including nested components."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
import pathlib
import plistlib
import signal
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
errors = []
scratch = (pathlib.Path(os.environ['DESKPORT_TEST_DIR']) if os.environ.get('DESKPORT_TEST_DIR')
           else pathlib.Path(os.environ.get(
               'DESKPORT_BUILD_ROOT', _build_storage.Storage().paths['root']))) / 'bundle-checks'
scratch = _build_storage.output(scratch)
scratch.mkdir(parents=True, exist_ok=True)
with (root / 'Contents/Info.plist').open('rb') as stream:
    info = plistlib.load(stream)
if not info.get('NSMicrophoneUsageDescription', '').strip():
    errors.append('Outer app is missing its microphone usage description')
resources = root / 'Contents/Resources'
if info.get('CFBundleIconName') != 'AppIcon':
    errors.append('Outer app is missing its native AppIcon name')
if info.get('CFBundleIconFile') != 'deskport.icns':
    errors.append('Outer app is missing its portable icon fallback')
for icon in ('Assets.car', 'deskport.icns'):
    if not (resources / icon).is_file() or not (resources / icon).stat().st_size:
        errors.append('Outer app is missing its icon resource: ' + icon)
recovery = root / 'Contents/Library/LaunchDaemons/io.github.keithxc.DeskPort.Recovery.plist'
try:
    service = plistlib.loads(recovery.read_bytes())
    assert service['Label'] == 'io.github.keithxc.DeskPort.Recovery'
    assert service['BundleProgram'] == 'Contents/Helpers/deskport-recovery'
    assert service['RunAtLoad'] is True and service['StartInterval'] == 30
    assert 'Program' not in service and 'ProgramArguments' not in service
    assert (root / service['BundleProgram']).is_file()
except (OSError, KeyError, AssertionError, plistlib.InvalidFileException) as error:
    errors.append(f'Invalid bundled unattended recovery service: {error}')
entitlements = plistlib.loads(subprocess.check_output(
    ['/usr/bin/codesign', '-d', '--entitlements', ':-', str(root)],
    stderr=subprocess.DEVNULL))
if entitlements.get('com.apple.security.device.audio-input') is not True:
    errors.append('Outer app is missing its audio-input entitlement')
for path in root.rglob('*'):
    if path.is_symlink() or not path.is_file():
        continue
    kind = subprocess.check_output(['/usr/bin/file', '-b', str(path)], text=True)
    if 'Mach-O' not in kind:
        continue
    signature = subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(path)], capture_output=True, text=True)
    if signature.returncode:
        errors.append(f'{path.relative_to(root)}: invalid code signature: {signature.stderr.strip()}')
    # Links are not the only build-machine dependency: Apple's libiconv as built
    # by Nix loads converter modules and tables from absolute store paths at run
    # time, so text conversion (and GLib error reporting) fails on other Macs.
    data = path.read_bytes()
    if b'/nix/store/' in data and (b'/share/i18n/csmapper' in data or b'/lib/i18n/' in data):
        errors.append(f'{path.relative_to(root)}: loads run-time converter data from the build machine')
    output = subprocess.check_output(['/usr/bin/otool', '-arch', 'arm64', '-L', str(path)], text=True)
    for line in output.splitlines()[1:]:
        dependency = line.strip().split(' (')[0]
        if dependency.startswith(('/opt/homebrew/', '/usr/local/', '/nix/', '/Users/')):
            errors.append(f'{path.relative_to(root)}: {dependency}')
host = root / 'Contents/Helpers/Sunshine.app/Contents/MacOS/Sunshine'
if not host.is_file():
    errors.append('Bundled Sunshine executable is missing')
elif not errors:
    # Signing and notarization do not resolve dependent symbols. Load the exact
    # nested executable from a write- and network-denied sandbox so dyld catches
    # ABI collisions without touching Sunshine, DeskPort or network state.
    with tempfile.TemporaryDirectory(prefix='deskport-host-help-', dir=scratch) as temporary:
        state = pathlib.Path(temporary)
        environment = {'HOME': temporary, 'TMPDIR': temporary,
                       'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'}
        process = subprocess.Popen([
            '/usr/bin/sandbox-exec', '-p',
            '(version 1) (allow default) (deny network*) (deny file-write*)',
            str(host), '--help'], cwd=state, env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            start_new_session=True)
        stdout = stderr = ''
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            errors.append('Bundled Sunshine dynamic-loader smoke timed out')
        finally:
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                remaining_stdout, remaining_stderr = process.communicate()
                stdout += remaining_stdout
                stderr += remaining_stderr
        output = stdout + stderr
        if process.returncode != 0 or 'Usage:' not in output:
            errors.append('Bundled Sunshine failed isolated dynamic-loader smoke: '
                          + output[-2000:].strip())
        if any(state.iterdir()):
            errors.append('Bundled Sunshine help smoke touched isolated state')
if errors:
    raise SystemExit('\n'.join(errors))
print('PASS: every Mach-O signature verified; no build-machine links; bundled Sunshine loads in isolated state')
