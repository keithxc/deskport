#!/usr/bin/env python3
"""Fence macOS native packet senders before session retirement.

Validate all pinned anchors before writing. Other platforms retain their pinned
queue and broadcast behavior. Reapplying this overlay is a no-op.
"""
from pathlib import Path
import sys


def apply(source):
    """Fence broadcast users before session destruction and synchronize mail."""
    changes = {}

    def replace(relative, old, new, count=1):
        path = source / relative
        text = changes.get(path, path.read_text())
        if text.count(new) == count:
            return
        if text.count(old) != count:
            raise SystemExit(f'Unsupported macOS shutdown source: {relative}: {old[:60]!r}')
        changes[path] = text.replace(old, new)

    # Each sender holds its mutex from dequeue through its final session use.
    # Retirement takes both mutexes after the producers have joined, then removes
    # queued packets for that session. No raw pointer can remain outside a fence.
    replace('src/stream.cpp', '  int start_broadcast(broadcast_ctx_t &ctx);', """  // Separate locks preserve audio/video concurrency while fencing retirement.
  static std::mutex deskport_video_packet_mutex;
  static std::mutex deskport_audio_packet_mutex;

  void deskport_retire_packets(session_t &session) {
    std::scoped_lock lock {deskport_video_packet_mutex, deskport_audio_packet_mutex};
    mail::man->queue<video::packet_t>(mail::video_packets)->erase_if(
      [&session](const auto &packet) { return packet->channel_data == &session; });
    mail::man->queue<audio::packet_t>(mail::audio_packets)->erase_if(
      [&session](const auto &packet) { return std::get<0>(packet) == &session; });
  }

  int start_broadcast(broadcast_ctx_t &ctx);""")
    for kind in ('video', 'audio'):
        # Distinct anchors surround the only native packet consumer loops.
        marker = ('    auto ratecontrol_next_frame_start = std::chrono::steady_clock::now();'
                  if kind == 'video' else
                  '    platf::adjust_thread_priority(platf::thread_priority_e::high);')
        old = marker + '\n\n    while (auto packet = packets->pop()) {'
        new = marker + f"""

    for (;;) {{
      std::unique_lock packet_lock {{deskport_{kind}_packet_mutex, std::defer_lock}};
      auto packet = packets->pop(packet_lock);
      if (!packet) {{
        break;
      }}"""
        replace('src/stream.cpp', old, new)
    replace('src/stream.cpp', '      session.controlEnd.view();', """      session.controlEnd.view();
      // Fence active packets and discard this session's queued packets first.
      deskport_retire_packets(session);
      // Final-owner broadcast joins remain inside the existing watchdog.
      session.broadcast_ref = {};""")
    # Keep the same stopped channels alive until all their consumers have left.
    # Otherwise a late lookup can create a different channel during teardown.
    replace('src/stream.cpp', '''    video_packets.reset();
    audio_packets.reset();

''', '''    // Keep stopped packet channels alive until every sender has joined.

''')

    # Reads must use the same mutex as raise/stop/reset and queue mutation.
    replace('src/thread_safe.h', '''      return _continue && (bool) _status;''', '''      std::lock_guard lg {_lock};
      return _continue && (bool) _status;''')
    replace('src/thread_safe.h', '''      return _continue && !_queue.empty();''', '''      std::lock_guard lg {_lock};
      return _continue && !_queue.empty();''')
    replace('src/thread_safe.h', '''    [[nodiscard]] bool running() const {
      return _continue;
    }''', '''    [[nodiscard]] bool running() const {
      std::lock_guard lg {_lock};
      return _continue;
    }''', 2)
    # Only these two classes expose const running().
    replace('src/thread_safe.h', '''    std::condition_variable _cv;
    std::mutex _lock;
  };''', '''    std::condition_variable _cv;
    mutable std::mutex _lock;
  };''')
    replace('src/thread_safe.h', '''    std::mutex _lock;
    std::condition_variable _cv;

    std::vector<T> _queue;''', '''    mutable std::mutex _lock;
    std::condition_variable _cv;

    std::vector<T> _queue;''')

    # Wait without the external consumer lock. Acquire it before dequeuing and
    # recheck under the queue lock, matching retirement's consumer->queue order.
    replace('src/thread_safe.h', '    std::vector<T> &unsafe() {', """    template<class Predicate>
    void erase_if(Predicate predicate) {
      std::lock_guard lock {_lock};
      std::erase_if(_queue, std::move(predicate));
    }

    status_t pop(std::unique_lock<std::mutex> &consumer_lock) {
      std::unique_lock queue_lock {_lock};
      for (;;) {
        _cv.wait(queue_lock, [this] { return !_continue || !_queue.empty(); });
        if (!_continue) {
          return util::false_v<status_t>;
        }
        queue_lock.unlock();
        consumer_lock.lock();
        queue_lock.lock();
        if (_continue && !_queue.empty()) {
          auto value = std::move(_queue.front());
          _queue.erase(_queue.begin());
          return value;
        }
        consumer_lock.unlock();
      }
    }

    std::vector<T> &unsafe() {""")

    for kind in ('event', 'queue'):
        replace('src/thread_safe.h', f'''        return lock<{kind}_t<T>>(it->second);''', f'''        if (auto post = lock<{kind}_t<T>>(it->second)) {{
          return post;
        }}
        // The final owner may be in post_t's destructor, waiting for mutex.
        // An expired entry must be replaced instead of returning a null post.
        id_to_post.erase(it);''')

    # Validate all anchors before mutating the prepared source.
    for path, text in changes.items():
        path.write_text(text)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: patch-host-macos-shutdown.py SOURCE')
    apply(Path(sys.argv[1]))
