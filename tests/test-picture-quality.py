#!/usr/bin/env python3
"""Run per-picture QP policy and verbatim production adapter blocks without VT."""
from pathlib import Path
import os
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CXX = os.environ.get('CXX', 'c++')


def compile_run(path, source):
    path.write_text(source)
    binary = path.with_suffix('')
    subprocess.run([CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                    '-I', str(ROOT), str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


HARNESS = r'''
#include "host/common/picturequality.h"
#include "host/common/ratecontrol.h"
#include "host/macos/vtclock.h"
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <utility>
#include <vector>
struct Log { template<typename T> Log& operator<<(const T&) { return *this; } };
#define BOOST_LOG(level) Log{}
constexpr int AV_FRAME_FLAG_KEY = 1;
constexpr int EOF_CODE = -12;
#define AVERROR(value) (-(value))
#define AVERROR_EOF EOF_CODE
using Writes = std::vector<std::pair<char, int>>;
struct Rational { int num = 60, den = 1; };
double av_q2d(Rational value) { return double(value.num) / value.den; }
struct Context {
    int min = 0, max = 51, fail = -1, rateCalls = 0, width = 2040, height = 1228;
    double expected = 30; bool expectedAccepted = true;
    Rational framerate; Writes writes; std::vector<char> operations;
};
namespace deskport::vt {
using VTCompressionSessionRef = Context*;
using CFNumberRef = double*;
using OSStatus = int;
constexpr int kCFNumberDoubleType = 0, kVTCompressionPropertyKey_ExpectedFrameRate = 0, noErr = 0;
CFNumberRef CFNumberCreate(std::nullptr_t, int, const double* value) { return new double(*value); }
void CFRelease(CFNumberRef value) { delete value; }
OSStatus VTSessionSetProperty(Context* ctx, int, CFNumberRef value) {
    ctx->operations.push_back('F');
    if (!ctx->expectedAccepted) return -1;
    ctx->expected = *value; return noErr;
}
__EXPECTED__
Context* session(Context* ctx) { return ctx; }
bool applyRate(Context* ctx, std::int64_t, int) { ++ctx->rateCalls; ctx->operations.push_back('R'); return true; }
bool applyQualityFloor(Context* ctx, int minimum) {
    ctx->writes.push_back({'m', minimum});
    if (minimum > ctx->max) return false;
    ctx->min = minimum; return true;
}
bool applyQualityCeiling(Context* ctx, int maximum) {
    ctx->writes.push_back({'M', maximum});
    if (ctx->fail == maximum || maximum < ctx->min) return false;
    ctx->max = maximum; return true;
}
__RANGE__
}
struct Base {
    virtual ~Base() = default;
    virtual bool deskport_set_bitrate(int, bool, bool) = 0;
};
struct avcodec_encode_session_t : Base {
    std::unique_ptr<Context> avcodec_ctx = std::make_unique<Context>();
    deskport::PictureQuality deskport_quality;
    deskport::vt::FrameClock deskport_clock;
__SETTER__
};
void capture(std::unique_ptr<Base>& session) {
__CAPTURE__
}
struct Call { std::int64_t frame_nr, native = -1; bool currentProduced = false; };
int before(avcodec_encode_session_t& session, Call& call, bool key = false) {
    struct Frame { int flags; } value {key ? AV_FRAME_FLAG_KEY : 0};
    auto* frame = &value;
    auto& ctx = session.avcodec_ctx;
__BEFORE__
    call.native = *session.deskport_clock.submit(call.frame_nr * 125000, call.frame_nr, 1, 60);
    return 0;
}
int output(avcodec_encode_session_t& session, Call& call, std::int64_t native) {
    struct Packet { std::int64_t pts; } value {native}; auto* av_packet = &value;
    const bool deskport_timed_vt = true;
    const auto frame_nr = call.frame_nr;
    auto& deskport_current_produced = call.currentProduced;
__OUTPUT__
    return 0;
}
int finish(avcodec_encode_session_t& session, Call& call) {
    const bool deskport_timed_vt = true;
    auto& deskport_current_produced = call.currentProduced;
    const int ret = AVERROR(EAGAIN);
__DRAIN__
    return -2;
}
int main() {
    auto encoder = std::make_unique<avcodec_encode_session_t>();
    auto* ptr = encoder.get(); auto& ctx = *ptr->avcodec_ctx;
    std::unique_ptr<Base> base = std::move(encoder);
    assert(ptr->deskport_set_bitrate(992, false, true));
    assert(ctx.min == 22 && ctx.max == 51);
    assert(ctx.expected == 8 && ctx.framerate.num == 60 && ctx.framerate.den == 1);
    assert((ctx.operations == std::vector<char>{'F', 'R'}));
    const auto propertyCalls = ctx.operations.size();
    for (const double invalid : {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        assert(!deskport::vt::applyExpectedFrameRate(&ctx, invalid));
        assert(ctx.expected == 8 && ctx.operations.size() == propertyCalls);
    }
    ctx.writes.clear();
    Call idle {0}; assert(before(*ptr, idle) == 0 && ctx.writes.empty());
    assert(finish(*ptr, idle) == 0);
    capture(base); // image was popped/converted, but gate may delay submission
    capture(base);
    Call fresh {1}; assert(before(*ptr, fresh) == 0 && ctx.max == 26);
    assert((ctx.writes == Writes{{'M',26}}));
    assert(output(*ptr, fresh, fresh.native) == 0);
    assert(finish(*ptr, fresh) == 0); // EAGAIN after current output is not a drop
    capture(base);
    Call nextFresh {2}; assert(before(*ptr, nextFresh) == 0 && ctx.max == 26);
    assert(ctx.writes.size() == 1); // no native write when requested Max is unchanged
    assert(finish(*ptr, nextFresh) == 0); // current strict picture dropped
    capture(base);
    Call retry {3}; assert(before(*ptr, retry) == 0 && ctx.max == 51);
    assert(output(*ptr, retry, retry.native) == 0 && finish(*ptr, retry) == 0);
    Call refine {4}; assert(before(*ptr, refine) == 0 && ctx.max == 51);
    const auto writes = ctx.writes.size();
    assert(finish(*ptr, refine) == 0 && ctx.writes.size() == writes);

    // Delayed old output does not prove that the current strict picture exists.
    capture(base);
    Call strict {5}; assert(before(*ptr, strict) == 0 && ctx.max == 26);
    assert(output(*ptr, strict, refine.native) == 0 && !strict.currentProduced);
    assert(finish(*ptr, strict) == 0);
    capture(base);
    Call forced {6}; assert(before(*ptr, forced) == 0 && ctx.max == 51);
    assert(finish(*ptr, forced) == 0);

    // A fresh KEY always bypasses Max26 and cannot start the strict drop loop.
    capture(base);
    Call key {7}; assert(before(*ptr, key, true) == 0 && ctx.max == 51);
    assert(finish(*ptr, key) == 0);
    capture(base);
    Call afterKey {8}; assert(before(*ptr, afterKey) == 0 && ctx.max == 26);
    assert(output(*ptr, afterKey, afterKey.native) == 0 && finish(*ptr, afterKey) == 0);
    assert(ctx.rateCalls == 1 && ctx.min == 22); // picture policy never changes Avg/DRL/Min

    assert(ptr->deskport_set_bitrate(499, true, true));
    assert(ctx.min == 32 && ctx.max == 51);
    capture(base);
    Call congested {9}; assert(before(*ptr, congested) == 0 && ctx.max == 51);
    assert(finish(*ptr, congested) == 0);
    assert(ptr->deskport_set_bitrate(499, false, false));
    assert(ctx.expected == 60);
    capture(base);
    Call ordinary {10}; assert(before(*ptr, ordinary) == 0 && ctx.min == 0 && ctx.max == 51);
    assert(finish(*ptr, ordinary) == 0);

    ctx.framerate = {60000, 1001};
    assert(ptr->deskport_set_bitrate(499, false, false));
    assert(std::abs(ctx.expected - 60000.0 / 1001) < 1e-9); // restore the negotiated rational, not rounded 60
    const auto rateCalls = ctx.rateCalls;
    ctx.expectedAccepted = false;
    assert(!ptr->deskport_set_bitrate(992, false, true));
    assert(ctx.rateCalls == rateCalls); // a failed hint cannot silently claim complete policy success
    ctx.expectedAccepted = true;

    assert(ptr->deskport_set_bitrate(499, false, true));
    capture(base);
    Call lowBudget {11}; assert(before(*ptr, lowBudget) == 0 && ctx.min == 22 && ctx.max == 51);
    assert(finish(*ptr, lowBudget) == 0); // effective low budget disables only Max26
    ctx.width = 1280; ctx.height = 800;
    assert(ptr->deskport_set_bitrate(351, false, true));
    capture(base);
    Call smallerPicture {12}; assert(before(*ptr, smallerPicture) == 0 && ctx.max == 26);
    assert(output(*ptr, smallerPicture, smallerPicture.native) == 0 && finish(*ptr, smallerPicture) == 0);
    ctx.width = 0;
    assert(ptr->deskport_set_bitrate(992, false, true));
    capture(base);
    Call invalidSize {13}; assert(before(*ptr, invalidSize) == 0 && ctx.max == 51);
    assert(finish(*ptr, invalidSize) == 0);
    ctx.width = 2040; ctx.height = 1228;
    assert(ptr->deskport_set_bitrate(992, false, true));
    capture(base); ctx.fail = 26;
    Call rejected {14}; assert(before(*ptr, rejected) == -1);
    assert(ctx.max == 51); // rejected setter never enters the applied cache
    assert(ptr->deskport_quality.needsMaximum(26));
}
'''

checkout = subprocess.check_output(['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
directory = Path(os.environ.get('DESKPORT_TEST_DIR', Path(checkout) / 'tests'))
directory.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='deskport-picture-', dir=directory) as tmp:
    work = Path(tmp)
    compile_run(work / 'portable.cpp', (ROOT / 'tests/picture-quality.cpp').read_text())
    for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
        target = work / archive.removesuffix('.tar.gz'); target.mkdir()
        with tarfile.open(ROOT / 'host/vendor' / archive) as stream:
            stream.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display',
                     'encoder-policy', 'vt-drain', 'input-activity', 'sync-cadence', 'linux-cadence',
                     'pipewire-memory', 'remote-rate', 'data-saver', 'vt-drain'):
            subprocess.run(['python3', str(ROOT / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
        video = (target / 'src/video.cpp').read_text()
        begin = video.index('    bool deskport_set_bitrate(int kbps, bool constrained, bool saver) override')
        setter = video[begin:video.index('\n    }', begin) + len('\n    }')]
        begin = video.index('          if (auto *avcodec_session = dynamic_cast<avcodec_encode_session_t *>(session.get())) {')
        capture = video[begin:video.index('          frame_timestamp = img->frame_timestamp;', begin)]
        assert video.rfind('if (auto img = images->pop(', 0, begin) > video.index('void encode_run(')
        assert video.index('avcodec_session->deskport_quality.captured();') < video.index('deskport_saver_gate.admit(deskport_saver_us, requested_idr_frame)')
        assert video.count('avcodec_session->deskport_quality.captured();') == 1
        assert 'deskport_quality.changed(' not in video and 'deskport_quality.relax(' not in video
        begin = video.index('      const int deskport_max_qp = session.deskport_quality.requestedMaximum(')
        before = video[begin:video.index('      const auto pts = session.deskport_clock.submit(', begin)]
        begin = video.index('      if (deskport_timed_vt) {', video.index('session.deskport_keys.take(av_packet->pts)'))
        output = video[begin:video.index('\n#endif', begin)]
        assert video.index('av_packet->pts = *identity;') < video.index('if (*identity == frame_nr) deskport_current_produced = true;') < video.index('av_packet->pts -= session.deskport_pts_shift;')
        begin = video.index('      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {')
        drain = video[begin:video.index('      } else if (ret < 0)', begin) + len('      }')]
        header = (ROOT / 'host/macos/vtbitrate.h').read_text()
        begin = header.index('inline bool applyQualityRange(')
        quality_range = header[begin:header.index('\n}', begin) + len('\n}')]
        begin = header.index('inline bool applyExpectedFrameRate(')
        expected = header[begin:header.index('\n}', begin) + len('\n}')]
        source = HARNESS.replace('__SETTER__', setter).replace('__CAPTURE__', capture)
        source = source.replace('__BEFORE__', before).replace('__OUTPUT__', output)
        source = source.replace('__DRAIN__', drain).replace('__RANGE__', quality_range).replace('__EXPECTED__', expected)
        compile_run(target / 'picture.cpp', '#define __APPLE__ 1\n' + source)
        print(f'PASS: actual capture/MaxQP/logical output/drain/rate adapter blocks: {archive}')
