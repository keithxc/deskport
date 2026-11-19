#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Differential check of production desktop policy against fixed Git baselines.

Run with a C++17 compiler (DESKPORT_CXX, CXX, or the platform default). Defaults
are desktop e96192d0 and core 44f22899, never the moving checkout HEAD. Optional
--work selects an external parent for temporary files; otherwise build_storage
chooses the project's test storage. Neither Git worktree is changed.

Each executable is ONE independent translation unit. The historical executable
includes Git-extracted production headers, preserving their relative includes;
the current executable includes the live production headers. No policy is copied
into this test, and the two workspace implementations never share a compiler TU.
The new desktop_workspace.h need only exist when this test is actually run.
"""

# Match test-core.py: establish checked caches/tmp before creating any output.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage

import argparse
from collections import Counter
from dataclasses import dataclass
from itertools import product
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'shared/deskport-core'
DEFAULT_DESKTOP = 'e96192d0'
DEFAULT_CORE = '44f22899'
DEFAULT_SEED = 0xD35C2026
DEFAULT_SAMPLES = 40_000
APP_HEADERS = ('app/backend/streambudget.h', 'app/backend/trafficaccount.h')
CORE_HEADERS = ('include/deskport/workspace.h', 'portable/include/deskport/catalog.h')
CURRENT_WORKSPACE = CORE / 'portable/include/deskport/desktop_workspace.h'

# Each field is emitted even for {0,0,0} or an ineligible nonzero proposal.
# In particular, do not filter invalid proposals before testing later stages.
STAGES = ('base', 'adjust', 'host', 'saver', 'adjust.host', 'saver.adjust',
          'saver.adjust.host', 'adjust.saver', 'adjust.saver.host')

HARNESS = r'''
@HEADERS@
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Read IEEE special/boundary values too; iostream >> double rejects nan/inf
// on some standard libraries. strtod's underflow is intentional for tiny inputs.
static double number() {
    std::string token;
    if (!(std::cin >> token)) throw std::runtime_error("missing double");
    char *end = nullptr;
    double result = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end) throw std::runtime_error("bad double: " + token);
    return result;
}
static DPWorkspace workspace() {
    DPWorkspace result{};
    std::cin >> result.width >> result.height >> result.scale;
    return result;
}
static void emit(const std::string& name, DPWorkspace value) {
    std::cout << ' ' << name << '=' << value.width << ',' << value.height << ',' << value.scale;
}
static void stages(const std::string& prefix, DPWorkspace base, double tuning) {
    const auto adjusted = @POLICY@adjust(base, tuning);
    const auto saver = @POLICY@for_saver(base);
    const auto saver_adjusted = @POLICY@adjust(saver, tuning);
    const auto adjusted_saver = @POLICY@for_saver(adjusted);
    emit(prefix + ".base", base);
    emit(prefix + ".adjust", adjusted);
    emit(prefix + ".host", @POLICY@for_host(base));
    emit(prefix + ".saver", saver);
    emit(prefix + ".adjust.host", @POLICY@for_host(adjusted));
    emit(prefix + ".saver.adjust", saver_adjusted);
    emit(prefix + ".saver.adjust.host", @POLICY@for_host(saver_adjusted));
    emit(prefix + ".adjust.saver", adjusted_saver);
    emit(prefix + ".adjust.saver.host", @POLICY@for_host(adjusted_saver));
}
static void usage(const DeskPortTraffic::DailyUsage& daily) {
    std::cout << " used=" << daily.used << " day=" << (daily.day.empty() ? "-" : daily.day);
}
int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    DeskPortTraffic::DailyUsage daily;
    std::string op;
    long long id = -1;
    try {
        while (std::cin >> id >> op) {
            std::cout << id;
            if (op == "W") {
                const double w = number(), h = number(), density = number(), tuning = number();
                const auto base = workspace();
                stages("pixels", @POLICY@from_pixels(w, h, density), tuning);
                stages("encoded", base, tuning);
            } else if (op == "M") {
                const auto requested = workspace();
                int count, null_pointer, stored;
                std::cin >> count >> null_pointer >> stored;
                if (stored < 0 || stored > 97) throw std::runtime_error("bad stored mode count");
                std::vector<DPWorkspace> modes;
                for (int i = 0; i < stored; ++i) modes.push_back(workspace());
                // An explicit non-null empty list is distinct from no list.
                const DPWorkspace dummy = {640, 360, 1};
                const auto pointer = null_pointer ? nullptr : (modes.empty() ? &dummy : modes.data());
                emit("mode", @POLICY@select_mode(requested, pointer, count));
            } else if (op == "B") {
                int w, h, fps, automatic, saver, manual, yuv444;
                std::cin >> w >> h >> fps >> automatic >> saver >> manual >> yuv444;
                std::cout << " automatic=" << DeskPortStream::automaticBitrate(w, h, fps, yuv444)
                          << " saver=" << DeskPortStream::dataSaverBitrate(w, h)
                          << " session=" << DeskPortStream::sessionBitrate(automatic, saver, manual, w, h, fps, yuv444)
                          << " saver_fps=" << DeskPortStream::kDataSaverFps;
            } else if (op == "A") {
                std::string day, session;
                long long total;
                std::cin >> day >> session >> total;
                std::cout << " delta=" << daily.account(day, session, total);
                usage(daily);
            } else if (op == "R") {
                daily.reset(); usage(daily);
            } else if (op == "F") {
                std::string session;
                std::cin >> session;
                daily.forget(session); usage(daily);
            } else if (op == "D") {
                std::string day;
                std::cin >> day;
                daily.rollover(day); usage(daily);
            } else if (op == "N") {
                daily = DeskPortTraffic::DailyUsage{}; usage(daily);
            } else if (op == "L") {
                long long used, limit;
                std::cin >> used >> limit;
                std::cout << " level=" << static_cast<int>(DeskPortTraffic::level(used, limit));
            } else {
                throw std::runtime_error("unknown operation: " + op);
            }
            if (!std::cin) throw std::runtime_error("truncated input");
            std::cout << '\n';
        }
        if (!std::cin.eof()) throw std::runtime_error("malformed input");
    } catch (const std::exception& error) {
        std::cerr << "case " << id << " op=" << op << ": " << error.what() << '\n';
        return 2;
    }
    return 0;
}
'''


@dataclass(frozen=True)
class Case:
    label: str
    command: str


def tokens(values):
    return ' '.join(repr(value) if isinstance(value, float) else str(value) for value in values)


def workspace_case(label, pixels, tuning=1.0, base=(1920, 1080, 2)):
    return Case(label, 'W ' + tokens((*pixels, tuning, *base)))


def tuning_edges(allowed):
    values = []
    for factor in allowed:
        values.extend((factor, factor - 0.5e-6, factor + 0.5e-6))
        for edge in (factor - 1e-6, factor + 1e-6):
            values.extend((math.nextafter(edge, -math.inf), edge,
                           math.nextafter(edge, math.inf)))
    return values + [0.0, -1.0, 0.49, 0.55, 1.1, 1.6, 2.0, 8.0,
                     math.nan, math.inf, -math.inf]


def workspace_cases(rng, samples, allowed):
    tunings = tuning_edges(allowed)
    densities = (0.5, math.nextafter(0.5, 1), 0.75, 1.0,
                 math.nextafter(1.0, 0), math.nextafter(1.0, 2),
                 1.25, 1.5, 1.75, 2.0, math.nextafter(2.0, 3), 2.5, 3.0, 4.0, 8.0)
    sizes = ((640, 360), (639.999, 359.999), (800, 600), (960, 540),
             (1280, 800), (1920, 1080), (2040, 1228), (2560, 1440),
             (5104, 2720), (7680, 4320), (7680.001, 4320.001),
             (824, 1416), (1080, 1920), (4320, 7680), (1000, 1000),
             (1, 1000000), (1000000, 1), (1e-200, 1e200), (1e200, 1e-200))
    for (w, h), density in product(sizes, densities):
        for tuning in allowed:
            yield workspace_case('pixels-boundary', (w, h, density), tuning)

    # Finite tiny/huge ratios exercise nonzero ineligible proposals as well as
    # normal workspaces. Avoid DBL_MAX/DBL_MIN combinations whose historical
    # intermediate overflow can make the oracle depend on compiler UB.
    bad_values = (0.0, -1.0, math.nan, math.inf, -math.inf)
    for bad in bad_values:
        yield workspace_case('bad-width', (bad, 1080, 1.5))
        yield workspace_case('bad-height', (1920, bad, 1.5))
    for density in (*bad_values, math.nextafter(0.5, 0), math.nextafter(8.0, 9)):
        yield workspace_case('bad-density', (1920, 1080, density))

    bases = [(w, h, scale) for w, h, scale in product(
        (640, 800, 824, 960, 1280, 1920, 7680),
        (360, 600, 748, 1080, 1416, 1920, 4320), (1, 2))]
    bases += [(0, 0, 0), (-1, -1, 1), (639, 360, 1), (640, 359, 1),
              (642, 360, 1), (640, 362, 2), (7684, 4320, 2), (640, 4324, 1),
              (640, 360, 0), (640, 360, 3), (2147483647, -2147483648, 2)]
    for base, tuning in product(bases, tunings):
        yield workspace_case('encoded-boundary-and-tolerance', (1920, 1080, 1.5), tuning, base)

    # These proposals are in the wire box for every density. They are counted
    # again from the actual baseline output, rather than counting attempts as
    # valid samples. Direct encoded inputs independently span the entire box.
    for index in range(samples):
        w = rng.randint(640, 7680) + rng.choice((0.0, 0.25, 0.5, 0.75))
        h = rng.randint(360, 4320) + rng.choice((0.0, 0.25, 0.5, 0.75))
        density = densities[index % len(densities)]
        base = (4 * rng.randint(160, 1920), 4 * rng.randint(90, 1080), rng.randint(1, 2))
        yield workspace_case('random-valid-workspace', (w, h, density), rng.choice(tunings), base)


def mode_case(label, request, modes=(), *, count=None, null=False):
    count = len(modes) if count is None else count
    flat = [value for mode in modes for value in mode]
    return Case(label, 'M ' + tokens((*request, count, int(null), len(modes), *flat)))


def mode_cases(rng):
    landscape, portrait, square = (1920, 1080, 2), (1080, 1920, 2), (1000, 1000, 1)
    ordinary = [(1280, 720, 1), (1920, 1080, 2), (1080, 1920, 1), (2560, 1440, 1)]
    for request in (landscape, portrait, square):
        yield mode_case('no-mode-list', request, null=True)
        yield mode_case('empty-nonnull-list', request)
        yield mode_case('zero-count-ignores-list', request, ordinary, count=0)
        yield mode_case('landscape-portrait-list', request, ordinary)
        yield mode_case('reversed-landscape-portrait', request, list(reversed(ordinary)))
        yield mode_case('single-scale-two-is-normalized', request, [(1280, 720, 2)])
        # Equal scores keep the first advertised mode; equal dimensions with
        # distinct scales also exercise the historical output-scale contract.
        for tied in ([(1280, 720, 2), (1280, 720, 1)],
                     [(800, 1000, 2), (1000, 800, 1)],
                     [(800, 800, 2), (1252, 1252, 1)],
                     [(800, 800, 1), (800, 800, 2)],
                     [(800, 600, 1), (1200, 800, 2)]):
            yield mode_case('mode-tie-or-near-tie', request, tied)
            yield mode_case('mode-tie-reversed', request, list(reversed(tied)))
        for count in (-2147483648, -1, 97, 2147483647):
            yield mode_case('bad-mode-count', request, ordinary, count=count)
            yield mode_case('bad-mode-count-null', request, count=count, null=True)
        yield mode_case('missing-mode-pointer', request, count=1, null=True)
        for count in (1, 2, 95, 96):
            many = [(640, 360, 1)] * count
            many[-1] = request
            yield mode_case('mode-count-boundary', request, many)
            many[-1] = (0, 0, 0)
            if count > 1:
                many[0] = request
            yield mode_case('invalid-mode-count-boundary', request, many)

    invalid = [(0, 0, 0), (-1, -1, 1), (639, 360, 1), (640, 359, 1),
               (642, 360, 1), (640, 362, 1), (7684, 4320, 1), (640, 4324, 1),
               (640, 360, 0), (640, 360, -1), (640, 360, 3)]
    for bad in invalid:
        yield mode_case('invalid-request-empty', bad, null=True)
        yield mode_case('invalid-request-with-list', bad, ordinary)
        for position in range(len(ordinary) + 1):
            modes = ordinary.copy()
            modes.insert(position, bad)
            yield mode_case('invalid-mode-any-position', landscape, modes)

    # log(size) ranking differs from portable workspace.h's general-purpose
    # lexicographic policy. Exercise genuinely varied lists and advertisements.
    for index in range(2000):
        request = (4 * rng.randint(160, 1920), 4 * rng.randint(90, 1080), rng.randint(1, 2))
        count = rng.choice((1, 2, 3, 8, 16, 96))
        modes = [(4 * rng.randint(160, 1920), 4 * rng.randint(90, 1080), rng.randint(1, 2))
                 for _ in range(count)]
        if index % 7 == 0:
            modes[-1] = request
        yield mode_case('random-mode-list', request, modes)


def bitrate_cases(rng):
    # Desktop's 30/60/90/120 fps options, in supported encoded resolution bounds.
    # Products stay far below int64 overflow even after the 4:4:4 multiplier.
    sizes = [(640, 360), (800, 600), (1280, 720), (1280, 800), (1920, 1080),
             (1920, 1200), (2040, 1228), (2560, 1440), (2560, 1600),
             (3440, 1440), (3840, 2160), (5104, 2720), (5120, 2880), (7680, 4320),
             (824, 1416), (1080, 1920), (2160, 3840)]
    manuals = (1, 499, 500, 501, 1124, 1125, 1126, 1999, 2000, 2001,
               3999, 4000, 4001, 4999, 5000, 15000, 60000, 80000, 80001, 150000)
    for (w, h), fps, automatic, saver, manual, yuv444 in product(
            sizes, (30, 60, 90, 120), (0, 1), (0, 1), manuals, (0, 1)):
        yield Case('bitrate-supported-boundary', 'B ' + tokens((w, h, fps, automatic, saver, manual, yuv444)))
    for _ in range(2000):
        values = (4 * rng.randint(160, 1920), 4 * rng.randint(90, 1080),
                  rng.choice((30, 60, 90, 120)), rng.randrange(2), rng.randrange(2),
                  rng.randint(1, 150000), rng.randrange(2))
        yield Case('bitrate-random-supported', 'B ' + tokens(values))


def traffic_cases(rng):
    # Ordered stateful trace: compare delta, used AND day at each transition.
    trace = (
        ('new', 'N'), ('first-sample', 'A 2026-01-01 a 100'),
        ('increase', 'A 2026-01-01 a 250'), ('other-session', 'A 2026-01-01 b 50'),
        ('duplicate', 'A 2026-01-01 a 250'), ('counter-reset', 'A 2026-01-01 a 30'),
        ('negative-ignored', 'A 2026-01-01 a -5'), ('after-negative', 'A 2026-01-01 a 40'),
        ('zero-reset', 'A 2026-01-01 a 0'), ('after-zero', 'A 2026-01-01 a 20'),
        ('forget', 'F b'), ('after-forget', 'A 2026-01-01 b 70'),
        ('midnight-existing-session', 'A 2026-01-02 a 35'),
        ('midnight-other-session', 'A 2026-01-02 b 90'),
        ('explicit-reset', 'R'), ('after-usage-reset', 'A 2026-01-02 a 45'),
        ('same-day-rollover', 'D 2026-01-02'), ('explicit-rollover', 'D 2026-01-03'),
        ('after-rollover', 'A 2026-01-03 a 55'),
        ('negative-at-midnight', 'A 2026-01-04 a -1'),
        ('after-negative-midnight', 'A 2026-01-04 a 65'),
        ('new-session-after-midnight', 'A 2026-01-04 c 80'),
        ('backdated-day', 'A 2026-01-01 a 75'), ('forget-unknown', 'F unknown'),
        ('large-first-sample', 'A 2026-01-01 large 10000000000'),
        ('large-increase', 'A 2026-01-01 large 10000000001'))
    for label, command in trace:
        yield Case('traffic-' + label, command)

    # Reset at the start of each random trace keeps failing sequences replayable.
    # Bounded totals ensure both accounting and percentage products are defined.
    for _ in range(20):
        yield Case('traffic-random-new', 'N')
        counters = dict(a=0, b=0, c=0)
        day = 1
        for _ in range(100):
            session = rng.choice(tuple(counters))
            action = rng.randrange(12)
            if action == 0:
                yield Case('traffic-random-reset', 'R')
            elif action == 1:
                yield Case('traffic-random-forget', 'F ' + session)
            else:
                if action == 2:
                    day += 1
                if action == 3:
                    counters[session] = rng.randrange(100)
                elif action != 4:  # unchanged totals are meaningful too
                    counters[session] += rng.randrange(1000000)
                total = -1 if action == 5 else counters[session]
                yield Case('traffic-random-account', f'A day-{day} {session} {total}')

    for limit in (-1, 0, 1, 2, 3, 7, 99, 100, 101, 1000, 10_000_000_000):
        values = {-1, 0, 1, limit - 1, limit, limit + 1}
        if limit > 0:
            for percent in (70, 85, 100):
                edge = (limit * percent + 99) // 100
                values.update((edge - 1, edge, edge + 1))
        for used in sorted(values):
            yield Case('traffic-level-threshold', f'L {used} {limit}')


def git(repo, *arguments):
    return subprocess.check_output(['git', '-C', str(repo), *arguments], text=True).strip()


def extract_headers(repo, revision, paths, destination):
    for relative in paths:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        # Do not read baseline files from either index or worktree. In particular
        # workspace.h's ../../portable/include/deskport/catalog.h must still work.
        target.write_bytes(subprocess.check_output(
            ['git', '-C', str(repo), 'show', f'{revision}:{relative}']))


def compile_runner(work, name, app_root, core_root, workspace_header, policy, compiler):
    headers = '\n'.join('#include ' + json.dumps(str(path)) for path in (
        *(app_root / relative for relative in APP_HEADERS), workspace_header))
    source = work / (name + '.cpp')
    source.write_text(HARNESS.replace('@HEADERS@', headers).replace('@POLICY@', policy))
    binary = work / name
    command = [*compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-pedantic',
               '-fno-fast-math', '-I' + str(core_root / 'portable/include'),
               str(source), '-o', str(binary)]
    subprocess.run(command, cwd=work, check=True, timeout=180)
    return binary


def run_runner(binary, inputs, output):
    with inputs.open('r') as source, output.open('w') as result:
        subprocess.run([str(binary)], stdin=source, stdout=result, cwd=binary.parent,
                       check=True, timeout=120)


def fields(line, index, expected_keys, side):
    parts = line.split()
    if not parts or parts[0] != str(index):
        raise AssertionError(f'{side}: missing/misordered output for case {index}: {line!r}')
    result = {}
    for item in parts[1:]:
        key, separator, value = item.partition('=')
        if not separator or key in result:
            raise AssertionError(f'{side}: malformed/duplicate field {item!r}, case {index}')
        result[key] = value
    if set(result) != set(expected_keys):
        raise AssertionError(f'{side}: case {index} fields {sorted(result)} != {sorted(expected_keys)}')
    return result


def expected_fields(case):
    op = case.command.split()[0]
    if op == 'W':
        return [prefix + '.' + stage for prefix in ('pixels', 'encoded') for stage in STAGES]
    return {'M': ['mode'], 'B': ['automatic', 'saver', 'session', 'saver_fps'],
            'A': ['delta', 'used', 'day'], 'R': ['used', 'day'],
            'F': ['used', 'day'], 'D': ['used', 'day'], 'N': ['used', 'day'],
            'L': ['level']}[op]


def wire_valid(value):
    w, h, scale = map(int, value.split(','))
    return 640 <= w <= 7680 and 360 <= h <= 4320 and w % 4 == 0 and h % 4 == 0 and scale in (1, 2)


def compare(cases, baseline_output, current_output, seed, samples):
    valid_random = 0
    assertions = 0
    # Keep recent traffic transitions in the failure report: one command alone
    # cannot explain a stateful accounting difference. The full input is retained
    # in the temporary run until this comparison completes.
    recent_traffic = []
    with baseline_output.open() as old, current_output.open() as new:
        for index, case in enumerate(cases):
            keys = expected_fields(case)
            baseline = fields(old.readline(), index, keys, 'baseline')
            current = fields(new.readline(), index, keys, 'current')
            if case.command[0] in 'ARNFD':
                if case.command == 'N':
                    recent_traffic = []
                recent_traffic.append(f'{index} {case.command}')
            for key in keys:
                assertions += 1
                if baseline[key] != current[key]:
                    history = '\ntraffic trace:\n' + '\n'.join(recent_traffic) if case.command[0] in 'ARNFD' else ''
                    raise AssertionError(
                        f'seed={seed:#x}, case={index}, label={case.label}, stage={key}\n'
                        f'input: {case.command}\nbaseline: {baseline[key]}\ncurrent:  {current[key]}{history}')
            if case.label == 'random-valid-workspace' and wire_valid(baseline['pixels.base']):
                valid_random += 1
        if old.read().strip() or new.read().strip():
            raise AssertionError('Unexpected extra runner output')
    if valid_random != samples:
        raise AssertionError(f'Only {valid_random}/{samples} random proposals were valid; do not silently skip samples')
    return valid_random, assertions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--desktop-baseline', default=DEFAULT_DESKTOP, help='Local desktop Git commit')
    parser.add_argument('--core-baseline', default=DEFAULT_CORE, help='Local core Git commit')
    parser.add_argument('--seed', type=lambda value: int(value, 0), default=DEFAULT_SEED)
    parser.add_argument('--samples', type=int, default=DEFAULT_SAMPLES, help='Valid random workspace samples (default: 40000)')
    parser.add_argument('--work', type=Path, help='Checked external parent for temporary files')
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('--samples must be positive')
    storage = _build_storage.bootstrap()
    parent = storage.validate(args.work if args.work is not None else storage.paths['tests'])
    if not CURRENT_WORKSPACE.is_file():
        raise SystemExit(f'Missing production header: {CURRENT_WORKSPACE}; finish the desktop portable API before running this test')
    desktop_revision = git(ROOT, 'rev-parse', '--verify', args.desktop_baseline + '^{commit}')
    core_revision = git(CORE, 'rev-parse', '--verify', args.core_baseline + '^{commit}')
    compiler = shlex.split(os.environ.get('DESKPORT_CXX') or os.environ.get('CXX') or
                           ('/usr/bin/clang++' if sys.platform == 'darwin' else 'c++'))
    if not compiler:
        parser.error('Empty C++ compiler command')
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='desktop-portable-parity-', dir=parent) as temporary:
        work = Path(temporary)
        old_app, old_core = work / 'baseline/desktop', work / 'baseline/core'
        extract_headers(ROOT, desktop_revision, APP_HEADERS, old_app)
        extract_headers(CORE, core_revision, CORE_HEADERS, old_core)
        catalog = (old_core / CORE_HEADERS[1]).read_text()
        match = re.search(r'dp_catalog_tuning_values\[\]\s*=\s*\{([^}]+)\}', catalog)
        if not match:
            raise AssertionError('Cannot read tuning factors from baseline production catalog')
        allowed = [float(value.strip()) for value in match[1].split(',') if value.strip()]
        rng = random.Random(args.seed)
        cases = list(workspace_cases(rng, args.samples, allowed))
        cases.extend(mode_cases(rng))
        cases.extend(bitrate_cases(rng))
        cases.extend(traffic_cases(rng))
        inputs = work / 'inputs.txt'
        inputs.write_text(''.join(f'{index} {case.command}\n' for index, case in enumerate(cases)))
        print(f'Baselines: desktop={desktop_revision} core={core_revision}; seed={args.seed:#x}; cases={len(cases)}', flush=True)
        baseline = compile_runner(work, 'baseline-runner', old_app, old_core,
                                  old_core / CORE_HEADERS[0], 'dp_workspace_', compiler)
        current = compile_runner(work, 'current-runner', ROOT, CORE,
                                 CURRENT_WORKSPACE, 'dp_desktop_workspace_', compiler)
        old_output, new_output = work / 'baseline.txt', work / 'current.txt'
        run_runner(baseline, inputs, old_output)
        run_runner(current, inputs, new_output)
        valid_random, assertions = compare(cases, old_output, new_output, args.seed, args.samples)
    print('PASS: desktop portable production parity ' + json.dumps({
        'desktopBaseline': desktop_revision, 'coreBaseline': core_revision,
        'seed': args.seed, 'validRandomWorkspaces': valid_random,
        'cases': len(cases), 'fieldComparisons': assertions,
        'operations': dict(Counter(case.command.split()[0] for case in cases)),
        'acceptance': 'header policy parity only',
    }, sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit('FAIL: desktop portable parity: ' + str(error))
