#!/usr/bin/env python3
"""Video pause overlay on both repository-owned upstream revisions.

A paused viewer must not advance the host frame index: the encoders skip
encoding while paused and resume with a key frame. No live session is used.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import re, subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
earlier = ('smart-stream', 'session-settings', 'session-takeover', 'linux-display', 'input-activity',
           'sync-cadence', 'linux-cadence', 'pipewire-memory', 'encoder-policy', 'remote-rate', 'data-saver')

def patch(target, name, *extra):
    subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target), *extra], check=True)

for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-video-pause-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as f:
            f.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in earlier:
            patch(target, name)
        patch(target, 'video-pause')
        before = {p: p.read_text() for p in (target / 'src').glob('*.[ch]*')}
        patch(target, 'video-pause')  # A marked tree is left as it is.
        assert before == {p: p.read_text() for p in (target / 'src').glob('*.[ch]*')}

        video = (target / 'src/video.cpp').read_text()
        assert video.count('#include "rtsp.h"') == 1
        # Both encoder loops skip encoding (and so frame_nr++) while paused,
        # then request a key frame before their first encode after resume.
        assert video.count('if (deskport_admission::video_encode_paused) {') == 2
        assert video.count('deskport_encode_paused = false;\n        session->request_idr_frame();') == 1
        assert video.count('pos->deskport_encode_paused = false;\n            pos->session->request_idr_frame();') == 1
        for loop in ('if (encode(frame_nr++', 'if (encode(ctx->frame_nr++'):
            start = video.index(loop)
            assert video.rfind('deskport_admission::video_encode_paused', 0, start) > video.rfind('\n  }\n', 0, start)
        http = (target / 'src/confighttp.cpp').read_text()
        assert http.index('video_needs_idr = true;') < http.index('video_encode_paused = paused;')

        # The admission block itself compiles and resets the encoder gate.
        rtsp = (target / 'src/rtsp.h').read_text()
        block = re.search(r'namespace deskport_admission \{.*?\n\}\n', rtsp, re.S).group(0)
        probe = target / 'video-pause-probe.cpp'
        probe.write_text('#include <atomic>\n#include <chrono>\n#include <mutex>\n#include <string>\n#include <cassert>\n'
                         + block + 'int main() { deskport_admission::video_encode_paused = true;'
                         ' deskport_admission::reset_video(); assert(!deskport_admission::video_encode_paused); }\n')
        subprocess.run(['c++', '-std=c++20', str(probe), '-o', str(target / 'probe')], check=True)
        subprocess.run([str(target / 'probe')], check=True)

        patch(target, 'video-pause', '--revert')
        assert '#include "rtsp.h"' not in (target / 'src/video.cpp').read_text().split('#include "video.h"')[1][:20]
        print(f'PASS: video pause encoder gate: {archive}')
