#!/usr/bin/env python3
"""Video pause overlay on both repository-owned upstream revisions.

A paused viewer must not advance the host frame index: the encoders skip
encoding while paused and resume with a key frame. No live session is used.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]


def definition(source, signature):
    # The encoder definitions do not contain brace literals; use the existing
    # lexer so braces in comments cannot truncate a production function.
    import runpy
    return runpy.run_path(str(root / 'tests/test-host-encoder-replay.py'))['definition'](source, signature)


def patch(target, name, *extra):
    subprocess.run([sys.executable, str(root / f'host/overlays/patch-host-{name}.py'),
                    str(target), *extra], check=True)


def compile_probe(target, source):
    probe = target / 'pause-probe.cpp'
    probe.write_text(source)
    executable = target / 'pause-probe'
    subprocess.run(['c++', '-std=c++23', '-pthread', '-I', str(target / 'src'),
                    str(probe), '-o', str(executable)], check=True, timeout=120)
    subprocess.run([str(executable)], check=True, timeout=15)


# Test adapters replace the codec, display and packet sink only. The complete
# asynchronous encoder and synchronous callback below are extracted after every
# downstream overlay; hooks observe the real pause wake and inject control races.
# No display, input or network is opened.
ADAPTERS = r'''
#include "thread_safe.h"
#include "deskport/common/framecadence.h"
#include "deskport/common/encoderpolicy.h"
#include "deskport/common/ratecontrol.h"
#include <cassert>
#include <thread>
#include <functional>
using namespace std::literals;
using Clock = std::chrono::steady_clock;
struct Log { template<class T> Log &operator<<(const T &) { return *this; } };
#define BOOST_LOG(level) Log{}
namespace mail {
constexpr auto shutdown = "shutdown", idr = "idr", invalidate_ref_frames = "invalidate";
}
namespace platf {
struct img_t { int serial = -1; std::optional<Clock::time_point> frame_timestamp; };
struct encode_device_t {};
struct display_t {
  int width = 64, height = 64;
  std::shared_ptr<img_t> alloc_img() { return std::make_shared<img_t>(); }
  int dummy_img(img_t *) { return 0; }
};
inline void enable_mouse_keys() {}
}
namespace config { inline struct { int max_bitrate = 0; double minimum_fps_target = 0; } video; }
namespace deskport_admission { /* ADMISSION */ }
struct config_t { int framerate = 60, bitrate = 10000; bool deskport_smart = true, deskport_saver = true; };
struct Record { int picture, frame; bool key; std::optional<Clock::time_point> timestamp; };
struct State {
  std::mutex mutex;
  std::vector<Record> packets;
  std::atomic<int> conversions {0}, requests {0};
  std::atomic<bool> created {false};
  std::atomic<deskport::CadenceWake *> pause_wake {nullptr};
  std::function<void()> in_convert;
  std::function<void()> before_pause_snapshot;
};
inline State *active = nullptr;
struct encode_session_t {
  State *state = active;
  int picture = -1;
  bool key = false;
  virtual ~encode_session_t() = default;
  int convert(platf::img_t &img) {
    ++state->conversions; picture = img.serial;
    if (state->in_convert) { auto callback = std::move(state->in_convert); callback(); }
    return 0;
  }
  void request_idr_frame() { key = true; ++state->requests; }
  void request_normal_frame() { key = false; }
  void invalidate_ref_frames(int64_t, int64_t) { request_idr_frame(); }
  bool deskport_set_bitrate(int, bool, bool) { return false; }
};
struct avcodec_encode_session_t : encode_session_t { struct { void captured() {} } deskport_quality; };
struct encoder_t { int flags = 0; };
constexpr int ASYNC_TEARDOWN = 1;
using packet_t = int;
using img_event_t = std::shared_ptr<safe::event_t<std::shared_ptr<platf::img_t>>>;
using Packets = safe::mail_raw_t::queue_t<packet_t>;
namespace deskport::browser {
inline Packets video_queue(void *) { return {}; }
}
std::unique_ptr<encode_session_t> make_encode_session(platf::display_t *, const encoder_t &, config_t,
    int, int, std::unique_ptr<platf::encode_device_t>) {
  auto result = std::make_unique<avcodec_encode_session_t>();
  active->created = true;
  return result;
}
int encode(int frame, encode_session_t &session, Packets, void *, std::optional<Clock::time_point> stamp) {
  std::lock_guard lock {session.state->mutex};
  session.state->packets.push_back({session.picture, frame, session.key, stamp});
  return 0;
}
std::mutex deskport_rate_memory_mutex;
deskport::RateMemory deskport_rate_memory;
std::int64_t deskport_wall_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
/* ASYNC */
struct sync_session_ctx_t {
  safe::mail_t deskport_mail = std::make_shared<safe::mail_raw_t>();
  safe::signal_t joined;
  safe::signal_t *join_event = &joined;
  safe::mail_raw_t::event_t<bool> shutdown_event = deskport_mail->event<bool>(mail::shutdown);
  safe::mail_raw_t::event_t<bool> idr_events = deskport_mail->event<bool>(mail::idr);
  config_t config;
  int frame_nr = 1;
  Packets packets;
  void *channel_data = nullptr;
};
/* SYNC_SESSION */
bool sync_step(std::vector<sync_session_t> &synced_sessions,
               std::vector<std::unique_ptr<sync_session_ctx_t>> &synced_session_ctxs,
               std::shared_ptr<platf::img_t> &deskport_sync_image,
               std::shared_ptr<platf::img_t> img, bool frame_captured) {
  /* SYNC_LOOP */
  return true;
}
void set_pause(bool paused) {
  // Exact mutation used by the authenticated handler, including quick toggles.
  std::lock_guard lock {deskport_admission::video_mutex};
  /* CONTROL */
}
template<class Predicate> void await(Predicate predicate) {
  const auto end = Clock::now() + 2s;
  while (!predicate()) { assert(Clock::now() < end); std::this_thread::sleep_for(1ms); }
}
std::vector<Record> records(State &state) {
  std::lock_guard lock {state.mutex}; return state.packets;
}
void async_tests() {
  for (bool smart : {false, true}) {
    deskport_admission::reset_video();
    deskport::liveSaver = smart ? 1 : 0;
    set_pause(true);
    State state; active = &state;
    auto bus = std::make_shared<safe::mail_raw_t>();
    auto stop = bus->event<bool>(mail::shutdown);
    auto images = std::make_shared<img_event_t::element_type>();
    safe::signal_t reinit;
    config_t config; config.deskport_smart = config.deskport_saver = smart;
    int frame = 1;
    std::weak_ptr<platf::img_t> latest;
    std::thread worker([&] { encode_run(frame, bus, images, config,
        std::make_shared<platf::display_t>(), std::make_unique<platf::encode_device_t>(),
        reinit, encoder_t{}, nullptr); });
    await([&] { return state.created.load(); });
    await([&] { return bool(state.pause_wake.load()); });
    auto pause_wake = state.pause_wake.load();
    const auto pause_generation = pause_wake->generation();
    for (int serial : {1, 2, 3}) {
      auto img = std::make_shared<platf::img_t>(); img->serial = serial;
      img->frame_timestamp = Clock::now(); latest = img; images->raise(std::move(img));
      assert(pause_wake->generation() == pause_generation);
    }
    bus->event<deskport::ActivityBoost>("deskport_activity")->raise(deskport::ActivityBoost{});
    bus->event<int>("deskport_rtt")->raise(10);
    bus->event<int>("deskport_congestion")->raise(1);
    bus->event<int>("deskport_sent_kbps")->raise(100);
    bus->event<bool>(mail::idr)->raise(true);
    bus->event<std::pair<int64_t, int64_t>>(mail::invalidate_ref_frames)->raise(std::pair<int64_t, int64_t>{1, 2});
    assert(pause_wake->generation() == pause_generation);
    std::this_thread::sleep_for(25ms);
    assert(state.conversions == 0 && records(state).empty());
    set_pause(false);
    assert(pause_wake->generation() > pause_generation);
    await([&] { return !records(state).empty(); });
    auto packets = records(state);
    assert(packets.front().picture == 3 && packets.front().key && packets.front().frame == 1);
    // The final image is static: resume must encode the retained input at once.
    set_pause(true);
    auto before = records(state).size();
    auto requests = state.requests.load();
    set_pause(false);
    await([&] {
      auto p = records(state);
      return state.requests > requests && std::any_of(p.begin() + before, p.end(),
          [](auto packet) { return packet.picture == 3 && packet.key; });
    });
    // Toggle inside conversion, so the loop cannot observe the paused flag.
    set_pause(true);
    state.in_convert = [] { set_pause(true); set_pause(false); };
    auto img = std::make_shared<platf::img_t>(); img->serial = 4; images->raise(img); img.reset();
    set_pause(false);
    await([&] { auto p = records(state); return !p.empty() && p.back().picture == 4; });
    packets = records(state);
    assert(std::find_if(packets.begin(), packets.end(), [](auto p) { return p.picture == 4; })->key);
    set_pause(true);
    reinit.raise(true); // also before the first frame in a separate case below
    worker.join();
    assert(images->running());
    packets = records(state);
    for (std::size_t i = 0; i < packets.size(); ++i) assert(packets[i].frame == int(i + 1));
    // The encoder's fallback slot and subscriptions must not retain a display.
    images->pop(0ms); assert(latest.expired());
  }
  // Paused before first frame: neither dummy conversion nor reinit may stall.
  for (int reason : {0, 1, 2}) {
    deskport_admission::reset_video(); set_pause(true);
    State state; active = &state;
    auto bus = std::make_shared<safe::mail_raw_t>();
    auto stop = bus->event<bool>(mail::shutdown);
    auto images = std::make_shared<img_event_t::element_type>();
    safe::signal_t reinit; int frame = 1;
    std::thread worker([&] { encode_run(frame, bus, images, config_t{},
        std::make_shared<platf::display_t>(), std::make_unique<platf::encode_device_t>(), reinit, encoder_t{}, nullptr); });
    await([&] { return state.created.load(); });
    if (reason == 0) stop->raise(true);
    if (reason == 1) reinit.raise(true);
    if (reason == 2) images->stop();
    worker.join(); assert(state.conversions == 0 && frame == 1);
  }
  // Controls can arrive after observing pause but before sampling its wake.
  // The second lifecycle/paused check must handle them without another event.
  for (int reason : {0, 1, 2, 3}) {
    deskport_admission::reset_video(); set_pause(true);
    State state; active = &state;
    auto bus = std::make_shared<safe::mail_raw_t>();
    auto stop = bus->event<bool>(mail::shutdown);
    auto images = std::make_shared<img_event_t::element_type>();
    auto img = std::make_shared<platf::img_t>(); img->serial = 99; images->raise(img);
    safe::signal_t reinit; int frame = 1;
    state.before_pause_snapshot = [&] {
      if (reason == 0) set_pause(false);
      if (reason == 1) stop->raise(true);
      if (reason == 2) reinit.raise(true);
      if (reason == 3) images->stop();
    };
    std::thread worker([&] { encode_run(frame, bus, images, config_t{},
        std::make_shared<platf::display_t>(), std::make_unique<platf::encode_device_t>(), reinit, encoder_t{}, nullptr); });
    if (reason == 0) {
      await([&] { return !records(state).empty(); });
      auto packets = records(state);
      assert(packets.front().picture == 99 && packets.front().key && packets.front().frame == 1);
      stop->raise(true);
    }
    worker.join();
    if (reason != 0) assert(state.conversions == 0 && frame == 1);
  }
  // An unpaused reinit before capture still sends exactly the original dummy.
  deskport_admission::reset_video();
  State state; active = &state;
  auto bus = std::make_shared<safe::mail_raw_t>();
  auto images = std::make_shared<img_event_t::element_type>();
  safe::signal_t reinit; reinit.raise(true); int frame = 1;
  encode_run(frame, bus, images, config_t{}, std::make_shared<platf::display_t>(),
      std::make_unique<platf::encode_device_t>(), reinit, encoder_t{}, nullptr);
  assert(frame == 2 && state.conversions == 1 && records(state).front().picture == -1);
}
void sync_tests() {
  for (bool smart : {false, true}) {
    deskport_admission::reset_video(); deskport::liveSaver = smart ? 1 : 0;
    std::vector<std::unique_ptr<sync_session_ctx_t>> contexts;
    std::vector<sync_session_t> sessions;
    State states[2];
    for (int i : {0, 1}) {
      contexts.push_back(std::make_unique<sync_session_ctx_t>());
      contexts.back()->config.deskport_smart = smart;
      active = &states[i];
      sync_session_t session;
      session.ctx = contexts.back().get(); session.session = std::make_unique<avcodec_encode_session_t>();
      session.deskport_activity = session.ctx->deskport_mail->event<deskport::ActivityBoost>("deskport_activity");
      session.deskport_congestion = session.ctx->deskport_mail->event<int>("deskport_congestion");
      session.deskport_cadence.emplace(60); sessions.push_back(std::move(session));
    }
    auto current = std::make_shared<platf::img_t>();
    set_pause(true);
    for (int serial : {11, 12, 13}) {
      auto img = std::make_shared<platf::img_t>(); img->serial = serial; img->frame_timestamp = Clock::now();
      assert(sync_step(sessions, contexts, current, img, true));
    }
    for (auto &state : states) assert(state.conversions == 0 && records(state).empty());
    auto stamp = current->frame_timestamp;
    current->frame_timestamp.reset(); // pull_free_image callback clears metadata on idle ticks
    set_pause(false);
    assert(sync_step(sessions, contexts, current, {}, false));
    for (auto &state : states) {
      auto p = records(state); assert(p.size() == 1 && p.back().picture == 13 && p.back().key && p.back().timestamp == stamp);
    }
    set_pause(true); set_pause(false);
    assert(sync_step(sessions, contexts, current, {}, false));
    for (auto &state : states) {
      assert(records(state).size() == 2 && records(state).back().key);
      assert(state.conversions == 1); // resume can reuse an already converted image
    }
    if (!smart) {
      deskport::pipewireControlTicks = true;
      assert(sync_step(sessions, contexts, current, {}, false));
      for (auto &state : states) assert(records(state).size() == 2);
      deskport::pipewireControlTicks = false;
      // Other backends still get their native manual duplicate cadence.
      assert(sync_step(sessions, contexts, current, {}, false));
      for (auto &state : states) assert(records(state).size() == 3);
    }
    // Shutdown wins over pause, independently for each session.
    set_pause(true); contexts.front()->shutdown_event->raise(true);
    assert(sync_step(sessions, contexts, current, {}, false) && sessions.size() == 1);
    contexts.front()->shutdown_event->raise(true);
    assert(!sync_step(sessions, contexts, current, {}, false) && sessions.empty());
  }
}
int main() { async_tests(); sync_tests(); }
'''


def inspect_and_probe(target):
    video = (target / 'src/video.cpp').read_text()
    http = (target / 'src/confighttp.cpp').read_text()
    rtsp = (target / 'src/rtsp.h').read_text()
    async_code = definition(video, '  void encode_run(')
    sync_code = definition(video, '  encode_e encode_run_sync(')
    assert 'sleep_for(10ms)' not in async_code
    assert 'images->pop(0ms)' in async_code
    assert 'video_resume_generation' in async_code and 'video_resume_generation' in sync_code
    assert async_code.index('video_resume_generation.load()') < async_code.index('make_encode_session(')
    assert async_code.index('if (deskport_admission::video_encode_paused) {') < async_code.index('session->convert(')
    assert sync_code.index('if (deskport_admission::video_encode_paused) {') < sync_code.index('pos->session->convert(')
    assert 'deskport_pause_image' in async_code and 'else img = deskport_sync_image;' in sync_code
    assert 'images->deskport_watch(deskport_pause_wake, true);' in async_code
    assert sync_code.count('pos->deskport_pause_pending = false;') == 1
    assert sync_code.count('pos->deskport_picture_ready = true;') == 1
    assert http.index('++deskport_admission::video_resume_generation;') < http.index('video_encode_paused = paused;')
    assert 'deskport::cadenceControls.notify();' in http
    assert 'packet->deskport_encoded_at < deskport_admission::video_resume_time' in (target / 'src/stream.cpp').read_text()
    admission = re.search(r'namespace deskport_admission \{(.*?)\n\}\n', rtsp, re.S).group(1)
    control_begin = http.index('            if (deskport_admission::video_paused && !paused) {')
    control_end = http.index('            output["status"] = true;', control_begin)
    callback_start = sync_code.index('        KITTY_WHILE_LOOP(')
    callback_end = sync_code.index('\n\n        if (switch_display_event->peek())', callback_start)
    callback = sync_code[callback_start:callback_end]
    # Keep the actual retained-image fallback too, before running the body.
    retained_begin = sync_code.index('        if (frame_captured && img) deskport_sync_image = img;')
    retained_end = sync_code.index('        while (encode_session_ctx_queue.peek())', retained_begin)
    callback = sync_code[retained_begin:retained_end] + callback
    session_struct = definition(video, '  struct sync_session_t {') + ';'
    wake_hook = '    deskport::cadenceControls.watch(deskport_pause_wake);'
    assert async_code.count(wake_hook) == 1
    async_code = async_code.replace(wake_hook, wake_hook + '\n    active->pause_wake.store(deskport_pause_wake.get());')
    snapshot_hook = '        const auto deskport_pause_generation = deskport_pause_wake->generation();'
    assert async_code.count(snapshot_hook) == 1
    async_code = async_code.replace(snapshot_hook, '''        if (active->before_pause_snapshot) {
          auto callback = std::move(active->before_pause_snapshot);
          callback();
        }
''' + snapshot_hook)
    source = (ADAPTERS.replace('/* ADMISSION */', admission)
              .replace('/* ASYNC */', async_code)
              .replace('/* SYNC_SESSION */', session_struct)
              .replace('/* SYNC_LOOP */', callback)
              .replace('/* CONTROL */', http[control_begin:control_end]))
    compile_probe(target, source)


# Exercise the production platform order, including downstream browser packet
# routing and macOS shutdown fences, on both repository-owned host revisions.
for archive, platform in (('sunshine-nix.tar.gz', 'linux-nix'),
                          ('sunshine.tar.gz', 'linux-portable'),
                          ('sunshine.tar.gz', 'macos'),
                          ('sunshine.tar.gz', 'windows')):
    with tempfile.TemporaryDirectory(prefix='deskport-video-pause-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as stream:
            stream.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(path for path in target.iterdir() if (path / 'src').exists())
        subprocess.run([sys.executable, str(root / 'scripts/apply-host-patches.py'),
                        '--platform', platform, str(target)], check=True)
        # A complete later overlay can overlap the pause diff. Reversibility and
        # idempotence are checked on the precise pre-downstream tree below.
        inspect_and_probe(target)
        print(f'PASS: full-series pause/resume encoder probes: {archive}, {platform}')

for archive in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-video-pause-revert-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / archive) as stream:
            stream.extractall(target, filter='data')
        if not (target / 'src').exists():
            target = next(path for path in target.iterdir() if (path / 'src').exists())
        for name in ('smart-stream', 'session-settings', 'session-takeover', 'linux-display',
                     'input-activity', 'sync-cadence', 'linux-cadence', 'pipewire-memory',
                     'encoder-policy', 'remote-rate', 'data-saver'):
            patch(target, name)
        before = {path: path.read_bytes() for path in (target / 'src').glob('*.[ch]*')}
        patch(target, 'video-pause')
        applied = {path: path.read_bytes() for path in before}
        patch(target, 'video-pause')
        assert applied == {path: path.read_bytes() for path in before}
        patch(target, 'video-pause', '--revert')
        assert before == {path: path.read_bytes() for path in before}
        print(f'PASS: pause overlay idempotence and exact reversal: {archive}')
