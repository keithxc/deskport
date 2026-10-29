# Experimental Seamless Mode

Status: experimental server-side M1/M2 skeleton. Local supervisor hardening is
covered by isolated regressions as of 2026-10-11; the remote client/media path
and stricter native compatibility gates remain incomplete.

Seamless Mode is a second Linux hosting mode in which each remote Wayland
top-level surface is represented by a real top-level window on a Linux Wayland
client. It is not a replacement for the existing Desktop Mode, and it is not a
Wayland protocol proxy.

The initial target is deliberately narrow:

- Linux server and Linux Wayland client;
- one user, one session, one software-rendered application;
- a headless server without GNOME, KDE, a physical display, or a GPU;
- server-side surface rendering followed by DeskPort transport; and
- one local native window for the first rendering proof.

The first application should be a simple terminal using `wl_shm`. Firefox,
VS Code, XWayland, GPU buffers, multiple windows, audio, drag-and-drop, and IME
are later compatibility work.

## Non-regression contract

Desktop Mode remains the default and must retain its current behavior.

Today a native desktop session uses the existing Sunshine/GameStream host,
Moonlight transport, a full display or virtual output, one SDL viewer window,
and the existing input and decoder callbacks. The Qt/QML window is the control
shell; a desktop session worker owns the SDL media window and the legacy media
context. Adaptive display changes the remote desktop size and may restart the
desktop video transport.

That implementation has intentionally global assumptions, including one active
Moonlight callback owner, one `SDL_Window`, one decoder, and one input handler.
It is therefore treated as the Desktop Mode implementation, rather than being
refactored into a premature common base class.

Adding Seamless Mode must not:

- change Desktop Mode negotiation, capture, decoding, input, resize, clipboard,
  window recall, or background-session behavior;
- reinterpret old peer messages or make an old peer enter Seamless Mode;
- change saved Desktop Mode settings or migrate Moonlight credentials;
- make `deskport host run` depend on a new compositor unless
  `--mode seamless` was explicitly selected; or
- turn one remote application into one independent Moonlight session.

Regression checks for Desktop Mode remain release gates even while Seamless Mode
is experimental.

## Existing components and ownership

The following table defines what is reusable and what remains mode-specific.

| Component | Seamless use | Ownership boundary |
| --- | --- | --- |
| Peer binding, pinned certificates, mutual TLS, revocation | Reuse | `PeerManager` remains the authority for an approved peer identity. |
| Session admission, takeover, topology, lifecycle, and lease | Future reuse; not wired for Seamless | The current one-shot preflight does not acquire a lease. A cross-mode exclusive admission gate must cover Desktop, browser, and Seamless before M3. |
| New optional wire constants and validation | Reuse through the shared core | The contract belongs in `shared/deskport-core`; the Git submodule and Nix pin move together. |
| Host child-process supervision and diagnostics patterns | Implemented locally | `SeamlessHostManager` owns one sidecar process, validates its bounded JSON events, and tears down that exact child. It does not own remote admission. |
| Clipboard synchronization | Reuse later at session scope | Clipboard must not block the first surface proof and is not per-window. |
| Browser WebRTC transport | Possible future media building block | It currently carries one full-display H.264 track and is not a per-surface native-client receiver. |
| Sunshine/GameStream capture and Moonlight callbacks | Desktop only for now | They capture a display and expose a single legacy media context. |
| `Session`, `SdlInputHandler`, adaptive display resize | Desktop only | Their single-window and whole-display semantics do not match a window-ID protocol. |
| Existing QML control window and device cards | Unchanged | Seamless Mode must not introduce a second card layout or a giant transparent canvas. |

## Proposed architecture

```text
Linux server                                      Linux Wayland client

remote application                               DeskPort control shell
        |                                                  |
        v                                                  v
Wayland protocol                                  SeamlessClientSession
        |                                          /                 \
        v                                         v                   v
deskport-seamless-host                     control channel       media channel
  QtWaylandCompositor                            |                   |
  XDG shell + surface registry                   +--------+----------+
  local surface capture                                  |
        |                                                 v
        +---- lifecycle/captured frames ----------> SeamlessWindowRegistry
                                                          |
                                                          v
                                                one QWindow per remote
                                                xdg_toplevel surface
```

`deskport-seamless-host` is a separate sidecar under
`host/linux/seamless/`. Keeping it out of the main Qt/QML shell and out of the
patched Sunshine process gives it an explicit lifetime, a private Wayland socket,
and a narrow failure boundary. The implemented `SeamlessHostManager` supervises
one local sidecar, validates its bounded event stream, and stops the exact process
it started. It is not yet bound to an admitted remote Seamless session or lease.

The sidecar owns these server responsibilities:

1. create a private Wayland display and XDG shell;
2. launch an application with only that display in its environment;
3. assign an opaque, session-scoped ID to each top-level;
4. report create, metadata, map/configure, and destroy lifecycle events;
5. observe buffer commits and expose a bounded capture interface; and
6. later accept focus, close, pointer, keyboard, and configure requests.

The sidecar does not own peer credentials, remote admission policy, public
listeners, or long-lived settings. Those remain in the DeskPort host process.
On the current branch that host-side remote work stops at an approved-peer,
one-shot capability preflight; production lifecycle/media dispatch is not wired.

One client Seamless session owns a registry of remote window IDs. It must use one
process per remote host session, not one process per remote window. Every registry
entry eventually owns a native, parentless `QWindow`. The initial software path
uses `QBackingStore` and `QImage`; later decoders or a QRhi renderer can be hidden
behind a surface-renderer interface without changing window lifecycle.

## Why Qt Wayland Compositor

The first server implementation uses Qt Wayland Compositor.

It matches the repository's C++/Qt build and event-loop model, provides public
XDG shell and surface lifecycle APIs, and provides `QWaylandSurfaceGrabber` and
`QWaylandBufferRef` for reading surface content. This makes a small lifecycle and
`wl_shm` capture proof possible without first writing a complete window manager.
It also keeps the initial implementation compatible with software rendering.

The choice is scoped, not irreversible:

- **Weston headless:** Weston is a mature reference compositor and its headless
  and Pixman paths are useful for tests. Stock Weston, however, composites
  surfaces into an output. DeskPort still needs a shell/plugin or libweston
  integration to identify and capture individual application surfaces.
  Libweston's public API is explicitly still evolving, so that integration would
  add a version-sensitive plugin boundary before the first proof.
- **wlroots:** wlroots exposes a headless backend, XDG top-level events, scene
  primitives, and seats directly. It is a strong option if Qt cannot provide the
  required buffer and damage behavior. It also requires DeskPort to implement
  more compositor policy, configure/ack handling, popup rules, rendering, and
  input ownership immediately, while adding a new C library and packaging pin.
- **Qt Wayland Compositor:** it minimizes the first implementation and packaging
  delta and has a public surface-grab API. The tradeoff is that GPU and unusual
  buffer types may require a custom `grabSurface()` implementation later. M2 is
  therefore restricted to software/`wl_shm` validation.

Qt client windows and a server compositor solve different problems. Creating
multiple local Qt windows does not discover or render application surfaces on a
headless server; a server compositor is still required.

Relevant upstream references:

- [Qt Wayland Compositor overview](https://doc.qt.io/qt-6/qtwaylandcompositor-index.html)
- [QWaylandSurfaceGrabber](https://doc.qt.io/qt-6/qwaylandsurfacegrabber.html)
- [QWindow top-level semantics](https://doc.qt.io/qt-6/qwindow.html)
- [Weston headless and software renderer options](https://wayland.pages.freedesktop.org/weston/toc/running-weston.html)
- [libweston API status](https://wayland.pages.freedesktop.org/weston/toc/libweston.html)
- [wlroots headless backend](https://wlroots.pages.freedesktop.org/wlroots/wlr/backend/headless.h.html)
- [wlroots XDG shell API](https://wlroots.pages.freedesktop.org/wlroots/wlr/types/wlr_xdg_shell.h.html)

## Protocol and channel separation

The shared protocol version starts at:

```c
#define DP_SEAMLESS_PROTOCOL_VERSION 1
```

The detailed wire contract and bounds live in
`shared/deskport-core/protocol/SEAMLESS_MODE.md`. Its version 1 constants and
fixtures are included in published core revision `108ca7d`, pinned identically
by the Git submodule and desktop flake. Most reserved lifecycle messages are
intentionally disabled until admission and media milestones land. The local
sidecar event validator is an internal process boundary, not a second remote
wire protocol or an admission mechanism.

### Control channel

The implemented M1 control path reuses the approved peer certificate, mutual TLS,
certificate pinning, and bounded newline-delimited JSON convention already used
by peer control. It accepts exactly one `seamless-negotiate` request on a fresh
approved-peer connection, returns one `seamless-result`, and closes that preflight
connection. It does **not** acquire a session lease, start the sidecar, or admit
media or input.

A future admitted Seamless control session is suitable for small messages such
as:

- capability and version negotiation;
- session start, ready, error, heartbeat, stop, and release;
- window create, title/app ID update, map/configure, and destroy; and
- later input, focus, close, and resize requests.

The preflight request is accepted only when the host advertised the version and
the peer is already approved. An accepted result proves only mode/version
compatibility. All lifecycle, media, input, focus, close, and resize messages must
remain disabled until a future authenticated, cross-mode exclusive Seamless lease
is admitted. A Desktop-only or older peer must receive an unsupported result or
continue on the existing Desktop path; it must never guess the mode from an
unknown message.

Window IDs are opaque and scoped to one admitted session. They are not process
IDs, Wayland object IDs, or reusable global identifiers. The server owns ID
allocation; a destroyed ID is not reused during the session. Every message is
validated for type, version, state, ID, dimensions, sequence, and bounded text.

The existing non-clipboard peer frame limit is 32 KiB. Pixel payloads therefore
must never be base64-encoded into the control JSON stream.

### Media channel

A separate binary media channel is a future M3 requirement. It must reuse the
same DeskPort identity and a new exclusive Seamless session lease instead of
inventing another trust system. The receiver must bind the channel to that
admitted session before accepting payloads; the current preflight is insufficient.

The initial binary framing should include, at minimum, a magic value, protocol
version, record type, window ID, sequence, dimensions/stride or damage rectangle,
codec/format, and payload length. All lengths and dimensions require hard limits,
checked arithmetic, bounded queues, and backpressure. Stale, duplicate, unknown,
or destroyed window IDs are rejected without affecting Desktop Mode.

M3 may use a correctness-first full BGRA frame or a simple bounded lossless
encoding for one small window. M6 adds damage-driven updates and eliminates idle
traffic. Existing WebRTC/H.264 work can be evaluated later, after a generic
per-surface encoder input and a native receiver exist.

## Server module boundaries

The intended server boundary is:

```text
app/backend/SeamlessHostManager (implemented local supervisor)
  - exact sidecar process supervision and cleanup
  - bounded sidecar JSON validation and local lifecycle state
  - no admission lease or control/media channel binding yet
  - no compositor internals

host/linux/seamless/deskport-seamless-host
  - private Wayland display
  - QtWaylandCompositor + XDG shell
  - surface registry and stable session IDs
  - owned application launcher
  - local lifecycle/capture output
  - no remote trust database or public listener
```

The exact source filenames inside `host/linux/seamless/` may change while the M2
skeleton settles, but the process and authority boundary should not.

The sidecar skeleton also contains a known-pixel `wl_shm` self-test client. It
checks local create/damage/capture/hash/destroy behavior without a network
transport. On 2026-10-05 the package and this probe passed on the native x86_64
Linux host `wmn`, including exact child and Wayland-socket cleanup. That host has
a render device, so the stricter no-`/dev/dri` VPS condition remains unverified.

For M2, a successful local result is limited to:

1. the sidecar creates its private Wayland socket;
2. one software-rendered test application connects to that socket;
3. one XDG top-level is created and receives an ID;
4. title, app ID, map/configure, buffer, and destroy events are logged or emitted;
5. the capture skeleton can produce a bounded local image or an explicit
   unsupported-buffer error; and
6. the launched process, socket, and sidecar are cleaned up.

This does **not** mean that the surface was authenticated, encoded, sent over the
network, displayed on a remote client, or driven by remote input.

## Client module boundaries

The planned Linux client boundary is:

```text
SeamlessClientSession
  - pinned TLS negotiation and session lifecycle
  - control/media receive loops
  - validates and orders remote events
            |
            v
SeamlessWindowRegistry
  - maps one session-scoped remote ID to one local window
  - owns create/title/destroy ordering
            |
            v
SeamlessWindow : QWindow
  - parentless native top-level
  - QBackingStore/QImage software renderer for the first proof
  - later local focus/input/close/resize events
```

All `QWindow` creation, destruction, and painting occurs on the Qt GUI thread.
Network or decode workers deliver immutable frame data through queued calls. The
local compositor decides placement; Wayland clients cannot force global window
coordinates. The initial local application ID may remain DeskPort's ID, so some
desktops may group the remote windows even though each is a real top-level.

Do not wrap a Qt Wayland window with `SDL_CreateWindowFrom()`. It creates unclear
event and rendering ownership and does not remove the legacy single-window
Moonlight assumptions. SDL remains the proven Desktop Mode renderer.

## Milestones and current status

The status language below is intentionally conservative. A build or local
surface callback is not remote streaming acceptance.

| Milestone | Scope | Status on this experimental branch |
| --- | --- | --- |
| M0 | Repository audit, architecture, dependencies, risks | **Complete in design.** This document records the chosen boundaries. |
| M1 | Explicit `desktop\|seamless` mode plumbing and versioned negotiation; no pixels | **Partially implemented on the server.** The shared-core contract/fixtures and an approved-peer, one-shot preflight exist. A production Linux client emitter/consumer and a cross-mode exclusive Seamless lease do not. Preflight acceptance does not admit a session. |
| M2 | Headless Wayland sidecar, launch one test application, discover one top-level, lifecycle and capture skeleton | **Local skeleton implemented and verified on one native Linux host.** The packaged sidecar passed its known-pixel `wl_shm` lifecycle/capture/hash/cleanup probe on `wmn`. An external terminal and a no-`/dev/dri` VPS remain compatibility gates. This is not encoding, authenticated transport, remote rendering, or input. |
| M3 | Capture, transmit, and display one surface in one native client window | **Future/TODO.** Requires the authenticated media channel and client renderer. |
| M4 | Window-scoped keyboard, pointer, focus, and close | **Future/TODO.** Existing global `LiSend*` input is not reused directly. |
| M5 | Local resize to remote XDG configure/ack and redraw | **Future/TODO.** Temporary local scaling may hide round-trip latency. |
| M6 | Damage-driven updates and near-zero idle traffic | **Future/TODO.** Requires measured damage and queue/encoder policy. |
| M7 | Multiple remote top-levels in multiple local top-levels | **Future/TODO.** One session owns the registry; do not create one transport per window. |

Update this table only when the matching test boundary is present. In particular,
do not move M3 to complete after seeing a local captured `QImage`.

## Dependencies

M0/M1 use existing Qt Core/Network, TLS, and shared-core dependencies.

The M2 sidecar adds the Qt Wayland Compositor module to its own target. The Linux
package must provide its development/runtime libraries and Wayland server
dependency without adding them to the Desktop media process unnecessarily. The
sidecar also needs:

- a private, mode `0700` `XDG_RUNTIME_DIR` or a private socket inside the user's
  existing runtime directory;
- a unique `WAYLAND_DISPLAY` socket owned by the session;
- Qt XDG shell support;
- a software-capable Qt platform/rendering path; and
- one explicitly selected test application for lifecycle checks.

The first supported buffer contract is `wl_shm`. Linux DMA-BUF, EGL, Vulkan,
hardware encoding, XWayland, popups, subsurfaces, and nontrivial decorations are
not implied by a successful M2 check.

## Security and cleanup

- Seamless Mode is opt-in and capability-gated.
- The normal peer certificate and binding remain the root of trust.
- The current one-shot Seamless preflight does not acquire, reserve, or extend an
  existing session lease. Before M3, admission must enforce one cross-mode
  exclusive owner across Desktop, browser, and Seamless capture/input.
- The sidecar listens only on a private Unix Wayland socket. It must not expose a
  new unauthenticated TCP listener.
- Environment passed to remote applications is allowlisted. Credentials,
  management secrets, unrelated display sockets, and host environment details are
  not forwarded.
- Titles and app IDs are untrusted bounded text. They are metadata, not commands
  or local desktop-file paths.
- Window IDs, dimensions, rectangle arithmetic, payload lengths, queue depth, and
  update rates are validated before allocation or copy.
- On normal exit, error, timeout, or cancellation, stop owned applications, close
  media/control channels, remove the private socket/runtime artifacts, and stop
  the sidecar. Once the Seamless lease exists, release it only after that cleanup.
- Never kill processes by a broad executable-name match. Supervision owns exact
  child process handles/PIDs.

## Build and test commands

Keep build and test output under the project build root. From the repository root:

```sh
source scripts/build-paths.sh
mkdir -p "$DESKPORT_CHECKOUT_BUILD_ROOT/seamless/core"

cmake -S shared/deskport-core \
  -B "$DESKPORT_CHECKOUT_BUILD_ROOT/seamless/core" \
  -DBUILD_TESTING=ON
cmake --build "$DESKPORT_CHECKOUT_BUILD_ROOT/seamless/core"
ctest --test-dir "$DESKPORT_CHECKOUT_BUILD_ROOT/seamless/core" \
  --output-on-failure

nix develop -c python3 scripts/test-core.py
nix develop -c python3 scripts/test-seamless-host-manager.py
nix develop -c python3 scripts/test-host-lifecycle.py --binding
```

`scripts/test-core.py` now invokes the shared-core Seamless protocol fixture in
addition to the session graph, catalog, and workspace adapter checks.
`scripts/test-seamless-host-manager.py` builds an isolated fake-sidecar regression
that checks bounded JSON handling and exact-child teardown. Neither test exercises
QtWaylandCompositor or a native Linux display stack.

After building the helper on native Linux, the local M2 probe is:

```sh
deskport-seamless-host --self-test --width 320 --height 200
```

That probe passed from the Nix package on native x86_64 Linux (`wmn`) on
2026-10-05. The known-pixel hash proves only local `wl_shm` lifecycle/capture and
cleanup; it sends no pixels over a DeskPort connection.

The sidecar target is wired into the qmake/Nix path. For shared-core development,
validate an unpublished working-tree change with an explicit local input override:

```sh
nix build --no-write-lock-file \
  --override-input deskport-core "path:$PWD/shared/deskport-core" \
  .#packages.x86_64-linux.default
```

That override build passed on `wmn` on 2026-10-05. The current published core pin
no longer requires that override. New changes still require the normal locked
`nix build` and relevant native checks before release.

### Local supervisor regression coverage — 2026-10-11

The isolated fake-sidecar suite now covers the production supervisor's startup,
shutdown, event validation, and restart boundaries:

- A helper has ten seconds to report a valid `ready`; timeout stops only the
  owned child. Cancellation during process startup, failed exec, repeated stop,
  and synchronous signal-handler restarts each have explicit terminal behavior.
- Shutdown immediately withdraws readiness. Late or partially received `ready`
  and window events cannot reactivate the host or enter a newer run. Valid
  cleanup errors are still retained, and an unsolicited nonzero helper exit
  is not reported as a normal stop.
- The first failure remains visible after cleanup until a new start. Each run
  emits one `stopped` notification; an unresponsive helper is forcibly terminated
  after the existing five-second grace period.
- Parsing copies at most one 32 KiB record at a time, including its newline.
  Fragmented/coalesced input, oversized records, truncated EOF, strict integer
  fields, UTF-8 metadata limits, socket syntax, and unknown event types are tested.
- Window IDs increase within a run and cannot be reused after destruction.
  Metadata/frame events must reference a live window. Damage, optional capture
  file errors, and frame completion share one pending sequence; duplicate,
  stale, mismatched, and out-of-bounds frame events are rejected.

These tests use a fake process, not a remote session or a live compositor. The
2026-10-11 cloud check compiled the real sidecar with Qt 6.8.2, but its Wayland
self-test could not create an AF_UNIX socket in that sandbox (`EPERM`). The core
fixtures and Qt workspace adapter passed with ASan/UBSan and leak detection
explicitly disabled because LeakSanitizer cannot operate in that environment.
The locked Nix build, full host-binding regression, native no-GPU compositor,
and physical client checks remain separate verification gates.

Remaining targeted coverage for M1/M2 includes:

- optional-capability downgrade and Desktop/Seamless isolation;
- start/ready/stop state ordering, duplicate requests, timeout, and revocation;
- malformed version, ID, dimension, title/app ID, and oversized messages;
- deterministic create/configure/destroy lifecycle for one fake or test client;
- supported `wl_shm` capture and explicit rejection of unsupported buffers;
- child crash, compositor crash, client disconnect, and cancellation cleanup; and
- proof that no owned PID, socket, or runtime artifact remains after each case.

An isolated Wayland integration test must inventory existing compositor/test
processes first, use one owned instance, record its PID and socket, and tear it
down in a `finally`/trap path. It is a compositor-process test, not proof of a
real VPS, native input, latency, or deployed-package behavior.

## Run and debug interfaces

The intended user-facing mode selection is:

```sh
# Existing behavior; remains the default.
deskport host run --mode desktop

# M1/M2 experimental route. This is not a streaming-complete command until M3.
deskport host run --mode seamless
```

The mode parser and local `SeamlessHostManager` route have landed. The Seamless
command currently starts only the local sidecar path; it is not a production
remote client session and must not be presented as streaming-complete.

The sidecar is an internal helper, not a stable public CLI. During M2 development,
use its actual `--help` output rather than copying provisional flags:

```sh
deskport-seamless-host --help
```

Useful diagnostics for an owned local fixture include:

```sh
QT_LOGGING_RULES='qt.waylandcompositor.*=true' \
WAYLAND_DEBUG=server \
deskport-seamless-host --help
```

The final M2 integration test should print the private socket name, stable session
window ID, bounded title/app ID, buffer type and size, lifecycle order, child PID,
and cleanup result. It must not print peer certificates, access tokens, clipboard
contents, or captured application pixels.

The future M3 end-to-end command sequence will need an admitted client and a
remote application launch command. Its syntax is intentionally not documented as
working yet. Do not present `deskport connect`, `deskport run`, per-window input,
or remote frame delivery as implemented until their actual parser, protocol, and
integration tests exist.

## Local window diagnostics — 2026-10-12

While an experimental Linux Seamless host is running, `deskport status --json`
includes `windows` and a `windowStates` array in ascending session window-ID order.
Each detached snapshot contains bounded title/app ID, `initialWidth`/`initialHeight`
(the create-time configure proposal), `lastSequence`, `pendingSequence`, and
`lastFrame` with the most recent successful capture's sequence, dimensions and
SHA-256. It contains no pixels or capture file paths.

`captureError` describes the latest failed capture and clears after a successful
capture. That failure advances `lastSequence` but preserves `lastFrame` with its
own older sequence. `captureFileError` describes the most recent local PNG-save
failure; a new damage attempt clears it. A save failure does not invalidate
successfully captured frame metadata. Snapshot capture/save diagnostics
retain at most 512 UTF-16 code units each, keeping all 64 windows within the local
management response budget even with JSON-escaped text; forwarded events retain
their validated original messages. A destroyed window disappears from the array; final teardown and a new run clear the registry. During stopping,
the remaining snapshots are diagnostic state, not evidence of readiness.

The manager updates these snapshots before forwarding each accepted event. Queries
use the existing local management socket; this adds no remote wire message or
shared-core contract. Isolated macOS fake-sidecar and local-socket tests verify
snapshot ordering, capture failure/recovery, destruction and restart. Native Linux
daemon/compositor integration remains a separate acceptance gate.

## Experimental SSH debug stream and Seamless Lab — 2026-10-16

To exercise the M3-M5 client/server path before the admitted DeskPort media
channel exists, the sidecar has an opt-in `--stream` mode. It is meant only to
be started by an already-authenticated SSH login and never opens a listener:

```text
SeamlessLab (local)  --ssh -T-->  deskport-seamless-host --stream --scale N -- app
        stdin  : ClientHello, ClientInput (JSON), ClientFrameAck
        stdout : ServerHello, ServerEvent (sidecar JSON schema), Frame (binary)
```

The record framing lives in `app/seamless/seamlessstream.h` and is
transport-agnostic, so a future admitted media channel can reuse it. Each
record has a 16-byte little-endian header (magic, version, type, window ID,
bounded length). Frames carry damaged rectangles of premultiplied ARGB32
pixels, zlib-compressed per rectangle; the first frame after a size change
is a full frame. The server keeps at most two unacknowledged frames and
withholds Wayland frame callbacks while the client is behind, so a fast
application slows down instead of growing a queue.

Stream mode additionally provides:

- up to 16 concurrent XDG top-levels and popups, each with its own session ID;
- `xdg-decoration` with server-side preference, so the local native frame is
  the only title bar;
- integer output scale (`--scale`) for HiDPI clients; damage is converted from
  surface to buffer coordinates;
- window-scoped pointer motion/buttons/axis, keyboard (Linux evdev codes from
  physical keys, Qt key fallback), focus with release of held keys/buttons on
  focus loss or window destruction, close requests, and local resize to XDG
  configure; and
- `window-unmap` events when an application removes a surface's buffer; and
- `window-geometry` events carrying the `xdg_surface` window geometry in
  surface-local logical coordinates. XDG configure sizes refer to this
  rectangle, so the client sizes its native window to it, crops client-side
  shadows outside it and offsets pointer coordinates by its origin. Sizing to
  the buffer instead let each configure round trip grow a CSD client such as
  `weston-terminal` by its shadow margins until capture hit the size limit;
- `xdg_popup` windows (menus, combo lists, tooltips). A popup is announced
  with `window-create` carrying `popup: true` and its `parent` ID, followed by
  `popup-position` with the offset from the parent's window geometry. The
  client shows it as a frameless, non-focusable tool window at that offset.
  Because each popup is a separate local window, there is no output edge to
  constrain against and the unconstrained position is configured as final. A
  press on a root top-level while it has popups, or that top-level losing
  focus, sends `popup_done` to its popups (innermost first) and consumes the
  press, approximating an xdg_popup grab; and
- a launcher. `--app ID=PROGRAM` (repeatable, stream mode only) defines an
  allowlist that the server hello advertises as `apps`. The client sends a
  session-scoped (window ID 0) `{"type":"launch","app":ID}` request and can
  never supply a command. Each launched application runs in its own owned
  process group, reported with `app-start`/`app-exit` or `launch-rejected`;
  shutdown terminates every owned group. With a catalog, the session no
  longer ends when the optional initial application exits; it ends when the
  client closes the stream.

The client side is `SeamlessClientSession`, `SeamlessWindowRegistry` and one
`SeamlessWindow : QWindow` per remote top-level, as planned above, rendered
with `QBackingStore`. On macOS, physical key positions are translated from
`kVK_*` codes; Control stays Control and Command becomes Super.

`tools/seamless-lab/` wraps these in a standalone debug application,
SeamlessLab, with its own bundle identifier and settings domain. It does not
touch DeskPort/Moonlight settings, pairing data or services. Build the
self-contained, ad-hoc signed bundle under the build root with:

```sh
nix develop -c bash scripts/package-seamless-lab-macos.sh
```

The Linux side builds without the full DeskPort client, natively on the Linux
host:

```sh
nix build --out-link <build-root>/seamless-lab/server .#seamless-lab-server
```

or on a macOS host through its nix-darwin linux-builder, then copied to the
Linux host (the sidecar alone; full DeskPort and Sunshine Linux builds stay on
native Linux build hosts):

```sh
bash scripts/deploy-seamless-lab-server.sh <ssh-host> aarch64-linux
```

The script keeps one local `--out-link` per system under the checkout build
root and one remote GC root at `~/mygit/build/deskport/seamless-lab/server`, so
repeated deploys reuse the cached dependency closure and release the previous
build to the remote garbage collector. The remote nix-daemon must trust the
SSH user, because locally built paths are unsigned.

`seamless-lab-server` is the sidecar with a launcher catalog of `wl_shm`
applications (`terminal` = weston-terminal, `editor` = mousepad, `calculator` =
galculator); the catalog is added only for `--stream`, so `--self-test` runs as
before. Run the lab client once with `--host <ssh-destination> --server <remote
path>` (both are remembered). The launcher window, the global Apps menu and the
Dock menu start catalog applications; `--launch ID` (repeatable) starts them
on connect for scripted checks, and an application argv after `--` remains
available. Every remote window is a separate native window, so switching uses
the local window manager. The client log is
`~/Library/Logs/SeamlessLab/seamless-lab.log`.

Popup limitations in this harness: keyboard focus stays with the root
top-level, so keyboard navigation inside menus does not reach the popup;
popups without a grab (tooltips) are treated like menus, so a press while a
tooltip is open only dismisses it; press-drag-release menu selection is not
mapped across native windows (click, then click an item); and an open popup
does not follow a moved parent.

QtWaylandCompositor 6.11 advertises `wl_seat` version 4 (hard-coded in
`QWaylandSeat::initialize()`), and its data-device manager is not bound in
stream mode. Clients that require `wl_seat` v5, such as `foot`, therefore exit
at startup. Advertising v5 without emitting `wl_pointer.frame` would violate
the protocol, so this needs a compositor-side change, not a version bump.
`weston-terminal` always draws client-side decorations (it does not bind
`xdg-decoration`), so its frame appears inside the native macOS frame.

On 2026-10-29 the SSH lab path rendered `weston-terminal` from the aarch64
NixOS host (a headless Raspberry Pi 4, software `wl_shm`) into one native macOS window.
The same day, one session on that host ran weston-terminal, mousepad and
galculator as separate native macOS windows; opened, dismissed and selected
GTK 3 menu popups; created a second mousepad top-level from its menu; typed
into it; launched an application from the launcher list; and closed a window,
ending only that application. Every run ended with no remaining sidecar,
application process or Wayland socket on the host, and the non-stream M2
self-test still passed. These are debugging-harness results only; they are
not M3/M7 product acceptance.

This route is a debugging harness, not the product transport: it relies on
SSH for authentication, has no cross-mode lease, and does not interact with a
running Desktop Mode host. Moving the same records onto the admitted
DeskPort channel remains the M3 product gate.

## Known risks and limitations

- **Buffer compatibility:** the first proof is `wl_shm` only. Modern browsers and
  GPU applications commonly use DMA-BUF/EGL paths.
- **Capture cost:** surface grabbing and `QBackingStore` can copy pixels more than
  once. That is acceptable for correctness, not a performance conclusion.
- **Damage semantics:** a buffer commit is not yet an efficient damage encoder.
  M6 must preserve damage ordering and coalesce updates without losing content.
- **XDG configure ordering:** resize requires configure/ack discipline. Scaling an
  old buffer is only a temporary visual bridge, not completion of M5.
- **Popups and subsurfaces:** menus, tooltips, dialogs, and transient parents need
  an explicit mapping policy. Perfect handling is outside the first milestone.
- **Input:** the Desktop handler sends global Moonlight input. Seamless input must
  address a window and compositor seat and must release held state on focus loss,
  disconnect, or destroyed windows.
- **Window identity:** the local app ID initially remains DeskPort's identity.
  Compositors may group multiple remote top-levels even though they are independent
  native windows.
- **Wayland placement:** the client cannot choose absolute top-level coordinates;
  local placement belongs to the local compositor.
- **Resource limits:** a 1-2 vCPU/1-2 GiB host requires bounded windows, pixels,
  frame queues, and update rate. Static windows should eventually send no media.
- **API evolution:** Qt capture limitations may still force a later wlroots or
  custom capture backend. Keep `CompositorBackend`/capture interfaces narrow.
- **Test scope:** local lifecycle, build, or pixel checks are not evidence of
  deployed VPS reliability, physical client input, display-to-photon latency, or
  release readiness.

## Completion criteria before M3

The server skeleton does not satisfy the full M1/M2 product boundary yet. Before
starting M3, all of the following must be true:

1. this architecture and the shared protocol contract agree;
2. Desktop Mode remains the default and its targeted regressions pass;
3. old peers and Desktop-only peers cannot enter Seamless Mode;
4. a production Linux client emits/consumes the negotiated messages and an
   admitted cross-mode exclusive Seamless session has bounded start/stop/error
   semantics;
5. the sidecar launches one software-rendered test application on a private
   Wayland socket (a terminal remains the first external compatibility target);
6. one top-level receives a stable session ID and ordered lifecycle events;
7. the local capture skeleton returns a bounded `wl_shm` image or a precise error;
8. no remote pixel transport or client rendering is claimed;
9. failure and cancellation leave no owned child, socket, or lease; and
10. source, package, deployment, and physical acceptance remain reported as
    separate states.
