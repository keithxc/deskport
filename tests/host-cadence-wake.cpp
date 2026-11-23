// Exercise the final generated encoder/PipeWire waits and both upstream
// mailbox templates. No capture backend, session or injected input is used.
#include "src/thread_safe.h"
#include "src/deskport/common/framecadence.h"
#include <cassert>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
namespace platf {
struct img_t {};
enum class capture_e { ok, interrupted };
}

struct SyncSessionMail {
#include "cadence-session-mail.inc"
};

SyncSessionMail syncSessionMail(const safe::mail_t& mail) {
    struct { safe::mail_t deskport_mail; } ctx {mail};
    SyncSessionMail encode_session;
#include "cadence-syncmail.inc"
    return encode_session;
}

struct EncoderWait {
    std::shared_ptr<safe::event_t<int>> images = std::make_shared<safe::event_t<int>>();
    std::shared_ptr<safe::event_t<int>> shutdown_event = std::make_shared<safe::event_t<int>>();
    safe::signal_t reinit_event;
    safe::event_t<int> activity, idr;
    std::shared_ptr<deskport::CadenceWake> deskport_wake = std::make_shared<deskport::CadenceWake>();
    Clock::time_point smart_start = Clock::now(), last_encoded = smart_start;
    std::chrono::duration<double, std::milli> max_frametime {5000};
    bool deskport_saving = true, deskport_saver_pending = false;
    int frame_nr = 2;
    deskport::SaverGate deskport_saver_gate;
    deskport::ActivityBoost boost;
    std::atomic<bool> control_changed {false};

    EncoderWait() {
        images->deskport_watch(deskport_wake);
        shutdown_event->deskport_watch(deskport_wake);
        reinit_event.deskport_watch(deskport_wake);
        activity.deskport_watch(deskport_wake);
        idr.deskport_watch(deskport_wake);
        deskport::cadenceControls.watch(deskport_wake);
    }
    void wait(bool control = false) {
        while (true) {
            const auto deskport_generation = deskport_wake->generation();
            if (images->peek() || shutdown_event->peek() || reinit_event.peek() ||
                !images->running() || activity.peek() || idr.peek() ||
                (control && control_changed.load())) return;
            const bool requested_idr_frame = false;
#include "cadence-wait.inc"
            return;
        }
    }
};

struct PipeWait {
    struct Pipe {
        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false;
        auto& frame_mutex() { return mutex; }
        auto& frame_cv() { return cv; }
        bool is_frame_ready() const { return ready; }
        void set_frame_ready(bool value) { ready = value; }
    } pipewire;
    struct State { std::atomic<bool> stream_dead {false}; };
    std::shared_ptr<State> shared_state = std::make_shared<State>();
    bool deskport_smart = true;
    std::uint64_t deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
    std::uint64_t deskport_control_generation = deskport::captureWake->generation();
#include "cadence-pipewait.inc"
    struct {
        void first_point(Clock::time_point) {}
        void second_point_now_and_log() {}
    } sleep_overshoot_logger;

    template<class Push, class BeforeWait>
    platf::capture_e pace(std::chrono::milliseconds duration,
                         Push push_captured_image_cb, BeforeWait before_wait) {
        auto pull_free_image_cb = [](std::shared_ptr<platf::img_t>& img) {
            img = std::make_shared<platf::img_t>();
            return true;
        };
#include "cadence-push.inc"
        auto next_frame = Clock::now() + duration;
        auto now = Clock::now();
        // Model the preceding capture callback and the exact vulnerable gap.
        if (!deskport_push({}, false)) return platf::capture_e::ok;
        before_wait();
        while (true) {
#include "cadence-pace.inc"
            break;
        }
        // Real-image delivery happens only after the generated pacing block.
        deskport_push(std::make_shared<platf::img_t>(), true);
        return platf::capture_e::ok;
    }
};

template<class Wait, class Publish>
long long wakeAfterStart(Wait wait, Publish publish) {
    std::promise<void> started, finished;
    auto done = finished.get_future();
    std::thread thread([&] { started.set_value(); wait(); finished.set_value(); });
    started.get_future().wait();
    // Let the consumer enter a real wait, in addition to the deterministic
    // notification-before-wait check below. This delay is test setup only.
    std::this_thread::sleep_for(5ms);
    const auto begin = Clock::now();
    publish();
    const bool prompt = done.wait_for(100ms) == std::future_status::ready;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count();
    thread.join();
    assert(prompt); // Allows scheduler noise; production has no polling interval.
    return us;
}

int main() {
    // A notification between checking a mailbox and entering the CV must be
    // observed. An unrelated/expired listener never extends encoder lifetime.
    deskport::CadenceWake wake;
    const auto before = wake.generation();
    wake.notify();
    assert(wake.waitUntil(before, Clock::now() + 5s));
    auto observed = std::make_shared<deskport::CadenceWake>();
    safe::queue_t<int> queue;
    queue.deskport_watch(observed);
    auto serial = observed->generation();
    queue.raise(42);
    assert(observed->generation() != serial && queue.pop(0ms) == 42);
    serial = observed->generation();
    queue.stop();
    assert(observed->generation() != serial);
    safe::event_t<int> stop_only;
    stop_only.deskport_watch(observed, true);
    serial = observed->generation();
    stop_only.raise(1);
    assert(observed->generation() == serial);
    stop_only.stop();
    assert(observed->generation() != serial);

    // mail_raw_t stores weak channels, so test the generated session members
    // and subscriptions with the actual upstream mailbox on both pins.
    auto mail = std::make_shared<safe::mail_raw_t>();
    std::weak_ptr<safe::post_t<safe::event_t<deskport::ActivityBoost>>> weak_activity;
    {
        auto session = syncSessionMail(mail);
        weak_activity = session.deskport_activity;
        serial = deskport::captureWake->generation();
        mail->event<deskport::ActivityBoost>("deskport_activity")->raise(deskport::ActivityBoost{2000, 60});
        mail->event<int>("deskport_congestion")->raise(1);
        assert(deskport::captureWake->generation() != serial);
        assert(session.deskport_activity->pop(0ms)->untilMs == 2000);
        assert(session.deskport_congestion->pop(0ms) == 1);
    }
    assert(weak_activity.expired()); // No channel/listener retains the session.

    std::vector<long long> latency;
    for (int kind = 0; kind < 7; ++kind) {
        EncoderWait encoder;
        latency.push_back(wakeAfterStart([&] { encoder.wait(kind == 6); }, [&] {
            switch (kind) {
                case 0: encoder.images->raise(7); break;
                case 1: encoder.activity.raise(1); break;
                case 2: encoder.idr.raise(1); break;
                case 3: encoder.reinit_event.raise(true); break;
                case 4: encoder.shutdown_event->raise(1); break;
                case 5: encoder.images->stop(); break;
                case 6: encoder.control_changed = true; deskport::cadenceControls.notify(); break;
            }
        }));
    }
    // The last saver picture must get its slot without another capture event.
    EncoderWait pending;
    pending.deskport_saver_pending = true;
    assert(pending.deskport_saver_gate.admit(0, false));
    const auto start = Clock::now();
    pending.wait();
    const auto elapsed = Clock::now() - start;
    assert(elapsed >= 100ms && elapsed < 1s);
    deskport::FrameCadence cadence(60);
    cadence.saver = true;
    assert(cadence.admit(1000000, true, false));
    cadence.encoded(450); // No refinement tail for a small initial update.
    assert(!cadence.admit(1010000, true, false));
    assert(cadence.readyUs(1010000) == 1125000);
    assert(cadence.admit(1125000, false, false));
    cadence.encoded(450);
    assert(cadence.readyUs(1125000) == 6125000);
    cadence.boost = {2000, 60};
    assert(cadence.readyUs(1125000) == 1375000); // Actual input changes idle floor.
    deskport::SaverGate gate;
    assert(gate.admit(1000000, false, true));
    assert(gate.readyUs(1010000, true) == 1062500);
    assert(gate.readyUs(1020000, false) == 1062500); // Expiry cannot postpone.
    assert(gate.admit(1062500, false, false));
    assert(gate.readyUs(1070000, false) == 1187500);
    assert(gate.readyUs(1080000, true) == 1125000); // New input can accelerate.
    assert(gate.readyUs(1090000, false) == 1125000);
    deskport::SaverGate expired;
    assert(expired.admit(1000000, false, true));
    // Preserve the slot offered by admit itself, even when there was no
    // intermediate readyUs query before the interactive interval expired.
    assert(expired.admit(1070000, false, false));

    PipeWait pipe;
    pipe.pipewire.ready = true; // A frame already ready needs no further signal.
    assert(pipe.wait_for_frame(Clock::now() + 5s));
    assert(!pipe.pipewire.ready);
    deskport::captureWake->notify(); // Control-only wake produces a lifecycle tick.
    assert(!pipe.wait_for_frame(Clock::now() + 5s));
    // A real timeout callback services this control before the next snapshot.
    pipe.deskport_control_generation = deskport::captureWake->generation();
    pipe.deskport_capture_generation = deskport::pipewireFrameOrControl->generation();
    const auto quiet = Clock::now();
    assert(!pipe.wait_for_frame(quiet + 20ms));
    assert(Clock::now() - quiet >= 15ms); // A consumed wake must not busy-spin.
    latency.push_back(wakeAfterStart([&] {
        assert(pipe.wait_for_frame(Clock::now() + 5s));
    }, [&] {
        { std::lock_guard lock(pipe.pipewire.mutex); pipe.pipewire.ready = true; }
        deskport::pipewireFrameOrControl->notify();
    }));
    latency.push_back(wakeAfterStart([&] {
        assert(!pipe.wait_for_frame(Clock::now() + 5s));
    }, [] { deskport::captureWake->notify(); }));
    latency.push_back(wakeAfterStart([&] {
        assert(!pipe.wait_for_frame(Clock::now() + 5s));
    }, [&] {
        pipe.shared_state->stream_dead = true;
        deskport::captureWake->notify();
    }));
    assert(!pipe.wait_for_frame(Clock::now() + 5s)); // No spin on a dead stream.

    for (bool smart : {false, true}) {
        PipeWait paced;
        paced.deskport_smart = smart;
        int lifecycle = 0, pictures = 0;
        const auto begin = Clock::now();
        paced.pace(80ms, [&](auto, bool captured) {
            if (captured) {
                ++pictures;
                assert(Clock::now() - begin >= 75ms);
            } else if (++lifecycle == 2) {
                // Notification after the last callback survives into pacing.
                assert(Clock::now() - begin < 40ms);
            }
            return true;
        }, [] { deskport::cadenceControls.notify(); });
        assert(lifecycle >= 2 && pictures == 1);

        // Continuous producer frames must neither run control callbacks nor
        // reset the pacing target or publish a picture before its slot.
        std::thread producer([&] {
            for (int i = 0; i < 20; ++i) {
                std::this_thread::sleep_for(5ms);
                deskport::pipewireFrameOrControl->notify();
            }
        });
        const auto flowing = Clock::now();
        int callbacks = 0;
        paced.pace(80ms, [&](auto, bool) { ++callbacks; return true; }, [] {});
        assert(callbacks == 2); // Initial callback and paced picture.
        const auto elapsed = Clock::now() - flowing;
        producer.join();
        assert(elapsed >= 75ms && elapsed < 150ms);
    }
    std::cout << "PASS: generated waits, seven encoder wake sources, pending final picture, "
              << "PipeWire static/input lifecycle; maximum local wake us="
              << *std::max_element(latency.begin(), latency.end()) << '\n';
}
