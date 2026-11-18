#!/usr/bin/env python3
"""Connect native SCK source color state to the existing host media contract."""
from pathlib import Path
import sys
root = Path(sys.argv[1])
def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f'Mac color anchor mismatch: {name}: {old[:80]!r}')
    path.write_text(source.replace(old, new))

patch('src/platform/macos/display.mm',
      '    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb,',
      '''    bool is_hdr() override {
      // Source capability must not depend on the previous probe's pixel format.
      // Client dynamicRange chooses HDR/SDR; only the native VT capture path can HDR.
      return av_capture.hdrAvailable && av_capture.hdrCaptureAllowed;
    }

    bool get_hdr_metadata(SS_HDR_METADATA &metadata) override {
      // SCK canonical BT.2020/PQ has no public mastering-monitor/CLL data.
      // Zero means unavailable; never invent a 1000-nit mastering display.
      std::memset(&metadata, 0, sizeof(metadata));
      return is_hdr();
    }

    capture_e capture(const push_captured_image_cb_t &push_captured_image_cb,''')
# Active HDR and optional static mastering metadata are separate. The control
# channel still reports active HDR with unknown/zero metadata; omit invalid SEI.
path = root / 'src/video.cpp'
source = path.read_text()
marker = '        if (hdr_metadata.maxDisplayLuminance != 0 || hdr_metadata.displayPrimaries[0].x != 0) {'
if marker not in source:
    start = source.index('        auto mdm = av_mastering_display_metadata_create_side_data(frame.get());')
    end = source.index('        if (hdr_metadata.maxContentLightLevel != 0 || hdr_metadata.maxFrameAverageLightLevel != 0) {', start)
    block = source[start:end].rstrip()
    patch('src/video.cpp', block,
          marker + '\n' + '\n'.join('  ' + line if line else '' for line in block.splitlines()) + '\n        }')

# The SCK bridge emits canonical limited-range BT.709 SDR or BT.2020/PQ.
# Client CSC preferences must not retag those native YUV samples as 601/full.
patch('src/video.cpp',
      '    auto colorspace = colorspace_from_client_config(config, disp.is_hdr());',
      '''    auto colorspace = colorspace_from_client_config(config, disp.is_hdr());
#ifdef __APPLE__
    if (!colorspace_is_hdr(colorspace)) colorspace.colorspace = colorspace_e::rec709;
    colorspace.full_range = false;
#endif''')

patch('src/platform/macos/display.mm',
      '    display->width = display->av_capture.frameWidth;',
      '''    display->av_capture.hdrCaptureAllowed = hwdevice_type == mem_type_e::videotoolbox;
    display->width = display->av_capture.frameWidth;''')
