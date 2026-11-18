#!/usr/bin/env python3
"""One bounded, isolated macOS VM for desktop package validation."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
sys.dont_write_bytecode = True
import build_storage

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'scripts/macos-vm-lock.json').read_text())
LAB = Path.home() / 'Library/DeskPortVM.noindex'
TART = LAB / 'tools/tart.app/Contents/MacOS/tart'
NAME = LOCK['vm_name']
ENV = dict(os.environ, TART_HOME=str(LAB / 'tart'), TART_NO_AUTO_PRUNE='1', DO_NOT_TRACK='1')
GIB = 2**30
MIN_FREE = 80 * GIB
MAX_USED = 65 * GIB


def test_directories():
    """Keep VM disks/locks in the lab; share only checked test outputs."""
    storage = build_storage.Storage(task=os.environ.get('DESKPORT_VM_RUN', 'vm-test'),
                                    target='macos-arm64')
    env = storage.environment()
    shared = storage.validate(storage.paths['work'] / 'vm-shared.noindex')
    os.environ.update(env)
    storage.prepare(env)
    return shared / 'input', shared / 'results', storage.paths['cache'] / 'swift-modules'


def status():
    LAB.mkdir(parents=True, exist_ok=True)
    used = int(subprocess.check_output(['du', '-sk', str(LAB)], text=True).split()[0]) * 1024
    free = shutil.disk_usage(LAB).free
    return {'free_gib': round(free/GIB, 2), 'lab_allocated_gib': round(used/GIB, 2),
            'minimum_free_gib': 80, 'maximum_lab_gib': 65, 'safe': free >= MIN_FREE and used <= MAX_USED}


def guard():
    state = status()
    if not state['safe']:
        raise RuntimeError('Disk safety threshold reached: ' + json.dumps(state))


def tart(*args, **kwargs):
    return subprocess.run([str(TART), *args], env=ENV, check=True, **kwargs)


def supervised(args):
    guard()
    child = subprocess.Popen([str(TART), *args], env=ENV)
    try:
        while child.poll() is None:
            time.sleep(5)
            # A finished APFS clone can momentarily double du's shared-block count.
            # Let bootstrap immediately prune its source cache before the final check.
            if child.poll() is None:
                guard()
        if child.returncode:
            raise RuntimeError(f'Tart exited with status {child.returncode}')
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=15)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


def package_test(arguments):
    inputs, results, swift_cache = test_directories()
    guard()
    version = LOCK['package_version']
    if arguments:
        package = Path(arguments[0]).resolve()
    else:
        release_root = Path(subprocess.check_output(
            ['bash', str(ROOT / 'scripts/build-paths.sh'), 'releases'], text=True).strip())
        package = release_root / f'v{version}-release/DeskPort-{version}-macos-arm64.zip'
    if not package.is_file():
        raise RuntimeError('Pass the published ZIP path to the test command')
    if hashlib.sha256(package.read_bytes()).hexdigest() != LOCK['package_sha256']:
        raise RuntimeError('Release ZIP checksum does not match the test lock')
    inputs.mkdir(parents=True, exist_ok=True)
    results.mkdir(parents=True, exist_ok=True)
    (inputs / 'DESKPORT_VM_TEST_ONLY').write_text('Disposable VM inputs only.\n')
    shutil.copy2(package, inputs / 'DeskPort.zip')
    shutil.copy2(ROOT / 'tests/test-macos-vm-guest.sh', inputs / 'test.sh')
    (inputs / 'package.sha256').write_text(LOCK['package_sha256'] + '  DeskPort.zip\n')
    (inputs / 'expected-version.txt').write_text(version + '\n')
    subprocess.run(['xcrun', 'swiftc', '-module-cache-path', str(swift_cache), '-O', '-target', 'arm64-apple-macos26.0',
                    str(ROOT / 'tests/macos-vm-window.swift'), '-o', str(inputs / 'window-check')], check=True)
    for filename in ('STATUS.txt', 'desktop.png'):
        (results / filename).unlink(missing_ok=True)
    runner = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), 'start'])
    ready = False
    try:
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            if runner.poll() is not None:
                raise RuntimeError('VM runner exited before guest became ready')
            try:
                probe = subprocess.run([str(TART), 'exec', NAME, '/usr/bin/true'],
                                       env=ENV, capture_output=True, timeout=15)
                if probe.returncode == 0:
                    ready = True
                    break
            except subprocess.TimeoutExpired:
                pass
            time.sleep(5)
        if not ready:
            raise RuntimeError('Guest Agent did not become ready within 240 seconds')
        # A virtual socket is ready before the GUI login sometimes; allow startup to settle.
        time.sleep(15)
        if runner.poll() is not None:
            raise RuntimeError('Supervised VM stopped before testing')
        tart('exec', NAME, '/bin/bash', '/Volumes/My Shared Files/input/test.sh', timeout=240)
        if (results / 'STATUS.txt').read_text().strip() != 'PASS':
            raise RuntimeError('Guest did not produce a passing result')
    finally:
        if runner.poll() is None:
            try:
                subprocess.run([str(TART), 'stop', NAME], env=ENV, timeout=30)
                runner.wait(timeout=30)
            except subprocess.TimeoutExpired:
                runner.terminate()
                runner.wait(timeout=25)
    print(json.dumps(status(), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['status', 'bootstrap', 'start', 'stop', 'exec', 'prune', 'test'])
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    shared_directories = test_directories() if args.action == 'start' else None
    if args.action == 'status':
        print(json.dumps(status(), indent=2)); return
    if args.action == 'test':
        package_test(args.arguments); return
    if args.action == 'stop':
        tart('stop', NAME)
        # Wait for the supervised runner to release its operation lock.
        with (LAB / 'operation.lock').open('a') as handle:
            for attempt in range(30):
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    return
                except BlockingIOError:
                    time.sleep(1)
        raise RuntimeError('VM stopped but its runner has not released the lock')
    # One storage-mutating operation at a time. Stop/exec/status remain available.
    lockfile = None
    if args.action in ('bootstrap', 'start', 'prune'):
        LAB.mkdir(parents=True, exist_ok=True)
        lockfile = (LAB / 'operation.lock').open('a')
        try:
            fcntl.flock(lockfile, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('Another bootstrap or VM run is active')
    if args.action == 'prune':
        # Allow freeing lab cache even when below the free-space threshold.
        tart('prune', '--entries', 'caches', '--space-budget', '0')
        print(json.dumps(status(), indent=2)); return
    guard()
    if args.action == 'bootstrap':
        tools = LAB / 'tools'; tools.mkdir(exist_ok=True)
        archive = tools / 'tart.tar.gz'
        if not TART.exists():
            subprocess.run(['curl', '-fL', '--retry', '3', LOCK['tart_url'], '-o', str(archive)], check=True)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != LOCK['tart_sha256']:
                raise RuntimeError('Tart checksum mismatch')
            subprocess.run(['tar', '-xzf', str(archive), '-C', str(tools)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(TART.parents[2])], check=True)
        version = subprocess.check_output([str(TART), '--version'], env=ENV, text=True).strip()
        if LOCK['tart_version'] not in version:
            raise RuntimeError('Unexpected Tart version: ' + version)
        if not (LAB / 'tart/vms' / NAME).exists():
            if shutil.disk_usage(LAB).free < MIN_FREE + LOCK['virtual_disk_bytes']:
                raise RuntimeError('Insufficient headroom to download the pinned image')
            supervised(['clone', LOCK['image'], NAME, '--concurrency', '4'])
        tart('set', NAME, '--cpu', '4', '--memory', '8192')
        tart('prune', '--entries', 'caches', '--space-budget', '0')
        guard()
        print(json.dumps(status(), indent=2))
    elif args.action == 'start':
        disk = LAB / 'tart/vms' / NAME / 'disk.img'
        if not disk.is_file() or disk.stat().st_size > LOCK['virtual_disk_bytes']:
            raise RuntimeError('Missing VM disk or disk enlarged beyond the locked 50 GB limit')
        inputs, results, _ = shared_directories
        inputs.mkdir(parents=True, exist_ok=True)
        results.mkdir(parents=True, exist_ok=True)
        # Boot uses Tart's NAT; the guest test disables Ethernet before app launch.
        # No host microphone, clipboard, home directory or credentials exposed.
        graphics = [] if '--graphics' in args.arguments else ['--no-graphics']
        supervised(['run', NAME, *graphics, '--no-audio', '--no-clipboard',
                    '--dir', 'input:' + str(inputs) + ':ro',
                    '--dir', 'results:' + str(results)])
    elif args.action == 'exec':
        if not args.arguments:
            parser.error('exec requires a guest command')
        tart('exec', NAME, *args.arguments)


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(128 + signum))
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
