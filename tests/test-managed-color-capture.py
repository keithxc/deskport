#!/usr/bin/env python3
"""Opt-in SCK/P010 capture on an owned synthetic virtual output; no host session."""
import argparse
import datetime
import json
import os
from pathlib import Path
import select
import signal
import subprocess as sp
import time
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native', action='store_true', required=True)
parser.add_argument('--thread', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--wait-lock', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
args.output.mkdir(parents=True, exist_ok=False)
source = Path(sp.check_output(['bash', root/'scripts/build-paths.sh', 'macos-host-source'], text=True).strip())
source = Path(os.environ.get('DESKPORT_HOST_SOURCE_DIR', source))
legacy, binary, helper = (args.output/name for name in ['legacy.o', 'capture', 'display-helper'])
sp.run(['xcrun', 'clang', '-I', source, '-c', source/'src/platform/macos/av_video.m', '-o', legacy], check=True)
sp.run(['xcrun', 'clang', '-fobjc-arc', '-Wall', '-Werror', '-I', root, '-I', source, root/'tests/managed-color-capture.m', root/'host/macos/color-conversion.m', legacy,
        '-framework', 'ScreenCaptureKit', '-framework', 'VideoToolbox', '-framework', 'CoreMedia', '-framework', 'CoreVideo', '-framework', 'Metal',
        '-framework', 'CoreGraphics', '-framework', 'AVFoundation', '-framework', 'Foundation', '-framework', 'QuartzCore', '-framework', 'AppKit', '-o', binary], check=True)
sp.run(['xcrun', 'clang', '-fobjc-arc', root/'host/macos/display-helper.m', '-framework', 'Foundation', '-framework', 'AppKit',
        '-framework', 'CoreGraphics', '-framework', 'ApplicationServices', '-o', helper], check=True)
lock = Path.home()/'mygit/build/deskport/locks/local-ui.lock'
lock.parent.mkdir(parents=True, exist_ok=True)
while True:
    try: lock.mkdir(); break
    except FileExistsError:
        if not args.wait_lock: raise SystemExit('local-ui.lock is occupied')
        time.sleep(1)
owner = f'thread={args.thread}\npid={os.getpid()}\ntime={datetime.datetime.now().astimezone().isoformat()}\ntask=isolated native SCK P010 gradient capture\n'
(lock/'owner').write_text(owner)
print('Acquired local-ui.lock for isolated virtual-output capture', flush=True)
child = None
baseline = None
resources = {'parent_pid': os.getpid(), 'helper_pid': None, 'capture_pids': [],
             'helper_exited': False, 'captures_exited': [], 'topology_restored': False, 'lock_released': False}
def record_resources():
    (args.output/'resources.json').write_text(json.dumps(resources, indent=2)+'\n')
def interrupted(signum, frame):
    raise KeyboardInterrupt(f'Interrupted by signal {signum}')
signal.signal(signal.SIGTERM, interrupted)
record_resources()
try:
    baseline = sorted(json.loads(sp.check_output([binary], text=True)))
    descriptor = args.output/'capture-display'
    env = dict(os.environ, DESKPORT_DISPLAY_ISOLATED='1', DESKPORT_DISPLAY_STATE_DIR=str(args.output), DESKPORT_DISPLAY_SERIAL='2147484291')
    with (args.output/'helper.log').open('w') as log:
        child = sp.Popen([helper, '1280', '800'], stdin=sp.PIPE, stdout=sp.PIPE, stderr=log, text=True, env=env)
        resources['helper_pid'] = child.pid
        record_resources()
        def receive(seq=None):
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if not select.select([child.stdout], [], [], 0.2)[0]: continue
                line = child.stdout.readline()
                if not line: raise RuntimeError('owned display helper exited')
                value = json.loads(line)
                if (seq is None and value.get('ready')) or value.get('seq') == seq:
                    assert 'error' not in value, value
                    return value
            raise TimeoutError('owned display helper response')
        assert receive() == {'ready': True, 'active': False}
        results = []
        for index, (width, height, scale) in enumerate([(1280, 800, 1), (1280, 800, 1), (1920, 1200, 2), (1280, 800, 1)], 1):
            child.stdin.write(json.dumps(dict(seq=index, width=width, height=height, scale=scale, session=True, displayPolicy=2))+'\n'); child.stdin.flush()
            result = receive(index)
            descriptor.write_text(f'{result["displayId"]} {width} {height} {scale}\n')
            capture_env = dict(env, DESKPORT_CAPTURE_DISPLAY_FILE=str(descriptor))
            native = sp.Popen([binary, str(result['displayId']), str(width), str(height)], env=capture_env, stdout=sp.PIPE, stderr=sp.PIPE, text=True)
            resources['capture_pids'].append(native.pid)
            record_resources()
            try:
                stdout, stderr = native.communicate(timeout=20)
            finally:
                if native.poll() is None:
                    native.kill(); native.wait()
                resources['captures_exited'].append(native.pid)
                record_resources()
            (args.output/f'capture-{index}.log').write_text(stderr)
            assert native.returncode == 0, stderr
            frame = json.loads(stdout)
            assert frame['validP010'] and not frame['activeHDR'] and frame['width'] == width and frame['height'] == height and frame['uniqueLumaCodes'] > 256, frame
            results.append(frame); print(json.dumps(frame), flush=True)
        (args.output/'report.json').write_text(json.dumps(results, indent=2)+'\n')
finally:
    try:
        if child:
            child.stdin.close()
            try: child.wait(timeout=15)
            except sp.TimeoutExpired: child.terminate(); child.wait(timeout=5)
            resources['helper_exited'] = child.poll() is not None
            assert resources['helper_exited']
        restored = sorted(json.loads(sp.check_output([binary], text=True)))
        resources['topology_restored'] = baseline is not None and restored == baseline
        assert resources['topology_restored'], 'display membership/mode/layout changed'
        print('Owned helper exited; display topology verified restored', flush=True)
    finally:
        assert (lock/'owner').read_text() == owner
        (lock/'owner').unlink(); lock.rmdir()
        resources['lock_released'] = True
        record_resources()
        print('local-ui.lock released', flush=True)
