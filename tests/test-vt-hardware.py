#!/usr/bin/env python3
"""Use the production hardware-only HEVC probe with the client's public samples."""
import argparse
from pathlib import Path
import re
import subprocess as sp
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require-main', action='store_true', help='Require actual Main/Main10 hardware support on this machine')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = Path(sp.check_output(['bash', root/'scripts/build-paths.sh', 'checkout'], text=True).strip())/'tests/vt-hardware'
work.mkdir(parents=True, exist_ok=True)
source = (root/'app/streaming/video/ffmpeg_videosamples.cpp').read_text()
with tempfile.TemporaryDirectory(dir=work) as temp:
    path = Path(temp)
    definitions = []
    checks = ['assert(!dp_vt_hevc_hardware_probe(NULL, 0));', 'uint8_t invalid[8] = {0,0,0,1,0,0,0,0}; assert(!dp_vt_hevc_hardware_probe(invalid, sizeof(invalid)));']
    for name in ['HEVCMain', 'HEVCMain10', 'HEVCRExt8_444', 'HEVCRExt10_444']:
        body = re.search(r'k_'+name+r'TestFrame\[\] = \{(.*?)\};', source, re.S).group(1)
        definitions.append('static const uint8_t '+name+'[] = {'+body+'};')
        checks += [f'bool {name}_hw = dp_vt_hevc_hardware_probe({name}, sizeof({name}));', f'printf("{name} actual hardware: %s\\n", {name}_hw ? "yes" : "no");']
        if args.require_main and name in ['HEVCMain', 'HEVCMain10']:
            checks += [f'assert({name}_hw);']
    (path/'test.c').write_text('#include "vt-hardware.h"\n#include <assert.h>\n#include <stdio.h>\n'+ '\n'.join(definitions)+'\nint main(void) {\n'+'\n'.join(checks)+'\nreturn 0; }\n')
    sp.run(['xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', root/'app/streaming/video/ffmpeg-renderers', path/'test.c', root/'app/streaming/video/ffmpeg-renderers/vt-hardware.c', '-framework', 'VideoToolbox', '-framework', 'CoreMedia', '-framework', 'CoreVideo', '-framework', 'CoreFoundation', '-o', path/'test'], check=True)
    sp.run([path/'test'], check=True, timeout=30)
