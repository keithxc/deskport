#!/usr/bin/env python3
"""Read-only CPU intervals for explicit PIDs, or summarize resize stage logs.
No host discovery, input injection, screen capture, or configuration changes.
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
import re
import statistics
import subprocess
import time
from pathlib import Path


def cpu_seconds(value):
    days, _, clock = value.rpartition('-')
    fields = list(map(float, clock.split(':')))
    return (float(days) * 86400 if days else 0) + sum(v * 60 ** i for i, v in enumerate(reversed(fields)))


def snapshot(pids):
    out = subprocess.check_output(['ps', '-p', ','.join(map(str, pids)), '-o', 'pid=,time=,rss='], text=True)
    return {int(p): (cpu_seconds(c), int(r)) for p, c, r in (line.split() for line in out.splitlines())}


def summarize_log(path):
    # SDL ticks are process-local. A restart/wrap invalidates an unfinished trace.
    pattern = re.compile(r'DeskPort resize stage=(\S+) tick_ms=(\d+) width=(\d+) height=(\d+)')
    observed = None
    trace = None
    complete, incomplete = [], 0
    previous = 0
    for line in path.read_text(errors='replace').splitlines():
        m = pattern.search(line)
        if not m:
            continue
        stage, tick, w, h = m.groups()
        tick, size = int(tick), (int(w), int(h))
        if tick < previous:
            incomplete += int(trace is not None)
            observed = trace = None
        previous = tick
        if stage == 'observed':
            observed = tick
        elif stage == 'stop-begin':
            incomplete += int(trace is not None)
            trace = {'observed': observed if observed is not None else tick, stage: tick, 'size': size}
        elif trace is not None:
            trace[stage] = tick
            if stage == 'mode-ready':
                trace['size'] = size
            if stage == 'first-render-submit' and size == trace['size']:
                pairs = [('settle', 'observed', 'stop-begin'), ('stop', 'stop-begin', 'stop-end'),
                         ('mode', 'mode-request', 'mode-ready'), ('probe', 'probe-begin', 'probe-end'),
                         ('resume', 'resume-request', 'resume-response'),
                         ('to_render_submit', 'observed', stage)]
                complete.append({label + '_ms': trace[b] - trace[a] for label, a, b in pairs if a in trace and b in trace})
                trace = None
    return {'completed': len(complete), 'incomplete': incomplete + int(trace is not None), 'samples': complete,
            'note': 'Render submission is not physical scanout or input readiness; ticks cannot be compared across machines.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, action='append')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--interval', type=float, default=2)
    parser.add_argument('--label', default='unspecified-content')
    parser.add_argument('--log', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.log:
        result = summarize_log(args.log)
    else:
        if not args.pid or args.seconds <= 0 or args.interval <= 0:
            parser.error('CPU sampling requires --pid and positive durations')
        begin = time.monotonic()
        old = snapshot(args.pid)
        old_time = time.monotonic()
        rows = []
        while time.monotonic() - begin < args.seconds:
            time.sleep(min(args.interval, max(0, args.seconds - (time.monotonic() - begin))))
            try:
                current = snapshot(args.pid)
            except subprocess.CalledProcessError:
                current = {}
            now = time.monotonic()
            for pid in args.pid:
                if pid in old and pid in current and current[pid][0] >= old[pid][0]:
                    rows.append({'pid': pid, 'elapsed_s': now - begin, 'interval_s': now - old_time,
                                 'cpu_percent_one_core': 100 * (current[pid][0] - old[pid][0]) / (now - old_time),
                                 'rss_kib': current[pid][1]})
            old, old_time = current, now
        summaries = {}
        for pid in args.pid:
            own = [r for r in rows if r['pid'] == pid]
            summaries[str(pid)] = {'intervals': len(own), 'cpu_mean_one_core':
                sum(r['cpu_percent_one_core'] * r['interval_s'] for r in own) / sum(r['interval_s'] for r in own) if own else None,
                'cpu_peak_one_core': max((r['cpu_percent_one_core'] for r in own), default=None)}
        result = {'label': args.label, 'duration_s': time.monotonic() - begin, 'pids': summaries, 'samples': rows,
                  'note': 'Explicit PID CPU-time deltas, not lifetime ps %CPU; coarse OS accounting at short intervals. Missing PIDs are not zero CPU. RSS is not unique memory. Content not controlled by sampler.'}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'samples'}, indent=2))

if __name__ == '__main__':
    main()
