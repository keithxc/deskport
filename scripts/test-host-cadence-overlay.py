#!/usr/bin/env python3
"""Exercise both repository-owned upstream revisions without a live session."""
from pathlib import Path
import subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-cadence-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as f:
            f.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display', 'input-activity', 'sync-cadence', 'linux-cadence', 'pipewire-memory', 'encoder-policy'):
            subprocess.run(['python3', str(root / f'scripts/patch-host-{name}.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'scripts/patch-host-encoder-policy.py'), str(target)], check=True)
        video = (target / 'src/video.cpp').read_text()
        assert 'const bool smart = config.deskport_smart;' in video
        assert 'ctx->config.deskport_smart' in video
        assert 'deskport_cadence.emplace(ctx.config.framerate,' in video
        assert 'pos->deskport_timestamp.reset()' in video
        # Compile the actual upstream mailbox template: the older bool event
        # cannot instantiate timed pop(), while optional<int> works on both.
        mailbox = target / 'cadence-mailbox.cpp'
        mailbox.write_text('#include "src/thread_safe.h"\n#include <cassert>\n'
            'int main() { safe::event_t<int> event; '
            'assert(!event.pop(std::chrono::milliseconds(0))); '
            'event.raise(1); assert(event.pop(std::chrono::milliseconds(0))); '
            'assert(!event.pop(std::chrono::milliseconds(0))); }\n')
        subprocess.run(['c++', '-std=c++23', '-pthread', str(mailbox), '-o', str(target / 'mailbox-test')], check=True)
        subprocess.run([str(target / 'mailbox-test')], check=True)
        print(f'PASS: async/sync/input/PipeWire overlays: {archive}')
