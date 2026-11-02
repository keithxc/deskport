#!/usr/bin/env python3
"""Exercise both repository-owned upstream revisions without a live session."""
from pathlib import Path
import subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-cadence-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as f:
            f.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display', 'vt-drain', 'input-activity', 'sync-cadence', 'linux-cadence', 'pipewire-memory', 'encoder-policy', 'remote-rate'):
            subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-encoder-policy.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-remote-rate.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-vt-drain.py'), str(target)], check=True)
        for _ in range(2):
            subprocess.run(['python3', str(root / 'host/overlays/patch-host-data-saver.py'), str(target)], check=True)
        stream = (target / 'src/stream.cpp').read_text()
        assert stream.count('deskport::remotePacketsPerMs(') == 1
        assert stream.count('event<int>("deskport_rtt")->raise(') == 1
        video = (target / 'src/video.cpp').read_text()
        assert video.count('#include "deskport/macos/vtlatency.h"') == 1
        assert video.count('deskport::vt::ScopedFrameCompletion deskport_completion(session.deskport_complete_vt_frames);') == 1
        assert video.count('session->deskport_complete_vt_frames = config.deskport_smart || config.deskport_saver;') == 1
        assert 'const bool smart = config.deskport_smart || config.deskport_saver;' in video
        assert 'if (ctx->config.deskport_smart || deskport::liveSaver.load() == 1)' in video
        assert 'deskport_cadence.emplace(ctx.config.framerate,' in video
        assert 'pos->deskport_timestamp.reset()' in video
        assert video.count('av_packet->pts -= session.deskport_pts_shift;') == 1
        assert video.count('deskport_rate->rtt(now_us / 1000, *rtt);') == 1
        assert video.count('deskport::refineFrameRate(absolute_ms, deskport_last_change_ms,') == 1
        assert video.count('(deskport_saving ? deskport_change_candidate_ms : deskport_last_change_ms) = deskport_captured_ms;') == 1
        assert video.count('deskport::lastEncodedFrame = {') == 2
        assert video.count('deskport_refinement.changed(deskport_change_candidate_ms, deskport::encodedBytes(frame_nr - 1));') == 1
        assert video.count('deskport_refinement.refreshed(deskport::encodedBytes(frame_nr - 1));') == 1
        assert video.count('deskport_change_candidate_ms = deskport_key_ms;') == 1
        assert video.count('pos->deskport_cadence->encoded(deskport::encodedBytes(ctx->frame_nr - 1));') == 1
        assert video.count('deskport_key_gate.allow(deskport_key_ms, deskport_last_rtt)') == 2
        assert video.count('if (deskport_saving && fps <= 1)') == 1
        assert video.count('deskport::saverFrameRate(absolute_ms, deskport_refinement.active(absolute_ms), boost,') == 1
        assert video.count('deskport_saver_gate.admit(deskport_saver_us, requested_idr_frame)') == 1
        assert video.count('deskport_saver_pending = true;') == 1
        assert video.count('cadence.saver = deskport::liveSaver.load() == 1;') == 1
        assert stream.count('deskport::saverMinParity(deskport::liveSaver.load() == 1,') == 1
        assert stream.count('session->deskport_unrecoverable_ms = ') == 1
        assert (target / 'src/rtsp.cpp').read_text().count('deskport::liveSaver = session.deskport_saver') == 1
        assert (target / 'src/nvhttp.cpp').read_text().count('get_arg(args, "deskportSaver", "0")') == 1
        # Execute the actual patched asynchronous capture callback with fake image
        # mailboxes. An idle tick must not republish content, but a new encoder
        # still needs exactly one initial image. The retained image must be
        # released before any capture/display reinitialization waits for owners.
        capture_start = video.index('      std::shared_ptr<platf::img_t> deskport_latest_image;')
        capture_end = video.index('      auto status = disp->capture(', capture_start)
        capture_body = video[capture_start:capture_end]
        reset_position = video.index('      deskport_latest_image.reset();', capture_end)
        assert reset_position < video.index('      if (artificial_reinit', capture_end)
        bootstrap = target / 'cadence-bootstrap.cpp'
        bootstrap.write_text('''#include <cassert>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>
#define KITTY_WHILE_LOOP(init, condition, body) for (init; condition;) body
namespace platf { struct img_t { int serial; }; }
struct Images {
  bool running() const { return true; }
  std::vector<std::shared_ptr<platf::img_t>> delivered;
  void raise(std::shared_ptr<platf::img_t> img) { delivered.push_back(img); }
};
struct Context { std::shared_ptr<Images> images = std::make_shared<Images>(); };
struct Queue {
  std::vector<Context> waiting;
  bool running() const { return true; }
  bool peek() const { return !waiting.empty(); }
  std::optional<Context> pop() {
    auto item = waiting.front(); waiting.erase(waiting.begin()); return item;
  }
};
struct Event { bool set = false; bool peek() const { return set; } };
int main() {
  std::vector<Context> capture_ctxs(1);
  auto capture_ctx_queue = std::make_shared<Queue>();
  auto switch_display_event = std::make_shared<Event>();
  bool artificial_reinit = false;
''' + capture_body + '''
  auto first = std::make_shared<platf::img_t>(platf::img_t{1});
  std::weak_ptr<platf::img_t> old_image = first;
  assert(push_captured_image_callback(std::shared_ptr<platf::img_t>(first), true));
  assert(capture_ctxs.front().images->delivered.size() == 1);
  assert(push_captured_image_callback({}, false));
  assert(capture_ctxs.front().images->delivered.size() == 1);
  assert(deskport_latest_image == first);
  capture_ctx_queue->waiting.emplace_back();
  assert(push_captured_image_callback({}, false));
  assert(capture_ctxs.size() == 2);
  assert(capture_ctxs[0].images->delivered.size() == 1);
  assert(capture_ctxs[1].images->delivered.size() == 1);
  assert(capture_ctxs[1].images->delivered.front() == first);
  auto second = std::make_shared<platf::img_t>(platf::img_t{2});
  std::weak_ptr<platf::img_t> retained_image = second;
  assert(push_captured_image_callback(std::shared_ptr<platf::img_t>(second), true));
  assert(deskport_latest_image == second);
  for (auto &ctx : capture_ctxs) ctx.images->delivered.clear();
  first.reset();
  assert(old_image.expired());
  second.reset();
  assert(!retained_image.expired());
  switch_display_event->set = true;
  assert(!push_captured_image_callback({}, false) && artificial_reinit);
  deskport_latest_image.reset();
  assert(retained_image.expired());
}
''')
        subprocess.run(['c++', '-std=c++23', '-pthread', str(bootstrap), '-o', str(target / 'bootstrap-test')], check=True)
        subprocess.run([str(target / 'bootstrap-test')], check=True)
        # Compile the actual upstream mailbox template: the older bool event
        # cannot instantiate timed pop(), while optional<int> works on both.
        mailbox = target / 'cadence-mailbox.cpp'
        mailbox.write_text('#include "src/thread_safe.h"\n#include <cassert>\n'
            'int main() { safe::event_t<int> event; '
            'assert(!event.pop(std::chrono::milliseconds(0))); '
            'event.raise(1); assert(event.pop(std::chrono::milliseconds(0))); '
            'assert(!event.pop(std::chrono::milliseconds(0))); }\n')
        subprocess.run(['c++', '-std=c++23', '-pthread', str(mailbox), '-o', str(target / 'mailbox-test')], check=True)
        subprocess.run([str(target / 'mailbox-test')], check=True)
        print(f'PASS: async/sync/input/PipeWire overlays, idle bootstrap and reinit release: {archive}')
