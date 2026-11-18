#!/usr/bin/env python3
"""Headless synthetic macOS hardware probe; never captures screens or sends input.

Pass the already checksum-verified pinned host FFmpeg root. This runner does not
fetch dependencies, package, sign, install or interact with a deployed host.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ffmpeg-root', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
build = Path(subprocess.check_output(['bash', str(repo / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()) / 'live-bitrate'
evidence = build / 'evidence'
evidence.mkdir(parents=True, exist_ok=True)
(evidence / '.keep').touch()
ffmpeg = args.ffmpeg_root.resolve()
# Preserve the pinned host toolchain. No system-media library substitution.
for file in ('lib/libavcodec.a', 'lib/libavutil.a', 'lib/libcbs.a', 'include/libavcodec/avcodec.h'):
    if not (ffmpeg / file).is_file():
        parser.error(f'Missing pinned FFmpeg input: {ffmpeg / file}')
frameworks = ['VideoToolbox', 'CoreFoundation', 'CoreMedia', 'CoreVideo', 'CoreServices', 'AudioToolbox', 'Security']
common = ['/usr/bin/clang++', '-std=c++17', '-O2', '-I', str(repo), '-I', str(ffmpeg / 'include')]
framework_args = [arg for name in frameworks for arg in ('-framework', name)]
transaction = build / 'vt-transaction'
subprocess.run(common + [str(repo / 'tests/vt-rate-transaction.cpp'), '-framework', 'VideoToolbox',
                        '-framework', 'CoreFoundation', '-o', str(transaction)], check=True)
subprocess.run([str(transaction)], check=True)
binary = build / 'vt-live-rate'
subprocess.run(common + [str(repo / 'tests/vt-live-rate.cpp'), '-L', str(ffmpeg / 'lib'),
                        '-lavcodec', '-lcbs', '-lavutil', '-lx264', '-lx265', '-lSvtAv1Enc'] +
               framework_args + ['-liconv', '-o', str(binary)], check=True)
results = []
for codec, extension in [('h264_videotoolbox', 'h264'), ('hevc_videotoolbox', 'hevc')]:
    log = evidence / f'vt-{extension}.log'
    prefix = evidence / extension
    with log.open('w') as output:
        subprocess.run([str(binary), codec, 'noise'], env={**os.environ, 'DP_RATE_OUTPUT': str(prefix)},
                       stdout=output, stderr=subprocess.STDOUT, check=True)
    text = log.read_text()
    match = re.search(r'live packets=(\d+).*low_mbps=([0-9.]+).*recovered_mbps=([0-9.]+).*applied=1 hardware=1', text)
    if not match:
        raise SystemExit(f'Missing applied hardware result: {log}')
    packets, low, recovered = int(match[1]), float(match[2]), float(match[3])
    assert low < 4.0 and recovered > low * 2, text
    for label in ('baseline', 'live'):
        stream = Path(f'{prefix}.{label}.{extension}')
        decoded = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0',
            '-show_entries', 'stream=codec_name,width,height,nb_read_frames', '-of', 'json', str(stream)], text=True))
        (evidence / f'{extension}-{label}-decode.json').write_text(json.dumps(decoded, indent=2) + '\n')
        info = decoded['streams'][0]
        assert (info['width'], info['height']) == (2560, 1440)
        assert int(info['nb_read_frames']) == (packets if label == 'live' else 540)
        with (evidence / f'{extension}-{label}-decode.log').open('w') as output:
            subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(stream), '-f', 'null', '-'],
                           stdout=output, stderr=subprocess.STDOUT, check=True)
    results.append({'codec': codec, 'packets': packets, 'low_mbps': low, 'recovered_mbps': recovered,
                    'dropped': 540 - packets, 'independent_decode': 'passed'})
(evidence / 'vt-summary.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
