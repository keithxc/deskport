// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/streaming/videoformats.h"
#include <cassert>
#include <cstdio>

int main() {
    enum { None, Software, Hardware };
    auto run = [](int hevc, int av1, bool forceHardware) {
        SupportedVideoFormatList formats;
        formats << VIDEO_FORMAT_AV1_MAIN10 << VIDEO_FORMAT_H265_MAIN10 << VIDEO_FORMAT_H265 << VIDEO_FORMAT_H264;
        unsigned probes = 0;
        assert(formats.qualify([&](int format) {
            ++probes;
            int support = format == VIDEO_FORMAT_AV1_MAIN10 ? av1 : format == VIDEO_FORMAT_H265_MAIN10 ? hevc : Hardware;
            return support != None && (!forceHardware || support == Hardware);
        }));
        int expected = av1 == Hardware || (av1 == Software && !forceHardware) ? VIDEO_FORMAT_AV1_MAIN10 :
            hevc == Hardware || (hevc == Software && !forceHardware) ? VIDEO_FORMAT_H265_MAIN10 : VIDEO_FORMAT_H265;
        assert(formats.front() == expected);
        assert(probes == (expected == VIDEO_FORMAT_AV1_MAIN10 ? 1u : expected == VIDEO_FORMAT_H265_MAIN10 ? 2u : 3u));
    };
    for (int hevc = None; hevc <= Hardware; ++hevc)
        for (int av1 = None; av1 <= Hardware; ++av1)
            for (bool forceHardware : {false, true}) run(hevc, av1, forceHardware);
    SupportedVideoFormatList none;
    none << VIDEO_FORMAT_AV1_MAIN10 << VIDEO_FORMAT_H264;
    assert(!none.qualify([](int) { return false; }) && none.isEmpty());
    SupportedVideoFormatList profiles;
    profiles << VIDEO_FORMAT_AV1_MAIN10 << VIDEO_FORMAT_AV1_MAIN8 << VIDEO_FORMAT_H265_MAIN10;
    const int host = profiles.maskByServerCodecModes(SCM_AV1_MAIN8 | SCM_HEVC_MAIN10);
    profiles.removeByMask((VIDEO_FORMAT_MASK_10BIT | VIDEO_FORMAT_MASK_YUV444 | VIDEO_FORMAT_MASK_AV1) & ~host);
    assert(!profiles.contains(VIDEO_FORMAT_AV1_MAIN10) && profiles.contains(VIDEO_FORMAT_AV1_MAIN8) && profiles.contains(VIDEO_FORMAT_H265_MAIN10));
    puts("PASS: 18 exact-profile software/hardware/HDR/SDR selections, no unsupported resurrection, exact host profile intersection");
}
