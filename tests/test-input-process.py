#!/usr/bin/env python3
"""Separate macOS desktop-input process: protocol unit test and overlay anchors."""
from pathlib import Path
import os, subprocess, tarfile, tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-input-process-') as tmp:
    binary = Path(tmp) / 'input-process'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    str(root / 'tests/input-process.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    target = Path(tmp) / 'source'
    target.mkdir()
    with tarfile.open(root / 'host/vendor/sunshine.tar.gz') as stream:
        stream.extractall(target, filter='data')
    if not (target / 'src').exists():
        target = next(p for p in target.iterdir() if (p / 'src').exists())
    subprocess.run(['python3', str(root / 'scripts/apply-host-patches.py'), '--platform', 'macos', str(target)],
                   check=True, stdout=subprocess.DEVNULL)
    main = (target / 'src/main.cpp').read_text()
    stream = (target / 'src/stream.cpp').read_text()
    video = (target / 'src/video.cpp').read_text()
    assert main.count('{"deskport-input"sv,') == 1
    assert main.index('auto input_deinit_guard = input::init();') < main.index('deskport::input_process::Host::get().start();')
    # Both authenticated paths forward after activity observation; none bypasses it.
    assert stream.count('deskport::input_process::passthrough(session->input, std::move(plaintext));') == 2
    for start in [i for i in range(len(stream)) if stream.startswith('deskport::input_process::passthrough(', i)]:
        assert stream.rfind('session->deskport_activity.observe(', 0, start) > stream.rfind('input::passthrough(', 0, start)
    assert stream.count('deskport::input_process::Host::get().reset();') == 1
    assert video.count('deskport::input_process::raisePort(') == 2
    assert (target / 'src/deskport/macos/inputprocess-host.h').exists()
    assert (target / 'src/deskport/common/inputprocess.h').exists()
print('PASS: input process protocol and macOS overlay wiring')
