#!/usr/bin/env python3
"""Keep Vulkan driver initialization bounded across Linux encoder probes."""
from pathlib import Path
import shutil
import sys
root = Path(sys.argv[1])
header = Path(sys.argv[2])
source = root / 'src/video.cpp'
s = source.read_text()
marker = '    deskport::vulkan::keep_drivers_loaded();'
include = '#include "deskport/linux/vulkan-driver-lifetime.h"'
if marker not in s:
    anchor = '  util::Either<avcodec_buffer_t, int> vulkan_init_avcodec_hardware_input_buffer(platf::avcodec_encode_device_t *encode_device) {\n'
    if s.count(anchor) != 1 or s.count('#include "video.h"') != 1:
        raise SystemExit('Vulkan lifetime overlay anchor mismatch')
    s = s.replace('#include "video.h"', '#include "video.h"\n#if defined(__linux__) && defined(SUNSHINE_BUILD_VULKAN)\n' + include + '\n#endif', 1)
    s = s.replace(anchor, anchor + '#if defined(__linux__)\n' + marker + '\n#endif\n', 1)
    source.write_text(s)
elif include not in s:
    raise SystemExit('Partial Vulkan lifetime overlay')
target = root / 'src/deskport/linux/vulkan-driver-lifetime.h'
target.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(header, target)
