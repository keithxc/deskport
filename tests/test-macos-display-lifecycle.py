#!/usr/bin/env python3
"""Opt-in native extended-display acceptance; never starts the installed host.

Requires an idle, explicitly available GUI session. Physical displays are only
observed. UUIDs and process paths in the output are private acceptance evidence.
"""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import time
from urllib.error import URLError
from urllib.request import urlopen
from xml.etree import ElementTree


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('helper', type=Path)
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--thread-id', default=os.environ.get('CODEX_THREAD_ID'))
    args = parser.parse_args()
    if not args.native or not args.thread_id:
        parser.error('--native and a thread ID are required')
    root = Path(__file__).resolve().parents[1]
    build_root = Path(os.environ.get('DESKPORT_BUILD_ROOT', Path.home() / 'mygit/build/deskport')).resolve()
    output = args.output.resolve()
    if not output.is_relative_to(build_root) or output.exists():
        parser.error('--output must be a new directory under the DeskPort build root')
    if not args.helper.resolve().is_relative_to(build_root):
        parser.error('use a separate candidate helper under the DeskPort build root')
    lock = build_root / 'locks/local-ui.lock'
    lock.parent.mkdir(parents=True, exist_ok=True)
    lock.mkdir()  # Atomic reservation; never adopt or remove another owner's lock.
    owner = dict(thread=args.thread_id, pid=os.getpid(), started=time.time(), output=str(output))
    (lock / 'owner.json').write_text(json.dumps(owner, indent=2) + '\n')
    children = []
    baseline = None
    evidence = dict(owner=owner, cases=[], passed=False)
    try:
        output.mkdir(parents=True)
        output.chmod(0o700)
        # A native opt-in is not permission to disturb an active deployed stream.
        for port in range(48989, 50890, 100):
            try:
                with urlopen(f'http://127.0.0.1:{port}/serverinfo', timeout=.3) as response:
                    server = ElementTree.fromstring(response.read(65536))
                if server.findtext('state') == 'SUNSHINE_SERVER_BUSY':
                    raise RuntimeError('Deployed DeskPort has an active session; end it before native acceptance')
            except (URLError, TimeoutError, ElementTree.ParseError):
                continue
        probe = output / 'display-probe'
        subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-fblocks', str(root / 'tests/macos-display-probe.m'),
                        '-framework', 'Foundation', '-framework', 'ApplicationServices',
                        '-framework', 'AppKit', '-o', str(probe)], check=True)

        def topology():
            return sorted(json.loads(subprocess.check_output([str(probe)], text=True)), key=lambda d: d['uuid'])

        baseline = topology()
        evidence['baseline'] = baseline
        (output / 'baseline.json').write_text(json.dumps(baseline, indent=2) + '\n')
        evidence['processesBefore'] = subprocess.check_output(['ps', '-axo', 'pid,ppid,etime,command'], text=True)

        def restored():
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if topology() == baseline:
                    return
                time.sleep(.1)
            raise AssertionError('Physical topology or an owned virtual display did not return to baseline')

        for index, ending in enumerate(('release', 'eof', 'sigterm', 'sigkill')):
            state = output / ending
            state.mkdir()
            env = dict(os.environ, DESKPORT_DISPLAY_ISOLATED='1', DESKPORT_DISPLAY_STATE_DIR=str(state),
                       DESKPORT_DISPLAY_SERIAL=str(2147483901 + index))
            # Never let an inherited environment enable primary/mirror policy.
            env.pop('DESKPORT_DISPLAY_REAL_TOPOLOGY', None)
            with (state / 'helper.log').open('wb') as log:
                child = subprocess.Popen([str(args.helper.resolve()), '2560', '1440'], env=env,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
                children.append(child)
                pending = b''
                sequence = 0
                responses = []

                def receive(seq=None):
                    nonlocal pending
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if b'\n' in pending:
                            line, pending = pending.split(b'\n', 1)
                            result = json.loads(line)
                            if (seq is None and 'ready' in result) or result.get('seq') == seq:
                                assert 'error' not in result, result
                                responses.append(result)
                                return result
                        elif select.select([child.stdout], [], [], .2)[0]:
                            data = os.read(child.stdout.fileno(), 65536)
                            if not data:
                                raise RuntimeError('helper exited before acknowledgment')
                            pending += data
                    raise TimeoutError('helper acknowledgment')

                def send(width, height, scale, active, wait=True):
                    nonlocal sequence
                    sequence += 1
                    request = dict(seq=sequence, width=width, height=height, scale=scale,
                                   session=active, displayPolicy=2)
                    child.stdin.write(json.dumps(request).encode() + b'\n')
                    child.stdin.flush()
                    return receive(sequence) if wait else None

                assert receive() == dict(ready=True, active=False)
                restored()  # Starting the helper must not create a display.
                for width, height, scale in ((2560, 1440, 2), (1920, 1200, 1), (1440, 2400, 2)):
                    result = send(width, height, scale, True)
                    assert (result['width'], result['height'], result['scale']) == (width, height, scale), result
                    current = topology()
                    assert len(current) == len(baseline) + 1
                    assert [d for d in current if d['uuid'] in {b['uuid'] for b in baseline}] == baseline
                # Replacement admission races the old lease's queued idle restore.
                send(2560, 1440, 1, False, wait=False)
                result = send(1920, 1888, 2, True)
                assert (result['width'], result['height'], result['scale']) == (1920, 1888, 2), result
                time.sleep(.8)
                assert len(topology()) == len(baseline) + 1, 'stale idle callback removed the new workspace'
                if ending == 'release':
                    result = send(2560, 1440, 1, False)
                    assert result['active'] is False and result['width'] == result['height'] == 0
                    restored()
                    child.stdin.close()
                elif ending == 'eof':
                    child.stdin.close()
                else:
                    child.send_signal(signal.SIGTERM if ending == 'sigterm' else signal.SIGKILL)
                child.wait(timeout=15)
                assert child.returncode == (-signal.SIGKILL if ending == 'sigkill' else 0), child.returncode
                restored()
                assert not (state / 'display-recovery.json').exists(), 'isolated helper wrote a physical journal'
                evidence['cases'].append(dict(ending=ending, pid=child.pid, responses=responses,
                                              returncode=child.returncode, baselineRestored=True))
                print(f'PASS {ending}: resize, queued lease replacement, exact topology restored', flush=True)
        evidence['passed'] = True
    except BaseException as error:
        evidence['failure'] = repr(error)
        raise
    finally:
        # Close/terminate only children launched here; never signal a deployed helper.
        for child in children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
            if child.stdin and not child.stdin.closed:
                child.stdin.close()
            if child.stdout:
                child.stdout.close()
        evidence['childrenExited'] = all(child.poll() is not None for child in children)
        try:
            if baseline is not None:
                evidence['finalTopology'] = topology()
                evidence['baselineRestored'] = evidence['finalTopology'] == baseline
            output.mkdir(parents=True, exist_ok=True)
            (output / 'summary.json').write_text(json.dumps(evidence, indent=2) + '\n')
        finally:
            assert json.loads((lock / 'owner.json').read_text()) == owner
            (lock / 'owner.json').unlink()
            lock.rmdir()
    assert evidence['childrenExited'] and evidence['baselineRestored']


if __name__ == '__main__':
    main()
