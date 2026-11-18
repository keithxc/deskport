#!/usr/bin/env python3
"""Decode a multi-reference stream before/after the actual client SPS fixup.

Optional argument: an Annex B H.264 synthetic fixture. Without one, generate a
portable x264 fixture. No screen capture or personal-host connection is used.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import os
from pathlib import Path
import re
import subprocess as sp
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
lib = root / 'h264bitstream/h264bitstream'

def run(args, **kw):
    return sp.run(list(map(str, args)), check=True, capture_output=True, **kw)

with tempfile.TemporaryDirectory(prefix='deskport-sps-') as directory:
    work = Path(directory)
    objects = []
    for source in ['h264_stream.c', 'h264_nal.c', 'h264_sei.c']:
        obj = work / (source + '.o')
        run([os.environ.get('CC', 'cc'), '-w', '-I', lib, '-c', lib/source, '-o', obj])
        objects.append(obj)
    exe = work/'rewrite'
    run([os.environ.get('CXX', 'c++'), '-std=c++17', '-I', lib,
         '-I', root/'app/streaming/video', root/'tests/video/h264-sps.cpp',
         *objects, '-o', exe])
    run([exe], input=b'')
    original = work/'original.h264'
    if len(sys.argv) > 1:
        original.write_bytes(Path(sys.argv[1]).read_bytes())
    else:
        run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
             'testsrc2=size=640x360:rate=60', '-filter_threads', '1', '-frames:v', '600',
             '-c:v', 'libx264', '-threads', '2', '-refs', '4', '-bf', '0', '-g', '250',
             '-pix_fmt', 'yuv420p', original])
    data = original.read_bytes()
    starts = list(re.finditer(b'\x00\x00(?:\x00)?\x01', data))
    assert starts and starts[0].start() == 0
    for mode in ['legacy', 'fixed']:
        chunks = []
        for i, start in enumerate(starts):
            end = starts[i+1].start() if i+1 < len(starts) else len(data)
            nal = data[start.end():end]
            if nal[0] & 31 == 7:
                nal = run([exe, mode], input=nal).stdout
            chunks.extend([start.group(), nal])
        (work/(mode+'.h264')).write_bytes(b''.join(chunks))
    results = {}
    for mode in ['original', 'legacy', 'fixed']:
        result = sp.run(['ffmpeg', '-v', 'warning', '-threads', '2', '-i', str(work/(mode+'.h264')),
                         '-threads', '2', '-f', 'framemd5', '-'], capture_output=True, check=mode != 'legacy')
        hashes = [line for line in result.stdout.splitlines() if not line.startswith(b'#')]
        errors = result.stderr.count(b'exceeds max') + result.stderr.count(b'decode_slice_header error')
        results[mode] = (hashes, errors)
        print(f'{mode}: {len(hashes)} decoded frames, {errors} reference/slice errors')
    assert results['original'][1] == 0
    assert results['legacy'][1] > 0 or results['legacy'][0] != results['original'][0], \
        'Fixture must reproduce reference errors or pixel corruption'
    assert results['fixed'][1] == 0
    assert results['fixed'][0] == results['original'][0], 'Decoded pixels changed'
    print('Legacy decoded output differs:', results['legacy'][0] != results['original'][0])
    print('PASS: reference and reorder declarations preserved; fixed frames match original exactly')
