#!/usr/bin/env python3
"""Compile the production NVENC transaction with pinned SDK types/fault injection.

No GPU/capture/input is used. This is API/rollback verification, not hardware
acceptance. Also exercise the platform-neutral Windows rate-overlay order on both pins.
"""
from pathlib import Path
import os
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-live-bitrate-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as tar:
            tar.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        # Match Windows: encoder policy precedes the input/cadence adapters;
        # Linux-only cadence/capture overlays are intentionally absent.
        for name in ('smart-stream', 'session-settings', 'session-takeover',
                     'encoder-policy', 'input-activity', 'sync-cadence',
                     'remote-rate', 'data-saver', 'video-pause', 'rate-api'):
            subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
        before = (target / 'src/video.cpp').read_text()
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-rate-api.py'), str(target)], check=True)
        assert before == (target / 'src/video.cpp').read_text()
        source = (target / 'src/nvenc/nvenc_base.cpp').read_text()
        start = source.index('  bool nvenc_base::deskport_set_bitrate(')
        end = source.index('\n  void nvenc_base::destroy_encoder()', start)
        method = source[start:end]
        assert 'NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE' in source
        assert 'deskport_init.encodeConfig = nullptr;' in source
        assert 'deskport::liveEncoder = 0;' in before
        # Exercise the production initial clamp even when no live API exists.
        init_start = before.index('    auto deskport_encoder_config = client_config;')
        init_end = before.index('    return std::make_unique<nvenc_encode_session_t>', init_start)
        initial_clamp = before[init_start:init_end]
        clamp_test = r'''#include <algorithm>
#include <cassert>
#include <memory>
struct Config { int bitrate, width = 2560, height = 1440, framerate = 60; };
namespace config { struct { int max_bitrate = 0; } video; }
int created_rate = 0;
struct Device {
    int colorspace = 7;
    bool init_encoder(const Config& value, int color) {
        assert(value.width == 2560 && value.height == 1440 && value.framerate == 60 && color == 7);
        created_rate = value.bitrate;
        return true;
    }
};
std::unique_ptr<int> create(const Config& client_config, std::unique_ptr<Device> encode_device) {
''' + initial_clamp + r'''
    return std::make_unique<int>(created_rate);
}
int main() {
    Config original {15000};
    for (int limit : {0, -1, 10000, 30000}) {
        config::video.max_bitrate = limit;
        auto result = create(original, std::make_unique<Device>());
        assert(result && *result == (limit == 10000 ? 10000 : 15000));
        assert(original.bitrate == 15000);
    }
    original.bitrate = 0;
    assert(!create(original, std::make_unique<Device>()));
}
'''
        cpp, binary = target / 'clamp.cpp', target / 'clamp'
        cpp.write_text(clamp_test)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        assert 'deskport::appliedCeilingPermille = -1;' in (target / 'src/rtsp.cpp').read_text()
        api = (target / 'src/confighttp.cpp').read_text()
        assert 'deskport::appliedCeilingPermille.load() == permille' in api
        # The newer header is shared with SDK 13 from the older source pin.
        headers = list(target.rglob('ffnvcodec/nvEncodeAPI.h'))
        assert headers, archive
        harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <ffnvcodec/nvEncodeAPI.h>
#define BOOST_LOG(level) std::cerr
struct nvenc_base {
    void* encoder = this;
    bool deskport_dynamic_rate = true;
    NV_ENC_INITIALIZE_PARAMS deskport_init {};
    NV_ENC_CONFIG deskport_config {}, deskport_initial_config {};
    std::shared_ptr<NV_ENCODE_API_FUNCTION_LIST> nvenc = std::make_shared<NV_ENCODE_API_FUNCTION_LIST>();
    std::string last_nvenc_error_string = "injected";
    bool nvenc_failed(NVENCSTATUS status) { return status != NV_ENC_SUCCESS; }
    bool deskport_set_bitrate(int);
};
int calls = 0, fail_count = 0;
NV_ENC_CONFIG accepted {};
NVENCSTATUS NVENCAPI reconfigure(void*, NV_ENC_RECONFIGURE_PARAMS* params) {
    ++calls;
    assert(params->version == NV_ENC_RECONFIGURE_PARAMS_VER);
    assert(!params->resetEncoder && !params->forceIDR);
    auto init = params->reInitEncodeParams;
    assert(init.encodeWidth == 2560 && init.encodeHeight == 1440);
    assert(init.frameRateNum == 60000 && init.frameRateDen == 1001);
    assert(init.encodeConfig && init.enableEncodeAsync == 1);
    assert(init.encodeConfig->encodeCodecConfig.h264Config.chromaFormatIDC == 1);
    assert(init.encodeConfig->gopLength == NVENC_INFINITE_GOPLENGTH);
    if (fail_count) { --fail_count; return NV_ENC_ERR_GENERIC; }
    accepted = *init.encodeConfig;
    return NV_ENC_SUCCESS;
}
'''
        harness += method + r'''
int main() {
    nvenc_base encoder;
    auto& init = encoder.deskport_init;
    init.encodeWidth = 2560; init.encodeHeight = 1440;
    init.frameRateNum = 60000; init.frameRateDen = 1001; init.enableEncodeAsync = 1;
    auto& original = encoder.deskport_initial_config;
    original.gopLength = NVENC_INFINITE_GOPLENGTH;
    original.encodeCodecConfig.h264Config.chromaFormatIDC = 1;
    original.rcParams.averageBitRate = 15000000;
    original.rcParams.maxBitRate = 18000000;
    original.rcParams.vbvBufferSize = 500000;
    original.rcParams.vbvInitialDelay = 250000;
    encoder.deskport_config = original;
    encoder.nvenc->nvEncReconfigureEncoder = reconfigure;
    assert(!encoder.deskport_set_bitrate(0));
    assert(!encoder.deskport_set_bitrate(-1));
    assert(!encoder.deskport_set_bitrate(15001));
    assert(!encoder.deskport_set_bitrate(2147483647));
    assert(calls == 0);
    assert(encoder.deskport_set_bitrate(15000) && calls == 0);
    for (int i = 0; i < 200; ++i) {
        assert(encoder.deskport_set_bitrate(3000));
        assert(accepted.rcParams.averageBitRate == 3000000);
        assert(accepted.rcParams.maxBitRate == 3600000);
        assert(accepted.rcParams.vbvBufferSize == 100000);
        assert(accepted.rcParams.vbvInitialDelay == 50000);
        assert(encoder.deskport_set_bitrate(15000));
        assert(!std::memcmp(&encoder.deskport_config, &original, sizeof(original)));
    }
    const int before = calls;
    fail_count = 1;
    assert(!encoder.deskport_set_bitrate(4000));
    assert(calls == before + 2 && !encoder.deskport_dynamic_rate);
    assert(accepted.rcParams.averageBitRate == original.rcParams.averageBitRate);
    assert(!std::memcmp(&encoder.deskport_config, &original, sizeof(original)));
    assert(!encoder.deskport_set_bitrate(5000) && calls == before + 2);
    encoder.deskport_dynamic_rate = true;
    fail_count = 2; // both mutation and rollback fail: capability stays revoked
    assert(!encoder.deskport_set_bitrate(5000) && !encoder.deskport_dynamic_rate);
    encoder.deskport_dynamic_rate = true;
    encoder.encoder = nullptr;
    assert(!encoder.deskport_set_bitrate(5000));
    std::cout << "PASS: production NVENC bounds/VBV/format/rollback/failure latch\n";
}
'''
        cpp, binary = target / 'rate-test.cpp', target / 'rate-test'
        cpp.write_text(harness)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-Wno-missing-field-initializers', '-I', str(headers[0].parents[1]),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        # Execute the actual API wait/acknowledgment predicate with concurrent
        # encode-thread success/failure and a stalled encoder.
        begin = api.index('            const auto deadline =')
        end = api.index('\n          }', begin)
        ack = r'''#include "host/common/ratecontrol.h"
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <thread>
bool response(int permille, int saver = 0) {
    std::map<std::string, bool> output;
''' + api[begin:end] + r'''
    return output["live"];
}
int main() {
    deskport::liveEncoder = 0; deskport::appliedCeilingPermille = 0; deskport::appliedSaver = 0;
    assert(!response(250)); // fixed backend / previous stream capability reset
    deskport::liveEncoder = 1;
    std::thread success([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        deskport::appliedCeilingPermille = 250;
    });
    assert(response(250)); success.join();
    assert(!response(250, 1)); // Equal bitrate must still acknowledge saver properties.
    deskport::appliedSaver = 1; assert(response(250, 1));
    std::thread failure([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        deskport::liveEncoder = 0;
    });
    assert(!response(500)); failure.join();
    deskport::liveEncoder = 1;
    const auto start = std::chrono::steady_clock::now();
    assert(!response(500)); // no false live from the old acknowledged ceiling
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}
'''
        cpp.write_text(ack)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread', '-I', str(root),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f'PASS: Windows live-rate overlays/idempotence: {archive}')
