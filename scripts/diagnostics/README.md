# Isolated PipeWire native-buffer tracking

Use this Linux/glibc-only tool on a disposable host process to investigate the
intermittent 32 KiB `protocol-native` allocation reported by heap profiling.
It is not compiled into DeskPort, enabled by a build flag, or preloaded into
installed services. It observes exactly 32 KiB allocations; it is not a general
leak detector. The API wrappers call glibc directly, so do not combine this tool
with another allocator replacement or heap profiler.

```sh
cc -std=gnu11 -O2 -shared -fPIC -pthread \
  scripts/diagnostics/pipewire-buffer-watch.c -o /tmp/pipewire-buffer-watch.so
# Launch only your isolated test executable/configuration:
BUFFER_WATCH_LOG=/tmp/pipewire-buffers.log \
LD_PRELOAD=/tmp/pipewire-buffer-watch.so /path/to/test-host /path/to/test.conf
python3 scripts/diagnostics/summarize-pipewire-buffers.py /tmp/pipewire-buffers.log
```

Use a fresh absolute log path for each process. Keep logs private: stacks contain
local paths and addresses. A normal exit writes `SUMMARY`; a missing summary,
forced kill, forked child, or full tracking table does not establish balanced
lifetimes. Run synthetic tests without external child applications. `LIVE` means
outstanding at the observer's destructor, which can include process-lifetime
caches and is not by itself proof of an unreachable leak.

The table records allocation identity rather than subtracting only totals.
Reallocation is serialized with updates so reuse of an old address by another
thread cannot delete a newer allocation's record. Test both deliberate retention
and complete cleanup before interpreting a clean real-media run.
