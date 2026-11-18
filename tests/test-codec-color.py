#!/usr/bin/env python3
"""Synthetic elementary-stream color/precision qualification, never screen capture.

Software reference codecs are mandatory. Optional --hardware tests actual encoder
and explicit decoder backends; failures are failures, absent backends are skips.
This does not qualify DeskPort capture, renderer output, HDR luminance or latency.
"""
import argparse
import array
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess as sp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--hardware', choices=['videotoolbox', 'vaapi', 'cuda', 'd3d11va'])
parser.add_argument('--device', help='VAAPI render node, required for VAAPI encoding')
parser.add_argument('--encoder-backend', action='append', choices=['videotoolbox', 'vaapi', 'nvenc', 'amf', 'qsv'], help='Optional explicit GPU encoder backend, repeat for a GPU matrix')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
ffmpeg, ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    parser.error('ffmpeg and ffprobe are required')

def run(command):
    return sp.run(list(map(str, command)), capture_output=True, check=True, timeout=60)

native_vt = None
if args.hardware == 'videotoolbox':
    root = Path(__file__).resolve().parents[1]
    native_vt = args.output / 'vt-hevc-decode'
    run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', root,
         root/'tests/video/vt-hevc-decode.c', '-framework', 'VideoToolbox', '-framework', 'CoreMedia',
         '-framework', 'CoreVideo', '-framework', 'CoreFoundation', '-o', native_vt])

inventory = run([ffmpeg, '-hide_banner', '-encoders']).stdout.decode()
report = {'platform': platform.platform(), 'ffmpeg': run([ffmpeg, '-version']).stdout.decode().splitlines()[0],
          'scope': 'synthetic elementary streams; not capture/display/live acceptance', 'results': []}
width, height, frames = 192, 128, 4
cases = [('h264', 8, False, 'libx264'), ('hevc', 8, False, 'libx265'),
         ('hevc', 10, False, 'libx265'), ('hevc', 10, True, 'libx265'),
         ('av1', 8, False, 'libaom-av1'), ('av1', 10, False, 'libaom-av1'), ('av1', 10, True, 'libaom-av1')]
failed = False
for codec, bits, hdr, software in cases:
    # Limited-range luma ramp and six chroma bars. Values span the actual 10-bit
    # code range; low bits are populated before encoding rather than padding SDR8.
    scale = 1 if bits == 10 else 4
    pixels = [(64 + (x * 876 // (width-1))) // scale for y in range(height) for x in range(width)]
    pixels += [(128, 256, 384, 640, 768, 896)[x*6//(width//2)] // scale for y in range(height//2) for x in range(width//2)]
    pixels += [(896, 768, 640, 384, 256, 128)[x*6//(width//2)] // scale for y in range(height//2) for x in range(width//2)]
    raw = array.array('H' if bits == 10 else 'B', pixels)
    if bits == 10 and __import__('sys').byteorder != 'little':
        raw.byteswap()
    original = raw.tobytes() * frames
    name = f'{codec}-{bits}-' + ('pq2020' if hdr else 'sdr709')
    source = args.output / (name + '.yuv')
    source.write_bytes(original)
    pix_fmt = 'yuv420p10le' if bits == 10 else 'yuv420p'
    color = ['-color_range', 'tv', '-color_primaries', 'bt2020' if hdr else 'bt709',
             '-color_trc', 'smpte2084' if hdr else 'bt709', '-colorspace', 'bt2020nc' if hdr else 'bt709']
    encoders = [software]
    backends = args.encoder_backend or ({'videotoolbox': ['videotoolbox'], 'vaapi': ['vaapi'], 'cuda': ['nvenc']}.get(args.hardware, []))
    encoders += [codec + '_' + backend for backend in backends]
    for encoder in encoders:
        entry = {'case': name, 'encoder': encoder}
        report['results'].append(entry)
        if encoder not in inventory:
            entry['status'] = 'skip' if encoder != software else 'fail'
            entry['reason'] = 'encoder not compiled in'
            failed |= encoder == software
            print(json.dumps(entry), flush=True)
            continue
        path = args.output / f'{name}-{encoder}.{codec if codec != "av1" else "obu"}'
        command = [ffmpeg, '-v', 'error', '-threads', '2', '-filter_threads', '1']
        if encoder.endswith('_vaapi'):
            if not args.device:
                parser.error('--device is required for VAAPI encoding')
            command += ['-vaapi_device', args.device]
        command += color + ['-f', 'rawvideo', '-pixel_format', pix_fmt, '-video_size', f'{width}x{height}', '-framerate', '24', '-i', source, '-frames:v', frames, '-an']
        if encoder.endswith('_vaapi'):
            command += ['-vf', 'format=' + ('p010le' if bits == 10 else 'nv12') + ',hwupload']
        command += ['-c:v', encoder, '-threads', '2']
        if encoder == 'libx264': command += ['-preset', 'veryfast', '-qp', '1', '-profile:v', 'high']
        elif encoder == 'libx265': command += ['-x265-params', 'lossless=1:pools=none:frame-threads=1:log-level=error']
        elif encoder == 'libaom-av1': command += ['-cpu-used', '8', '-crf', '0', '-b:v', '0']
        else:
            command += ['-b:v', '8M']
            if encoder.endswith('_videotoolbox'): command += ['-allow_sw', '0']
        command += color + ['-y', path]
        try:
            run(command)
            info = json.loads(run([ffprobe, '-v', 'error', '-show_streams', '-of', 'json', path]).stdout)['streams'][0]
            expected = {'codec_name': codec, 'color_range': 'tv', 'color_space': 'bt2020nc' if hdr else 'bt709',
                        'color_transfer': 'smpte2084' if hdr else 'bt709', 'color_primaries': 'bt2020' if hdr else 'bt709',
                        'pix_fmt': pix_fmt}
            for key, value in expected.items():
                assert info.get(key) == value, (key, info.get(key), value)
            decode = [ffmpeg, '-v', 'error', '-threads', '2', '-i', path, '-threads', '2', '-pix_fmt', pix_fmt, '-f', 'rawvideo', '-']
            decoded = run(decode).stdout
            assert len(decoded) == len(original), 'decoded frame count/size mismatch'
            # Lossless reference is an exact byte comparison, including low bits.
            if encoder == software and codec != 'h264':
                assert decoded == original, 'software reference changed decoded pixels'
            actual = array.array('H' if bits == 10 else 'B', decoded)
            if bits == 10 and __import__('sys').byteorder != 'little': actual.byteswap()
            errors = [abs(a-b) for a, b in zip(pixels * frames, actual)]
            mean_error, maximum_error = sum(errors) / len(errors), max(errors)
            assert mean_error <= (8 if bits == 10 else 2), 'color chart distorted'
            if bits == 10:
                assert sum(value % 4 != 0 for value in actual) > len(actual) // 10, '10-bit precision lost'
            entry.update(status='pass', metadata=expected, bitstream_profile=info.get('profile'), bytes=path.stat().st_size,
                         mean_code_error=mean_error, maximum_code_error=maximum_error,
                         decoded_sha256=hashlib.sha256(decoded).hexdigest(), frames=frames)
            if args.hardware:
                # -hwaccel without auto/software fallback; compare against the
                # software decode of the very same compressed stream.
                hwdecode = [ffmpeg, '-v', 'error', '-threads', '2', '-hwaccel', args.hardware]
                if args.device: hwdecode += ['-hwaccel_device', args.device]
                surface = {'videotoolbox': 'videotoolbox_vld', 'vaapi': 'vaapi', 'cuda': 'cuda', 'd3d11va': 'd3d11'}[args.hardware]
                download = 'p010le' if bits == 10 else 'nv12'
                hwdecode += ['-hwaccel_output_format', surface, '-i', path,
                             '-vf', f'hwdownload,format={download},format={pix_fmt}', '-pix_fmt', pix_fmt, '-f', 'rawvideo', '-']
                hardware_pixels = run(hwdecode).stdout
                assert hardware_pixels == decoded, 'hardware/software decoded pixels differ'
                entry['hardware_decode'] = 'pass; explicit hardware surface matches software bytes'
                if native_vt and codec == 'hevc':
                    # FFmpeg enables HEVC acceleration but permits VT software
                    # fallback. Verify actual hardware and pixels with the same
                    # first compressed image through the production native probe.
                    first = args.output / (path.name + '.first.hevc')
                    native_pixels = args.output / (path.name + '.native.yuv')
                    run([ffmpeg, '-v', 'error', '-i', path, '-frames:v', '1', '-c:v', 'copy', '-f', 'hevc', '-y', first])
                    run([native_vt, first, native_pixels, str(bits)])
                    assert native_pixels.read_bytes() == decoded[:len(original)//frames], 'native hardware pixels differ from software'
                    entry['native_hardware_frames'] = 1
                    entry['hardware_decode'] = 'pass; native VT hardware first-frame pixels and all-frame device-surface pixels match software'
        except (sp.CalledProcessError, sp.TimeoutExpired, AssertionError) as error:
            entry.update(status='fail', reason=(error.stderr.decode(errors='replace')[-3000:] if isinstance(error, sp.CalledProcessError) else str(error)))
            failed = True
        print(json.dumps(entry), flush=True)
(args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
raise SystemExit(1 if failed else 0)
