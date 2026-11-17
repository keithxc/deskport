#!/usr/bin/env python3
"""Exercise real pinned shutdown/queue code with controlled non-network workers.

The original lifetime negative control uses an independent atomic marker, never
freed storage. Its expected exit 21 is distinct from sanitizer/runtime failures.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def body(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--sanitize', choices=('address', 'thread'))
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'host/vendor/sunshine.json').read_text())
    archive = ROOT / 'host/vendor' / manifest['archive']
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == manifest['sha256']
    overlay = load('shutdown_overlay', ROOT / 'host/overlays/patch-host-macos-shutdown.py')
    prepare = load('prepare_host', ROOT / 'scripts/prepare-host-source.py')
    template = (ROOT / 'tests/host-macos-shutdown.cpp.in').read_text()
    for variant in ('original', 'fixed'):
        source = work / variant
        if variant == 'fixed':
            # The public runner must reach this overlay solely through series.
            prepare.prepare_source(ROOT, source)
            subprocess.run([sys.executable, str(ROOT / 'scripts/apply-host-patches.py'),
                            '--platform', 'macos', str(source)], check=True)
            paths = [source / 'src/stream.cpp', source / 'src/thread_safe.h']
            first = {p: p.read_bytes() for p in paths}
            overlay.apply(source)
            assert first == {p: p.read_bytes() for p in paths}, 'overlay must be idempotent'
            # A late unsupported anchor must not leave earlier files half-patched.
            rejected = work / 'unsupported'
            (rejected / 'src').mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive) as vendor:
                for name in ('src/stream.cpp', 'src/thread_safe.h'):
                    data = vendor.extractfile(name).read()
                    if name.endswith('thread_safe.h'):
                        data = data.replace(b'return _continue && !_queue.empty();', b'return false;')
                    (rejected / name).write_bytes(data)
            before = {p: p.read_bytes() for p in (rejected / 'src').iterdir()}
            try:
                overlay.apply(rejected)
            except SystemExit:
                pass
            else:
                raise AssertionError('unsupported source must be rejected')
            assert before == {p: p.read_bytes() for p in before}, 'rejection must be atomic'
        else:
            (source / 'src').mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive) as vendor:
                for name in ('src/stream.cpp', 'src/thread_safe.h', 'src/utility.h'):
                    (source / name).write_bytes(vendor.extractfile(name).read())
        stream = (source / 'src/stream.cpp').read_text()
        join = body(stream, 'void join(session_t &session)')
        assert 'task_pool.pushDelayed(task, 10s)' in join
        cpp = template.replace('@END_BROADCAST@', body(stream, 'void end_broadcast(broadcast_ctx_t &ctx) {'))
        cpp = cpp.replace('@JOIN@', join).replace('@FIXED@', str(int(variant == 'fixed')))
        cpp = cpp.replace('@PACKET_MUTEXES@', '\n'.join(
            line.strip() for line in stream.splitlines() if line.strip().startswith('static std::mutex deskport_')))
        retire = body(stream, 'void deskport_retire_packets(session_t &session)') if variant == 'fixed' else ''
        if retire:
            # Observation hook only; all locking/removal below is unmodified
            # production code. This orders the controlled active-packet test.
            retire = retire.replace('{', '{\n    if (retire_started) retire_started->set_value();', 1)
        cpp = cpp.replace('@RETIRE_PACKETS@', retire)
        for kind in ('video', 'audio'):
            sender = body(stream, f'void {kind}BroadcastThread(udp::socket &sock)')
            start = sender.index('    for (;;) {' if variant == 'fixed' else '    while (auto packet = packets->pop()) {')
            end = sender.index('      if (shutdown_event->peek())', start)
            cpp = cpp.replace(f'@{kind.upper()}_DEQUEUE@', sender[start:end])
        assert '@' not in cpp
        probe = source / 'probe.cpp'
        probe.write_text(cpp)
        binary = source / 'probe'
        command = [os.environ.get('CXX', '/usr/bin/clang++'), '-std=c++23', '-pthread',
                   '-Wall', '-Wextra', '-Werror', '-g', '-O1', str(probe), '-o', str(binary)]
        if args.sanitize:
            sanitizer = 'address,undefined' if args.sanitize == 'address' else 'thread'
            command += ['-fsanitize=' + sanitizer, '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True)
        for mode, original_result in (('expired', 20), ('shutdown', 21), ('overlap', 21), ('queued', 0), ('gap', 0), ('concurrent', 0)):
            result = subprocess.run([str(binary), mode], timeout=45, capture_output=True, text=True)
            print(result.stdout, end='')
            expected = original_result if variant == 'original' else 0
            original_race = (args.sanitize == 'thread' and variant == 'original' and
                             'WARNING: ThreadSanitizer: data race' in result.stderr)
            if original_race:
                # Darwin can abort before the negative control returns its own
                # failure code. Preserve the actual sanitizer report as evidence.
                assert result.returncode != 0, result.stderr
            else:
                assert result.returncode == expected, (variant, mode, result.returncode, expected, result.stderr)
                assert not result.stderr, result.stderr
            (work / f'{variant}-{mode}.stderr').write_text(result.stderr)
            print(f'PASS: {variant}: {mode} (exit {result.returncode})', flush=True)

        if variant == 'fixed':
            # Remove only per-session retirement: early broadcast-ref release
            # cannot protect a retiring session while another owner survives.
            mutation = cpp.replace('      deskport_retire_packets(session);', '')
            assert mutation != cpp
            mutation = mutation.replace('#if 1', '#if 0')
            probe.write_text(mutation)
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary), 'overlap'], timeout=45,
                                    capture_output=True, text=True)
            assert result.returncode == 21 and not result.stderr, (result.returncode, result.stderr)
            print('PASS: ref-only mutation: overlap (expected exit 21)', flush=True)
            # Keep the generated fixed source/binary reviewable after mutation.
            probe.write_text(cpp)
            subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
