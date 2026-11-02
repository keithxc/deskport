#!/usr/bin/env python3
"""Execute the patched encoder rate loop with fake transport and encoders."""
from pathlib import Path
import ast
import os
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
# Extract the replacement as a boundary, then execute the corresponding bytes
# from each patched upstream tree. This tests the production loop, not a copy.
module = ast.parse((root / 'host/overlays/patch-host-remote-rate.py').read_text())
rate_block = next(node.value.args[2].value for node in module.body
                  if isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                  and getattr(node.value.func, 'id', '') == 'patch'
                  and isinstance(node.value.args[2], ast.Constant)
                  and 'const bool deskport_lost =' in node.value.args[2].value)
earlier = ('smart-stream', 'session-settings', 'session-takeover', 'linux-display',
           'input-activity', 'sync-cadence', 'linux-cadence', 'pipewire-memory', 'encoder-policy', 'remote-rate')

harness = r'''
#include "host/common/ratecontrol.h"
#include "host/common/smartstream.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
using namespace std::chrono_literals;
struct Log { template<typename T> Log& operator<<(const T&) { return *this; } };
#define BOOST_LOG(level) Log{}
struct Event {
  std::optional<int> value;
  auto pop(std::chrono::milliseconds) { auto result = value; value.reset(); return result; }
  void raise(int next) { value = next; }
};
struct Mail {
  Event output;
  template<typename T> Event* event(const char*) { return &output; }
};
struct Applied { int kbps; bool constrained, saver; };
struct Encoder {
  std::vector<Applied> calls;
  bool accepted = true;
  bool deskport_set_bitrate(int kbps, bool constrained, bool saver) {
    calls.push_back({kbps, constrained, saver}); return accepted;
  }
};
std::mutex deskport_rate_memory_mutex;
deskport::RateMemory deskport_rate_memory;
std::int64_t deskport_wall_ms() { return 1000; }
struct Runner {
  bool smart = true, deskport_live = true;
  int deskport_ceiling_kbps = 10000, deskport_permille = 0, deskport_last_rtt = 0;
  deskport::EncoderRateState deskport_applied_rate;
  deskport::StreamPolicy policy {60};
  std::optional<deskport::BitrateController> deskport_rate;
  Event loss, rtt, sent;
  Event *congestion = &loss, *deskport_rtt = &rtt, *deskport_sent = &sent;
  Encoder encoder;
  Encoder *session = &encoder;
  Mail mailbox;
  Mail *mail = &mailbox;
  Runner() {
    deskport::liveSaver = 0; deskport::liveCeilingPermille = 0; deskport::liveEncoder = 1;
    deskport_applied_rate.applied(deskport_ceiling_kbps, false, false);
    deskport_rate_memory = {};
  }
  void step(std::int64_t now_us = 1000000) {
''' + rate_block + r'''
  }
};
int main() {
  // LAN: saver must alter the burst property in both directions at equal kbps.
  Runner lan;
  lan.step(); assert(lan.encoder.calls.empty());
  deskport::liveSaver = 1; lan.step();
  assert(lan.encoder.calls.size() == 1 && lan.encoder.calls.back().saver);
  assert(lan.encoder.calls.back().kbps == 10000 && !lan.encoder.calls.back().constrained);
  lan.step(); assert(lan.encoder.calls.size() == 1);
  deskport::liveSaver = 0; lan.step();
  assert(lan.encoder.calls.size() == 2 && !lan.encoder.calls.back().saver);
  deskport::liveCeilingPermille = 50; lan.step();
  assert(lan.encoder.calls.size() == 3 && lan.encoder.calls.back().kbps == 500);
  deskport::liveSaver = 1; lan.step();
  assert(lan.encoder.calls.size() == 4 && lan.encoder.calls.back().kbps == 500 && lan.encoder.calls.back().saver);

  // Remote: changing only the ceiling can remove the QP floor at equal kbps.
  Runner remote;
  remote.deskport_rate.emplace(10000, 0);
  remote.deskport_rate->setCeiling(5000); remote.deskport_rate->resume(2000);
  remote.deskport_permille = 500; deskport::liveCeilingPermille = 500;
  remote.deskport_applied_rate.applied(2000, true, false);
  remote.step(); assert(remote.encoder.calls.empty());
  deskport::liveCeilingPermille = 400; remote.step();
  assert(remote.encoder.calls.size() == 1 && remote.encoder.calls.back().kbps == 2000);
  assert(!remote.encoder.calls.back().constrained);
  deskport::liveSaver = 1; remote.step();
  assert(remote.encoder.calls.size() == 2 && remote.encoder.calls.back().saver);
  deskport::liveSaver = 0; remote.step();
  assert(remote.encoder.calls.size() == 3 && !remote.encoder.calls.back().saver);
  assert(remote.encoder.calls.back().kbps == 2000);

  // The first RTT resumes the remembered target without briefly raising it.
  Runner resume;
  deskport_rate_memory.store(10000, 2000, 0);
  resume.rtt.raise(20); resume.step();
  assert(resume.encoder.calls.size() == 1 && resume.encoder.calls.back().kbps == 2000);

  // Unsupported and rejected encoders cannot be reported as live-capable.
  Runner unsupported; unsupported.deskport_live = false; deskport::liveEncoder = 0;
  deskport::liveSaver = 1; unsupported.step(); assert(unsupported.encoder.calls.empty());
  Runner rejected; rejected.encoder.accepted = false;
  deskport::liveSaver = 1; rejected.step();
  assert(rejected.encoder.calls.size() == 1 && !rejected.deskport_live && deskport::liveEncoder == 0);
  assert(!rejected.deskport_applied_rate.saver);
  rejected.step(); assert(rejected.encoder.calls.size() == 1);
}
'''

recovery_harness = r'''
#include "host/common/ratecontrol.h"
#include "host/common/framecadence.h"
#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
using namespace std::chrono_literals;
struct Log { template<typename T> Log& operator<<(const T&) { return *this; } };
#define BOOST_LOG(level) Log{}
template<typename T> struct Event {
  std::optional<T> value;
  bool peek() const { return value.has_value(); }
  auto pop(std::chrono::milliseconds = 0ms) { auto result = value; value.reset(); return result; }
  void raise(T next) { value = next; }
};
struct Encoder {
  int invalidations = 0, idrs = 0;
  virtual ~Encoder() = default;
  virtual void invalidate_ref_frames(std::int64_t, std::int64_t) { ++invalidations; }
  void request_idr_frame() { ++idrs; }
};
struct avcodec_encode_session_t : Encoder {
  void invalidate_ref_frames(std::int64_t, std::int64_t) override { ++invalidations; request_idr_frame(); }
};
struct Runner {
  bool smart = true, requested = false;
  std::unique_ptr<Encoder> session = std::make_unique<avcodec_encode_session_t>();
  Event<std::pair<std::int64_t, std::int64_t>> invalidation;
  Event<bool> idr;
  auto* refs() { return &invalidation; }
  Event<std::pair<std::int64_t, std::int64_t>>* invalidate_ref_frames_events = &invalidation;
  Event<bool>* idr_events = &idr;
  deskport::KeyframeGate deskport_key_gate;
  deskport::SaverRefinement deskport_refinement;
  int deskport_last_rtt = 0;
  std::int64_t deskport_last_change_ms = -1, deskport_change_candidate_ms = -1, deskport_coalesced_log_ms = 0;
  std::uint64_t deskport_coalesced_logged = 0;
  void step() {
    bool requested_idr_frame = false;
__RECOVERY_BLOCK__
    requested = requested_idr_frame;
  }
  void denyRequests() {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
    deskport_key_gate.allow(now + 60000, 0);
  }
  bool refining() const {
    return deskport_refinement.active(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  }
};
int main() {
  // Without data saver an accepted invalidation keeps the original path: only
  // the encoder's own IDR answers it, with no extra request or refinement.
  deskport::liveSaver = 0;
  Runner plain;
  plain.invalidation.raise({10, 11}); plain.step();
  assert(!plain.requested && plain.session->invalidations == 1 && plain.session->idrs == 1);
  assert(!plain.refining() && plain.deskport_change_candidate_ms == -1);
  deskport::liveSaver = 1;
  Runner ref;
  ref.invalidation.raise({10, 11}); ref.step();
  assert(ref.requested && ref.session->invalidations == 1 && ref.session->idrs > 0);
  assert(ref.refining() && ref.deskport_change_candidate_ms >= 0);
  // A simultaneous explicit request is coalesced, but accepted invalidation
  // must still bypass the saver idle/content wait and start refinement.
  Runner combined;
  combined.invalidation.raise({10, 11}); combined.idr.raise(true); combined.step();
  assert(combined.requested && combined.session->invalidations == 1 && !combined.idr.peek());
  assert(combined.refining() && combined.deskport_change_candidate_ms >= 0);
  assert(combined.deskport_key_gate.coalesced == 1);
  Runner denied;
  denied.denyRequests(); denied.invalidation.raise({10, 11}); denied.idr.raise(true); denied.step();
  assert(!denied.requested && denied.session->invalidations == 0 && denied.session->idrs == 0);
  assert(!denied.refining() && denied.deskport_change_candidate_ms == -1);
  // Native reference invalidation remains ungated and does not request an IDR.
  Runner native;
  native.session = std::make_unique<Encoder>(); native.denyRequests();
  native.invalidation.raise({10, 11}); native.step();
  assert(!native.requested && native.session->invalidations == 1 && native.session->idrs == 0);
  assert(!native.refining() && native.deskport_change_candidate_ms == -1);
  Runner explicitIdr;
  explicitIdr.idr.raise(true); explicitIdr.step();
  assert(explicitIdr.requested && explicitIdr.session->idrs == 1 && explicitIdr.refining());
  assert(explicitIdr.deskport_change_candidate_ms >= 0);
}
'''

for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-rate-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as stream:
            stream.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in earlier:
            subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
        video = (target / 'src/video.cpp').read_text()
        assert video.count(rate_block) == 1
        probe = target / 'rate-probe.cpp'
        probe.write_text(harness.replace(rate_block, video[video.index(rate_block):video.index(rate_block) + len(rate_block)]))
        binary = target / 'rate-probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(root), str(probe), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-data-saver.py'), str(target)], check=True)
        video = (target / 'src/video.cpp').read_text()
        start = video.index('      const auto deskport_key_ms =')
        end = video.index('      std::optional<std::chrono::steady_clock::time_point> frame_timestamp;', start)
        recovery = target / 'recovery-probe.cpp'
        recovery.write_text(recovery_harness.replace('__RECOVERY_BLOCK__', video[start:end]))
        recovery_binary = target / 'recovery-probe'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(root), str(recovery), '-o', str(recovery_binary)], check=True)
        subprocess.run([str(recovery_binary)], check=True)
        print(f'PASS: live rate, saver/quality transitions and recovery aggregation: {archive}')
