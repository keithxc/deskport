#!/usr/bin/env python3
"""Opt-in topology regression in the isolated macos-vm.py guest only.
Stop guest sharing first. --helper is a guest path to a candidate display helper.
Never executes the native display helper on the invoking Mac.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import json
import os
from pathlib import Path
import select
import runpy
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--helper', required=True)
args = parser.parse_args()
wrapper = [sys.executable, str(Path(__file__).resolve().parents[1].joinpath('scripts', 'macos-vm.py')), 'exec']
subprocess.run(wrapper + ['/bin/test', '-f', '/Volumes/My Shared Files/input/DESKPORT_VM_TEST_ONLY'], check=True)
# Refuse to compete with the guest's installed sharing service.
active = subprocess.run(wrapper + ['/usr/bin/pgrep', '-f', '/Applications/DeskPort.app/Contents/MacOS/DeskPort'], capture_output=True)
if active.returncode == 0:
    raise SystemExit('Stop sharing in the test VM before running isolated helper tests')
vm = runpy.run_path(str(Path(__file__).resolve().parents[1].joinpath('scripts', 'macos-vm.py')))
process = subprocess.Popen([str(vm['TART']), 'exec', '-i', vm['NAME'], args.helper, '2560', '1440'], env=vm['ENV'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
buffer = b''
def response(sequence):
    global buffer
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if b'\n' in buffer:
            line, buffer = buffer.split(b'\n', 1)
            item = json.loads(line)
            if item.get('seq', 0) == sequence and 'caret' not in item:
                assert 'error' not in item, item
                return item
            continue
        ready, _, _ = select.select([process.stdout], [], [], .2)
        if ready:
            chunk = os.read(process.stdout.fileno(), 8192)
            if not chunk:
                raise AssertionError('Helper exited before completing request')
            buffer += chunk
    raise AssertionError(f'Timed out waiting for sequence {sequence}')
try:
    response(0)
    sequence = 0
    for cycle in range(3):
        for width, height, scale, session in [(1920,2880,2,True), (2880,1920,2,True), (2560,1440,1,False)]:
            sequence += 1
            request = dict(width=width, height=height, scale=scale, session=session, seq=sequence)
            process.stdin.write(json.dumps(request).encode() + b'\n'); process.stdin.flush()
            result = response(sequence)
            assert (result['width'], result['height'], result['scale']) == (width,height,scale), result
            assert bool(result['mirrored']) == session, result
            print(f'PASS cycle={cycle + 1} {width}x{height}@{scale} session={session}', flush=True)
finally:
    process.stdin.close()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.terminate(); process.wait(timeout=5)
