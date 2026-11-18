#!/usr/bin/env python3
"""Run the shutdown contracts with libc++ jthread declarations disabled.

This is a feature-absence simulation, not acceptance of an older Apple SDK.
The negative compile control must fail before any contract results are trusted.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--sanitize', choices=('address', 'thread'))
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CXX', '/usr/bin/clang++')
    # Skip only libc++'s jthread header. Keep std::thread, barriers, futures and
    # all extracted production code intact. The controls detect SDK guard drift.
    flags = ['-std=c++23', '-pthread', '-D_LIBCPP___THREAD_JTHREAD_H']
    unavailable = work / 'jthread-unavailable.cpp'
    unavailable.write_text('#include <thread>\nint main() { std::jthread t([] {}); }\n')
    result = subprocess.run([compiler, *flags, '-fsyntax-only', str(unavailable)],
                            capture_output=True, text=True)
    (work / 'jthread-unavailable.stderr').write_text(result.stderr)
    assert result.returncode != 0 and "no member named 'jthread'" in result.stderr, result.stderr
    print('PASS: missing-jthread negative compile control', flush=True)

    template = (ROOT / 'tests/host-macos-shutdown.cpp.in').read_text()
    owner = template[template.index('namespace fixture {'):
                     template.index('} // namespace fixture') + len('} // namespace fixture')]
    probe = work / 'owner-check.cpp'
    probe.write_text('#include <thread>\n#include <utility>\n#include <atomic>\n#include <cassert>\n' + owner + '''
int main() {
  std::atomic<bool> destroyed = false, replaced = false, moved = false;
  { fixture::Thread t([&] { destroyed = true; }); }
  assert(destroyed);
  {
    fixture::Thread first([&] { replaced = true; });
    fixture::Thread second([&] { moved = true; });
    first = std::move(second);
    assert(replaced);
    fixture::Thread third(std::move(first));
    third.join();
    assert(moved);
  }
}
''')
    binary = work / 'owner-check'
    subprocess.run([compiler, *flags, '-Wall', '-Wextra', '-Werror', str(probe),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=45)
    print('PASS: thread owner destruction, move assignment, move construction and explicit join', flush=True)

    wrapper = work / 'clang-no-jthread'
    wrapper.write_text(f'#!{sys.executable}\nimport os, sys\n'
                       f'os.execvp({compiler!r}, [{compiler!r}, {flags[-1]!r}, *sys.argv[1:]])\n')
    wrapper.chmod(0o755)
    command = [sys.executable, str(ROOT / 'tests/test-host-macos-shutdown.py'),
               '--work-dir', str(work / 'contracts')]
    if args.sanitize:
        command += ['--sanitize', args.sanitize]
    (work / 'simulation.json').write_text(json.dumps({
        'compiler': compiler, 'disabled_header_guard': flags[-1],
        'command': command, 'sanitize': args.sanitize,
    }, indent=2) + '\n')
    subprocess.run(command, env={**os.environ, 'CXX': str(wrapper)}, check=True)


if __name__ == '__main__':
    main()
