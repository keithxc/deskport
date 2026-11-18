#!/usr/bin/env python3
"""Linux-only owned process fixtures; no compositor, sessions, SSH or input.

All signal targets are descendants of this test. The negative control is also
an owned child; the test stops it through its pipe, never a numeric group ID.
"""
import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def worker(mode, marker):
    if mode == 'negative':
        os.setsid()
        Path(marker).write_text(str(os.getpid()))
        sys.stdin.read()
        return
    leader = os.getpid()
    if mode == "stubborn":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    if mode in ('natural', 'detached', 'stubborn'):
        descendant = os.fork()
        if descendant == 0:
            if mode == 'detached':
                os.setsid()
            if mode != 'natural':
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
            Path(marker).write_text(json.dumps({'leader': leader, 'descendant': os.getpid()}))
            if mode == 'natural':
                time.sleep(.4)
                os._exit(0)
            while True:
                time.sleep(.1)
        if mode != 'stubborn':
            os._exit(7)
    else:
        Path(marker).write_text(json.dumps({'leader': leader}))
    while True:
        time.sleep(.1)


def ready(path, process):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if path.exists() and path.stat().st_size:
            return json.loads(path.read_text())
        if process.poll() is not None:
            raise AssertionError(f'fixture exited before ready: {process.returncode}')
        time.sleep(.01)
    raise AssertionError('owned fixture failed to become ready')


def run(binary, work):
    assert sys.platform == 'linux'
    # Parent-death helper is adopted here, so this test can reap precisely it.
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
    here = str(Path(__file__).resolve())
    negative = subprocess.Popen([sys.executable, here, '--worker', 'negative', str(work/'negative.pid')],
                                stdin=subprocess.PIPE)
    active = []
    try:
        for mode in ('natural', 'term', 'stubborn', 'detached'):
            marker = work/f'{mode}.json'
            marker.unlink(missing_ok=True)
            process = subprocess.Popen([binary, sys.executable, here, '--worker', mode, str(marker)],
                                       stdin=subprocess.PIPE)
            active.append(process)
            pids = ready(marker, process)
            assert negative.poll() is None
            if mode == 'natural':
                assert process.poll() is None, 'supervisor failed to pin group after leader exit'
                assert process.wait(timeout=3) == 7
            else:
                start = time.monotonic()
                process.stdin.write(b'T'); process.stdin.flush()
                rc = process.wait(timeout=6)
                assert rc == (143 if mode == 'term' else 137 if mode == 'stubborn' else 7), (mode, rc)
                if mode in ('stubborn', 'detached'):
                    assert 2.9 <= time.monotonic() - start < 5
            for pid in pids.values():
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError(f'{mode}: owned descendant not reaped: {pid}')
            assert negative.poll() is None, 'owned negative control was signalled'
            print('PASS native owned supervisor:', mode, flush=True)
        # An intermediary dies without sending T. Its pipe closure triggers the
        # same TERM/KILL cleanup and the helper is adopted by this test runner.
        marker = work/'parent-death.json'; marker.unlink(missing_ok=True)
        launcher = '''import os,subprocess,sys,time
p=subprocess.Popen(sys.argv[1:],stdin=subprocess.PIPE)
print(p.pid,flush=True)
deadline=time.monotonic()+3
while not os.path.exists(sys.argv[-1]) and time.monotonic()<deadline:
 time.sleep(.01)
os._exit(0)
'''
        parent = subprocess.Popen([sys.executable, '-c', launcher, binary, sys.executable, here,
                                   '--worker', 'stubborn', str(marker)], stdout=subprocess.PIPE, text=True)
        helper_pid = int(parent.stdout.readline())
        pids = ready(marker, parent)
        assert parent.wait(timeout=2) == 0
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            pid, status = os.waitpid(helper_pid, os.WNOHANG)
            if pid:
                assert os.waitstatus_to_exitcode(status) == 137
                break
            time.sleep(.01)
        else:
            raise AssertionError('parent-death cleanup did not finish')
        for pid in pids.values():
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError(f'parent death: owned descendant not reaped: {pid}')
        assert negative.poll() is None
        print('PASS native owned supervisor: parent death / EOF', flush=True)
    finally:
        # EOF is a stable capability, even on an assertion failure.
        for process in active:
            if process.stdin: process.stdin.close()
            if process.poll() is None: process.wait(timeout=6)
        if negative.stdin: negative.stdin.close()
        negative.wait(timeout=3)


if __name__ == '__main__':
    if sys.argv[1] == '--worker':
        worker(sys.argv[2], sys.argv[3])
    else:
        run(sys.argv[1], Path(sys.argv[2]))
