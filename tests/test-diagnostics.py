#!/usr/bin/env python3
"""Exercise privacy boundaries and verify the actual ZIP with an independent reader."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-diagnostics-') as temporary:
    work = Path(temporary)
    (work / 'test.pro').write_text(f'''QT += core gui widgets testlib
CONFIG += console c++17 testcase
CONFIG -= app_bundle
SOURCES += "{root}/tests/diagnostics-test.cpp" "{root}/app/backend/diagnostics.cpp"
HEADERS += "{root}/app/backend/diagnostics.h"
INCLUDEPATH += "{root}/app/backend" "{root}/moonlight-common-c/moonlight-common-c/src"
TARGET = diagnostics-test
''')
    env = dict(os.environ, TEST_DIAGNOSTICS_ROOT=temporary, TEST_DESKPORT_VERSION=(root / 'app/version.txt').read_text().strip(), QT_QPA_PLATFORM='offscreen')
    env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
    subprocess.run([os.environ.get('DESKPORT_QMAKE', 'qmake'), 'test.pro'], cwd=work, env=env, check=True)
    subprocess.run(['make', '-j4'], cwd=work, env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(work / 'diagnostics-test')], cwd=work, env=env, check=True, timeout=120)
    with zipfile.ZipFile(work / 'verified.zip') as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) <= {'manifest.json', 'README.txt'} | {f'{s}-{n}.jsonl' for s in ('host', 'client', 'display') for n in range(3)} | {f'viewer-{v}-client-{n}.jsonl' for v in range(8) for n in range(3)}
        manifest = json.loads(archive.read('manifest.json'))
        assert manifest['version'] == (root / 'app/version.txt').read_text().strip()
        assert len(archive.read('host-0.jsonl')) <= 1024 * 1024
        all_records = []
        allowed = {
            'source', 'run', 'event', 'elapsed_ms', 'stage', 'tick_ms', 'width', 'height',
            'phase', 'state', 'error', 'duration_ms', 'code', 'kind', 'backend',
            'target_kbps', 'rtt_ms', 'baseline_ms', 'ceiling_kbps', 'floor_kbps', 'resume_kbps',
            'live_encoder', 'quality_floor', 'saver', 'min_qp', 'max_qp', 'quality_strict', 'codec', 'video_format', 'fps',
            'rtt_var_ms', 'received_fps', 'rendered_fps', 'network_drop_permille',
            'pacer_drop_permille', 'received_kbps', 'path',
            'encoded_kbps', 'frames', 'keyframes', 'max_frame_bytes',
        }
        for name in archive.namelist():
            if name.endswith('.jsonl'):
                records = [json.loads(line) for line in archive.read(name).splitlines()]
                assert records
                assert all(set(r) <= allowed for r in records)
                all_records.extend(records)
        assert any(r.get('event') == 'rate' and r.get('target_kbps') == 992
                   and r.get('rtt_ms') == 27 and r.get('quality_floor') == 'off'
                   and r.get('saver') is True and r.get('min_qp') == 22
                   and r.get('max_qp') == 26 for r in all_records)
        for strict, minimum, maximum in ((True, 22, 26), (False, 22, 51)):
            assert any(r.get('event') == 'rate' and r.get('min_qp') == minimum
                       and r.get('max_qp') == maximum and r.get('quality_strict') is strict
                       and not ({'target_kbps', 'rtt_ms', 'quality_floor', 'saver'} & r.keys())
                       for r in all_records)
        assert any(r.get('event') == 'decoder' and r.get('codec') == 'hevc'
                   and r.get('video_format') == 0x100 and r.get('fps') == 60
                   and r.get('width') == 2040 and r.get('height') == 1228 for r in all_records)
    print('Independent ZIP CRC, allowlist, metadata and record validation passed')
