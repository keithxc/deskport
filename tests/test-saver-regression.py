#!/usr/bin/env python3
"""Run a seeded desktop-image replay through production macOS encoder code.

No GUI, capture permission, live host, network session or input is used. Outputs
are immutable per run. A failing quality or estimated video-IP budget check
returns nonzero and retains the bitstream, source snapshots and frame metrics.
"""
import argparse
import contextlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


@contextlib.contextmanager
def cleanup_signals():
    """Make SIGTERM/SIGINT unwind the owned phase before releasing its lock."""
    signals = (signal.SIGTERM, signal.SIGINT)
    previous = {number: signal.getsignal(number) for number in signals}

    def interrupt(number, _frame):
        # A repeated interrupt must not interrupt the bounded process cleanup.
        for item in signals:
            signal.signal(item, signal.SIG_IGN)
        raise SystemExit(128 + number)

    try:
        for number in signals:
            signal.signal(number, interrupt)
        yield
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)


def group_exists(group):
    # macOS can return EPERM for killpg(..., 0) after a group becomes defunct.
    # Inspect process state instead; zombies have stopped and own no resources.
    processes = subprocess.check_output(['ps', '-axo', 'pid=,pgid=,stat='], text=True)
    return any(len(parts) == 3 and int(parts[1]) == group and not parts[2].startswith('Z')
               for line in processes.splitlines() if (parts := line.split()))


def terminate_phase(child, grace=10):
    """Stop and verify only this start_new_session process group."""
    for number in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(child.pid, number)
        except ProcessLookupError:
            pass
        except PermissionError:
            if group_exists(child.pid):
                raise
        deadline = time.monotonic() + grace
        while time.monotonic() < deadline:
            child.poll()  # Reap the direct child before checking its group.
            if not group_exists(child.pid):
                child.wait()
                return
            time.sleep(0.05)
    raise RuntimeError(f'Owned phase process group {child.pid} remains after termination')


def run_phase(name, argv, *, logs, env, allow_quality_failure=False, timeout=300, cleanup_grace=10):
    with (logs / (name + '.log')).open('w') as log:
        child = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = child.wait(timeout=timeout)
        except BaseException:
            terminate_phase(child, cleanup_grace)
            raise
    if code and not (allow_quality_failure and code == 1):
        raise RuntimeError(f'{name} failed ({code}); inspect {logs / (name + ".log")}')
    return code


@contextlib.contextmanager
def owned_replay(output):
    import fcntl
    project = Path(os.environ.get('DESKPORT_BUILD_ROOT', Path.home() / 'mygit/build/deskport')).resolve()
    locks = project / 'locks'
    locks.mkdir(parents=True, exist_ok=True)
    with (locks / 'native-replay.lock').open('a+') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('Another resource regression is running on this host')
        lock.seek(0)
        lock.truncate()
        json.dump({'pid': os.getpid(), 'output': str(output)}, lock)
        lock.flush()
        try:
            yield
        finally:
            lock.seek(0)
            lock.truncate()
            fcntl.flock(lock, fcntl.LOCK_UN)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New directory under the project build root')
    parser.add_argument('--ffmpeg-root', type=Path, required=True, help='Retained, checksum-verified host FFmpeg prebuilt')
    parser.add_argument('--seed', type=int, default=20261102)
    parser.add_argument('--decoder', default='ffmpeg')
    parser.add_argument('--width', type=int, default=2040)
    parser.add_argument('--height', type=int, default=1228)
    parser.add_argument('--encoder-kbps', type=int, default=7462)
    parser.add_argument('--ceiling-permille', type=int, default=133)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This regression requires macOS with VideoToolbox')
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        parser.error('Keep replay outputs outside the source checkout')
    project = Path(os.environ.get('DESKPORT_BUILD_ROOT', Path.home() / 'mygit/build/deskport')).resolve()
    if not output.is_relative_to(project) or output == project or output.exists():
        parser.error('Use a new run directory below DESKPORT_BUILD_ROOT (default ~/mygit/build/deskport)')
    if not 0 < args.encoder_kbps <= 200000 or not 0 <= args.ceiling_permille <= 1000:
        parser.error('Invalid encoder budget or ceiling ratio')
    output.mkdir(parents=True)
    logs = output / 'logs'
    logs.mkdir()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(output / 'tmp'))
    Path(env['TMPDIR']).mkdir()

    def run(name, argv, *, allow_quality_failure=False, timeout=300):
        return run_phase(name, argv, logs=logs, env=env, allow_quality_failure=allow_quality_failure, timeout=timeout)

    with owned_replay(output), cleanup_signals():
        assets, prepared = output / 'assets', output / 'input'
        run('assets', [sys.executable, 'tests/generate-saver-scenes.py', '--output', str(assets),
                      '--seed', str(args.seed), '--width', str(args.width), '--height', str(args.height)])
        timeline = assets / 'timeline.json'
        scenario = json.loads(timeline.read_text())
        effective = args.encoder_kbps * args.ceiling_permille // 1000 if args.ceiling_permille else args.encoder_kbps
        if effective <= 0:
            raise ValueError('The selected ceiling must produce a positive encoder budget')
        requested = {'encoderNegotiatedKbps': args.encoder_kbps,
                     'ceilingPermille': args.ceiling_permille, 'bitrateKbps': effective}
        if any(scenario.get(key) != value for key, value in requested.items()):
            scenario.setdefault('provenance', {})['encoderOverride'] = {
                'generated': {key: scenario.get(key) for key in requested}, 'requested': requested}
        scenario.update(requested)
        timeline.write_text(json.dumps(scenario, indent=2) + '\n')
        run('prepare', [sys.executable, 'tests/test-vt-data-saver.py', '--timeline', str(timeline),
                        '--output', str(prepared), '--decoder', args.decoder, '--prepare-only'])
        prefix = output / 'evidence/production'
        prefix.parent.mkdir()
        (prefix.parent / '.keep').touch()
        run('encode', [sys.executable, 'tests/test-host-encoder-replay.py', '--work-dir', str(output / 'compiled'),
                       '--ffmpeg-root', str(args.ffmpeg_root.resolve()), '--width', str(args.width), '--height', str(args.height),
                       '--fps', str(scenario['nominalFps']), '--kbps', str(effective),
                       '--negotiated-kbps', str(args.encoder_kbps), '--ceiling-permille', str(args.ceiling_permille),
                       '--duration-ms', str(scenario['durationMs']), '--scenes-list', str(prepared / 'scenes.list'),
                       '--events', str(prepared / 'events.tsv'), '--output-prefix', str(prefix)],
            timeout=max(180, scenario['durationMs'] / 1000 + 90))
        code = run('quality', [sys.executable, 'tests/test-vt-data-saver.py', '--timeline', str(timeline),
                               '--output', str(prepared), '--decoder', args.decoder,
                               '--analyze-prefix', str(prefix), '--compare-event', '4'], allow_quality_failure=True)
        report = json.loads(prefix.with_suffix('.report.json').read_text())
        print(('PASS' if code == 0 else 'FAIL') + ': ' + str(prefix.with_suffix('.report.json')))
        print(json.dumps({key: report.get(key) for key in ('qualityPass', 'videoIpBudgetPass', 'pass')}, indent=2))
        return code


if __name__ == '__main__':
    raise SystemExit(main())
