#!/usr/bin/env python3
"""Exercise both repository-owned upstream revisions without a live session."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    if '--work-dir' in _storage_sys.argv:
        import argparse as _argparse
        import os as _os
        import tempfile as _tempfile
        from pathlib import Path as _Path
        _parser = _argparse.ArgumentParser(description=__doc__)
        _parser.add_argument('--work-dir', type=_Path, required=True)
        _options = _parser.parse_args()
        _storage = _build_storage.Storage()
        _work = _storage.validate(_options.work_dir.expanduser().absolute())
        _paths = {key: str(_storage.validate(_work / name)) for key, name in (
            ('TMPDIR', 'tmp'), ('TMP', 'tmp'), ('TEMP', 'tmp'),
            ('CLANG_MODULE_CACHE_PATH', 'clang-modules'), ('XDG_CACHE_HOME', 'cache'))}
        _os.environ.update(_paths, PYTHONDONTWRITEBYTECODE='1')
        for _value in set(_paths.values()):
            _Path(_value).mkdir(parents=True, exist_ok=True)
        _tempfile.tempdir = None
    else:
        _build_storage.bootstrap()

from pathlib import Path
import shutil, subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-cadence-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as f:
            f.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(p for p in target.iterdir() if (p / 'src').exists())
        common = target / 'src/deskport/common'
        common.mkdir(parents=True, exist_ok=True)
        for name in ('smartstream.h', 'inputactivity.h', 'framecadence.h'):
            shutil.copyfile(root / 'host/common' / name, common / name)
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display', 'encoder-policy', 'vt-drain', 'input-activity', 'mouse-latency', 'sync-cadence', 'linux-cadence', 'pipewire-memory', 'remote-rate'):
            subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
            if name in ('smart-stream', 'input-activity'):
                stage_video = (target / 'src/video.cpp').read_text()
                assert 'sleep_for(2ms)' not in stage_video
                assert 'images->pop(0ms)' in stage_video
                assert stage_video.count('// A producer can notify between this check') == 1
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-mouse-latency.py'), str(target)], check=True)
        input_cpp = (target / 'src/input.cpp').read_text()
        assert 'pushDelayed(std::move(f), 10ms)' not in input_cpp
        assert 'deliver absolute-mode left-button releases without a 10 ms' in input_cpp
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-encoder-policy.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-remote-rate.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-vt-drain.py'), str(target)], check=True)
        for _ in range(2):
            subprocess.run(['python3', str(root / 'host/overlays/patch-host-data-saver.py'), str(target)], check=True)
        # The native capture marker must compose with the saver conversion gate
        # in both orders, including reapplication after all replacements.
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-vt-drain.py'), str(target)], check=True)
        subprocess.run(['python3', str(root / 'host/overlays/patch-host-data-saver.py'), str(target)], check=True)
        stream = (target / 'src/stream.cpp').read_text()
        assert stream.count('deskport::remotePacketsPerMs(') == 1
        assert stream.count('event<int>("deskport_rtt")->raise(') == 1
        video = (target / 'src/video.cpp').read_text()
        assert video.count('#include "deskport/macos/vtlatency.h"') == 1
        assert video.count('deskport::vt::ScopedFrameCompletion deskport_completion(session.deskport_complete_vt_frames);') == 1
        assert video.count('if (deskport_completion.status() != noErr)') == 2
        assert video.count('session->deskport_complete_vt_frames = config.deskport_saver;') == 1
        assert video.count('#include "deskport/macos/vtclock.h"') == 1
        assert video.count('deskport::vt::FrameClock deskport_clock;') == 1
        submit = video.index('session.deskport_clock.submit(')
        assert submit < video.index('avcodec_send_frame(ctx.get(), frame)', submit)
        native_key = video.index('session.deskport_keys.take(av_packet->pts)')
        restore_identity = video.index('session.deskport_clock.take(av_packet->pts)')
        compact_wire = video.index('av_packet->pts -= session.deskport_pts_shift;')
        assert native_key < restore_identity < compact_wire
        assert video.index('av_packet->pts = *identity;', restore_identity) < compact_wire
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
        assert video.count('deskport_saver_gate.admit(deskport_saver_us, requested_idr_frame, deskport_interactive)') == 1
        assert video.count('deskport_saver_pending = true;') == 1
        assert video.count('cadence.saver = deskport::liveSaver.load() == 1;') == 1
        assert stream.count('deskport::saverMinParity(deskport::liveSaver.load() == 1,') == 1
        assert stream.count('session->deskport_unrecoverable_ms = ') == 1
        assert (target / 'src/rtsp.cpp').read_text().count('deskport::liveSaver = session.deskport_saver') == 1
        assert (target / 'src/nvhttp.cpp').read_text().count('get_arg(args, "deskportSaver", "0")') == 1
        # Check the final source after remote-rate, saver and reapplication;
        # checking every stage also guards against reintroducing polling.
        assert 'sleep_for(2ms)' not in video
        assert 'last_activity_fps > 1 ? 2ms : 10ms' not in video
        assert 'images->pop(0ms)' in video
        assert video.count('deskport_wake->waitUntil(deskport_generation, deadline)') == 1
        assert video.count('deskport_wake->waitUntil(deskport_generation, ready)') == 1
        pipewire = (target / 'src/platform/linux/pipewire.cpp').read_text()
        assert 'deskport_smart ? 10ms' not in pipewire
        assert 'deadline = std::min(deadline, deskport::captureDeadline);' in pipewire
        assert 'std::chrono::steady_clock::now() + 100ms' in pipewire
        assert 'sleep_until(next_frame)' not in pipewire
        capture_start = pipewire.index('          // Wake controls promptly')
        loop_top = pipewire.rfind('      while (true)', 0, capture_start)
        assert 'deskport_capture_generation =' not in pipewire[loop_top:capture_start]
        callback_start = pipewire.index('      auto deskport_push =')
        callback_end = pipewire.index('      auto next_frame =', callback_start)
        (target / 'cadence-push.inc').write_text(pipewire[callback_start:callback_end])
        pace_start = pipewire.rfind('        if (next_frame > now)', 0, capture_start)
        pace_end = pipewire.index('          sleep_overshoot_logger.second_point_now_and_log();', pace_start)
        pace_end = pipewire.index('        }', pace_end) + len('        }')
        (target / 'cadence-pace.inc').write_text(pipewire[pace_start:pace_end])
        mail_start = video.index('    // mail_raw_t owns weak references:')
        mail_end = video.index('    encode_session.deskport_cadence.emplace', mail_start)
        (target / 'cadence-syncmail.inc').write_text(video[mail_start:mail_end])
        fields_start = video.index('    safe::mail_raw_t::event_t<deskport::ActivityBoost> deskport_activity;')
        fields_end = video.index('    std::optional<deskport::FrameCadence>', fields_start)
        (target / 'cadence-session-mail.inc').write_text(video[fields_start:fields_end])
        assert 'ctx->deskport_mail->event<int>("deskport_congestion")->pop' not in video
        assert 'ctx->deskport_mail->event<deskport::ActivityBoost>("deskport_activity")->pop' not in video
        wait_start = video.index('      // A producer can notify between this check')
        wait_end = video.index('      // Encode at a minimum FPS', wait_start)
        (target / 'cadence-wait.inc').write_text(video[wait_start:wait_end])
        pipe_start = pipewire.index('    bool wait_for_frame(')
        pipe_end = pipewire.index('    static bool pw_format_supported(', pipe_start)
        (target / 'cadence-pipewait.inc').write_text(pipewire[pipe_start:pipe_end])
        subprocess.run(['c++', '-std=c++23', '-pthread', '-I', str(target), '-I', str(root),
                        str(root / 'tests/host-cadence-wake.cpp'), '-o', str(target / 'wake-test')], check=True)
        subprocess.run([str(target / 'wake-test')], check=True)
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
#include "src/deskport/common/smartstream.h"
#include <iterator>
#include <memory>
#include <optional>
#include <vector>
#define KITTY_WHILE_LOOP(init, condition, body) for (init; condition;) body
namespace platf { struct img_t { int serial; }; }
struct Images {
  void deskport_watch(const std::shared_ptr<deskport::CadenceWake>&, bool = false) {}
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
        # The independent pause overlay must retain the same event-driven waits.
        # Its extra retained-image condition must compose with the saver deadline
        # adjustment when saver is reapplied after the complete chain.
        for name in ('video-pause', 'rate-api', 'data-saver'):
            subprocess.run(['python3', str(root / f'host/overlays/patch-host-{name}.py'), str(target)], check=True)
        final_video = (target / 'src/video.cpp').read_text()
        assert final_video.count('// A producer can notify between this check') == 1
        assert 'sleep_for(2ms)' not in final_video and 'sleep_for(10ms)' not in final_video
        assert 'deskport::cadenceControls.notify();' in (target / 'src/confighttp.cpp').read_text()
        print(f'PASS: async/sync/input/PipeWire waits, pause composition, idle bootstrap and reinit release: {archive}')
