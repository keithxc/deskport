# DeskPort roadmap

Status: desktop interaction preview, 2026-09-10. This is a development tree, not a stable release.

## Product goal

Keep a remote desktop ready in the background. Recall it on the current local
workspace with one action, work normally, then tuck it away without reconnecting.
The first target is a KDE Wayland / AMD Linux client and a macOS Sunshine host.
Windows and macOS clients are intended follow-ups; they are not yet validated.

Product direction (2026-09-09): a single application with both viewer and optional
host roles, one explicit mutual-pairing flow and a shared device list. See
[the architecture](ARCHITECTURE.md) for component, permission and delivery boundaries.
The macOS preview bundles all three components and manages incoming PIN pairing.
The mutual-binding preview supports one recipient approval; reciprocal streaming
still requires manual acceptance.

## Binding delivery correction (2026-09-10)

Binding requests now distinguish connecting from recipient acknowledgment. The
actual approval QML component is covered by an isolated request/cancel test.
QML caches are keyed by resource content so reproducible builds cannot reuse
an older interface with identical resource timestamps. Discovery advertises the computer hostname instead of a fixed product name.
Validated on two computers: request delivery, recipient approval, saved bindings
in both directions, and host availability after restart. Reciprocal streaming
remains a manual acceptance gate.

## Implemented in the bootstrap

- Moonlight Qt v6.1.0 source and upstream Git ancestry.
- DeskPort executable, application/configuration identity and Linux desktop entry.
- Windowed, absolute-pointer defaults; mute on focus loss; background gamepad,
  game optimization and Discord presence disabled by default.
- Upstream update checks disabled for the independent development version.
- Pinned Nix build and development shell for Linux.

## macOS all-in-one preview (2026-09-09)

- Native Qt viewer plus original signed Sunshine bundle in one application.
- Native virtual-display helper; no BetterDisplay runtime dependency.
- Existing mirror membership is preserved; capture targets the active mirror source.
- Sharing page: start/stop, fixed HiDPI presets, permission shortcuts, incoming PIN.
- Private host state/certificates, separate ports, certificate-pinned local API.
- Tray recall while hosting and an optional user-login launcher.
- Monochrome tray artwork: native macOS template rendering and light/dark
  palette variants on other desktops (2026-09-10).
- Packaging script, signature checks, build-path dependency checks and host self-test.

Validated: native macOS GUI startup, bundled-component startup/cleanup, isolated
Linux CLI startup, and creation of all three fixed HiDPI presets. Intermediate
application bundles are excluded from Spotlight to avoid duplicate app entries.

Pending acceptance: complete OS consent, actual incoming stream and input,
reverse-platform hosting, reboot behavior and public distribution signing. A display
helper command acknowledgment is not proof of a changed physical display mode.
Live mode switching did not pass the first physical-mode check and remains pending. See MACOS_PACKAGE.md.

## Host lifecycle reliability (2026-09-10)

The macOS development preview now initializes host authentication asynchronously
and stops its child processes without blocking the UI. Unexpected host/display
exits clean up the other component and allow retry. Stop cancels pending startup;
late pairing replies cannot overwrite a newer session's status. The pairing button
is disabled during startup and cleanup. A running process is explicitly not a
claim that capture permissions or remote input have been verified.

Native regression harness: `python3 scripts/test-host-lifecycle.py` uses temporary
state and fake child processes, without display changes or input. Port coexistence
checks additionally use loopback-only socket reservations.
Existing separately installed Sunshine services are not modified or stopped.
Eight native lifecycle scenarios passed, including display failure while hosting
and authentication timeout. Simultaneous process error/exit notifications preserve
the original failure instead of replacing it with the cleanup process's exit.

Validation on 2026-09-10 also passed native macOS packaging/signature and linked
library checks, packaged CLI startup, and a Linux Nix build plus isolated CLI smoke
check. These checks do not establish real capture, input or permission readiness.

Next acceptance: validate the new package's permissions and incoming stream in a
user-approved test window. Keep the existing remote-access path available throughout.

## Coexistence with native services (2026-09-10)

User requirement: DeskPort and independently installed Sunshine or similar services
must not reconfigure or stop each other. Host startup now reserves the complete
TCP/UDP port family, selects an available DeskPort group, and remembers its base.
All pairing/UI/self-test endpoints use that base. A per-state-directory instance
lock prevents competing DeskPort launches from overwriting host state. Exhaustion
fails before creating a display; the native Sunshine port family is never selected.
Manual viewer entries default to DeskPort; explicit and saved ports are preserved.

Regression coverage includes each TCP/UDP family member being occupied, port
exhaustion/retry, a competing instance, chosen-port configuration, and explicit
native-Sunshine/manual IPv6 addresses. The instance-lock test also ages the lock file while its owner is still running.
All 21 scenarios passed (23 QtTest checks including setup/cleanup), followed by
native macOS packaging/signature/dependency checks, packaged CLI startup, and a
Linux Nix build with isolated CLI smoke checks. Full live streaming coexistence
still needs user-assisted acceptance while retaining the existing remote-access service.

## macOS privacy identity repair (2026-09-10)

Local TCC diagnostics attributed bundled-host capture to the outer DeskPort app,
but rejected the saved grant because an ad-hoc update changed its code hash.
Packaging now rejects accidental ad-hoc builds unless explicitly opted in. Local
installed updates use a stable Apple Development identity. The outer application
also declares the audio-input entitlement and microphone purpose string required
for the bundled host's audio request. Switching from the old ad-hoc identity needs
one user-assisted DeskPort reauthorization. Packaging also refreshes subproject
metadata and checks the final usage description and audio entitlement to prevent
stale incremental-build plists. The signed macOS package passed signature and
dependency verification; the Linux Nix build and isolated CLI smoke check passed.
Capture, audio and future-update grant
retention still need live acceptance; native Sunshine permissions remain untouched.

## TODO: first-run permissions and device identity (2026-09-10)

- [ ] Add a macOS onboarding guide covering Screen & System Audio Recording,
  Accessibility (keyboard/mouse control), and Microphone/audio input when required.
  Explain why each is needed and show actual readiness separately from pairing.
  Open the corresponding System Settings page and provide a draggable reference
  to the installed outer DeskPort.app, plus Reveal in Finder as a fallback. Verify
  which settings panes accept dropping an app; do not promise unsupported OS UI.
  Permission grants remain explicit user actions. Explain relaunch requirements
  and stale grants after migrating from ad-hoc signing; preserve native Sunshine.
- [ ] Keep the host identity stable before first pairing and across restarts/updates.
  Observed repeated missing host state before pairing and multiple saved UUIDs
  pointing to the same local endpoint. After pairing, the persisted UUID matches
  serverinfo. Verify ten restarts, including unpaired starts, without new entries.
- [x] Filter local loopback/interface addresses from discovery and the displayed
  saved-host list, preserving saved records and pairing data. Ignore local mDNS
  before querying or persisting new identities; do not match by name or subnet.
- [ ] Distinguish same-name devices by address and status; offer explicit removal of
  stale entries without deleting pairing data for another device. Never merge
  identities solely by display name or host address, since services can coexist.

Live checkpoint: user confirmed picture output; host logs show an active HEVC
session. Accessibility previously failed its saved code requirement; user reports
reauthorizing it, but pointer movement and click delivery still need acceptance.

## Mutual binding preview (2026-09-10)

The next preview implements IP/domain initiation plus one recipient confirmation,
TLS certificate exchange, both-direction local trust provisioning and automatic
remote device entries. Linux hosting is included with KWin/portal capture of the
existing desktop. See [the binding protocol and acceptance checks](PEER_BINDING.md).
PIN pairing remains available for legacy clients. Interrupted grants are visible;
local revocation is available, while coordinated remote revocation remains pending.
Live both-direction picture/input acceptance is still required.

## Next action: unified desktop experience (2026-09-10)

See [the desktop interaction guide](DESKTOP_UX.md) for the implemented flow and
acceptance checks.

User priority after the mutual-binding milestone: unify the Mac/Linux first-run
experience, permissions, binding, device list and settings. The new shared QML
shell groups Devices, Sharing and Settings, with a revisitable, optional setup
guide. Device cards show name, endpoint and connection state. Mutual binding is
the default add-device flow; legacy PIN pairing remains an explicit secondary path.
Common streaming settings are grouped separately from advanced compatibility
controls. Opening settings must preserve custom values without silently applying
presets. Permissions are checked separately from binding and host process state.

macOS reports screen, Accessibility and audio-input status, links to the matching
settings pane and provides an app-file drag plus Reveal in Finder. Linux explains
capture consent and checks input-device access without claiming that a process
start proves screen capture. Manual checks still required: OS pane drag acceptance,
keyboard navigation, both-platform layout, and reciprocal picture/input after the
refresh. Keep the independent remote-access service intact throughout.

## Following: persistent-session prototype

Language settings added on 2026-09-10: visible language selection with system
default, local persistence and live retranslation; primary desktop-page coverage
for English, Simplified/Traditional Chinese, Japanese, Korean, German, French and
Spanish. Existing additional catalogs remain selectable with English fallback.
See `docs/DESKTOP_UX.md` for translation maintenance and validation.

1. Diagnose input with the unchanged Moonlight/Sunshine path, including host
   permissions and interaction with a software KVM. Do not hide an input failure
   behind a new application shell.
2. Separate session lifetime from window visibility: add explicit show, hide,
   disconnect and quit operations. Do not treat a frozen frame as a live session.
3. Prototype one-action recall on the current KDE workspace; return held keys,
   buttons and input capture when the window is hidden or loses focus.
4. Measure recall time, fresh-frame time and input response separately; measure
   background bandwidth and CPU/GPU use before choosing an idle policy.

Acceptance: 50 hide/show cycles on the same healthy streaming session, no new
stream launch per recall, no stuck input, and a local escape that remains usable.
Window appearance under 200 ms is a target to measure, not a current result.
Initial connection, sleeping hosts and network recovery are separate cases.

## Then: daily development

### Dedicated display, client-size resolution and physical mirroring (updated 2026-09-10)

The host is intended to stay at home as an always-on desktop-development server.
Add a dedicated remote display whose mode follows the client's content area,
similar to a VM's automatic guest-display resizing. The macOS development package
includes native virtual-display creation and client-window adaptation. The Sharing
page shows the actual virtual-display size; Picture settings enable adaptation by
default for mutually bound, capable Mac hosts. No BetterDisplay runtime dependency
is needed for this dedicated workspace. Physical-display mirroring, making the
virtual workspace the main display, and restoring full physical topology remain
planned; the current mode preserves the existing physical capture source.

The first implementation coalesces changes for 900 ms after dragging, negotiates
aligned 640×360–3840×2160 pixel sizes over the pinned binding TLS connection, and
resumes video after stopping the old stream. This is a brief reconnection, not
seamless encoder reconfiguration. Window position/size, remote applications and the
display-control lease survive the transition. Only one approved controller can
resize; it sends heartbeats, and loss of its connection releases ownership.
After a 10-second grace period the display returns to its configured idle size.
Minimizing preserves the stream and display. Stopping sharing removes the helper.

Client drawable pixels are distinct from logical window size. HiDPI clients use
2× host scaling for workspaces of at least 1920×1080 pixels; compact workspaces use
1× because native testing found advertised small HiDPI modes that WindowServer
rejects. Fractional scaling is mapped to one of these macOS modes. Unsupported or
unbound hosts fall back to the saved fixed streaming resolution. Linux hosting
continues to capture its existing desktop.

Validation: isolated binding tests cover certificate pinning, unapproved clients,
exclusive ownership and transfer; host tests cover rejected/timed-out resize without
stopping sharing; QML tests exercise actual continuation-page activation. Native
window-drag, image quality and pointer acceptance are tracked separately from build
and protocol checks. See [adaptive display](ADAPTIVE_DISPLAY.md).

The requested session mode makes the virtual display the main display and mirrors
it onto the other attached displays. A full disconnect restores the previous
physical main display and display layout. This supersedes the earlier assumption
that physical displays remain independently arranged during a remote session.

- First prove persistent virtual-display creation and capture on macOS, including
  host reboot and operation without a physical monitor. Bundle the implementation
  and its permission/setup flow; do not require a separate display utility.
- Treat window size, client device-pixel ratio, host HiDPI scale and encoded frame
  size separately. A larger window should expose more workspace without blurry text.
- Coalesce resize events after dragging stops, choose supported/aligned modes and
  retain the last working mode if the host rejects a requested size.
- Verify how capture and encoder/decoder reconfiguration behave during an active
  session. Initial session-resolution selection is not proof of seamless live resize.
- Keep the virtual display alive while the window is hidden so applications stay
  placed. Define ownership before allowing two clients to change one display.
- Snapshot physical main-display identity, modes, scaling, positions and mirror
  groups before changing the topology. Make the virtual display the mirror source;
  account for physical aspect-ratio/mode limits without silently changing the
  requested remote workspace size. Surface unsupported mirroring explicitly.
- Distinguish window hide/minimize and transient transport loss from full session
  termination. Preserve the display during hide/reconnect; define a bounded orphan
  timeout and provide an explicit local stop action that releases session ownership.
- On full disconnect, quit, or unrecoverable session failure, restore the original
  physical main display and layout before removing the virtual display. Persist a
  recovery record for host-process crashes; handle unplugged monitors by selecting
  an available physical fallback. With no physical display, retain a usable headless
  recovery path rather than assuming restoration is possible.
- Deliver macOS first, then assess the KDE Wayland implementation independently;
  share settings and lifecycle semantics across platforms without claiming equal
  display-control capabilities before native validation.

Checkpoint: 30 alternating window sizes, including fractional client scaling;
sharp text, correct pointer coordinates, no application relocation to physical
screens, and measured interruption time. Start with explicit size presets if live
resizing requires disruptive stream restarts.

Additional acceptance: physical displays mirror the virtual workspace while
connected; 20 full disconnect/reconnect cycles restore the original main display
and layout; hide/show and brief network loss do not switch displays; host-process
crash recovery, monitor hotplug, headless operation and competing clients preserve
local control. Validate alongside the independent remote-access service.

### Port allocation and migration (requested 2026-09-10)

- Move preview defaults and fallback port families into the IANA dynamic/private
  range (49152–65535). The current 489xx defaults are outside that range, and UDP
  49000 is reserved. Private ports are not exclusive: retain complete-family
  conflict detection and configurable endpoints.
- Coordinate host, viewer, binding-service defaults, discovery announcements,
  saved peer endpoints, packaging, documentation and mynix firewall rules in one
  migration. Existing trusted peers must remain usable without deleting bindings;
  define mixed-version compatibility and rollback before deploying both platforms.
- Advertise actual service ports. Keep firewall rules consistent with selected
  ports; a fallback must not silently produce an unreachable host. Keep the
  management UI local and avoid opening unused ports or broad fallback ranges.
- Preserve independent Sunshine/Moonlight services and their settings.

Checkpoint: fresh pairing and existing bindings work across the migration; exercise
occupied default ports, restart, rollback and mixed versions on macOS and Linux;
verify discovery, reciprocal connection and streaming through the enabled firewall.

References: [IANA port registry](https://www.iana.org/assignments/service-names-port-numbers/),
[UDP 49000 reservation](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=49000),
[RFC 7605](https://www.rfc-editor.org/rfc/rfc7605.html).

### Shared clipboard (requested 2026-09-10)

- Add bidirectional clipboard sharing between the active client and host, starting
  with Unicode plain text, then PNG images. File transfer remains deferred.
- Provide a clear session setting to enable or disable sharing. Limit exchange to
  the authenticated active session; binding alone must not synchronize clipboards.
- Do not overwrite either clipboard on initial connection or replay stale contents
  after reconnect. Prevent echo loops and define ordering for simultaneous copies.
- Set payload limits and report unsupported or oversized content without silently
  truncating it. Do not log clipboard contents or retain clipboard history.
- Stop exchange on full disconnect. Validate platform clipboard permissions and
  KDE Wayland behavior independently; do not infer support from the shared UI.

Checkpoint: 100 bilingual/multiline text transfers in both directions, PNG transfers
after image support lands, simultaneous copies, disabled sharing, disconnect and
reconnect; no initial overwrite, stale replay or loops, and clear limit handling.

### Remaining daily-development work

- Explicit local/remote shortcut handling; native host IME first.
- Reconnection that preserves remote applications and returns local control.
- Fixed resolution/scale profiles, with text clarity checked on real hardware.
- Ten disconnect/reconnect trials; 100 bilingual/multiline clipboard samples;
  three two-hour development sessions before asking 3–5 developers to try it.

## Deferred

Individual remote application windows, multi-display roaming, file drag/drop,
USB redirection, local IME preedit forwarding, cloud accounts, mobile clients,
automatic updates and broad distribution packaging. Stable macOS signing is required
for the planned host deployment, rather than a cosmetic release task. Do not refactor the decoder or protocol
unless a measured blocker requires it.

### Fractional scaling and virtual-display input (2026-09-10)

- Compute the client logical workspace from drawable pixels / system scale. Use
  2× macOS backing for scaled clients, 1× otherwise, aligned to four pixels and
  bounded by 7680×4320. Keep a minimum 960×540 logical workspace for valid native
  HiDPI modes. Example: 2880×1620 at 150% becomes 3840×2160 backing / 1920×1080 logical.
- Match Qt logical output geometry to SDL native pixel modes on Wayland: SDL window
  ratios may hide fractional scaling while QScreen DPR may round 150% to 200%. Reconnect after changing
  system scale; live scale/hotplug refresh remains an acceptance follow-up.
- Build a pinned, patched macOS input backend that follows the verified virtual
  capture display. Do not change the physical main display to redirect input.
- Acceptance: verify mouse motion/clicks, fractional-scale text size, resize/resume,
  and coexistence with independent Sunshine on the installed endpoints.

### Continuous window during adaptive resize (2026-09-10)

- Retain the native client window, its placement and maximized/fullscreen state
  while the stream renegotiates. Show a translated animated loading view instead
  of destroying the window and exposing the desktop.
- Release input during the transition; discard waiting input rather than replaying
  it. Close/Escape cancels the continuation, and failed startup disposes the window.
- Wayland/Vulkan uses shared-memory loading buffers on the existing surface;
  SDL's software-renderer fallback can recreate Vulkan windows and is avoided.
- Native isolated tests exercise 30 handoffs, native-window identity, animated
  frames, compositor buffer recycling and close cleanup on macOS and KDE Wayland.
  Real stream/decoder resume and perceived continuity still require interactive
  acceptance after installation, including minimize and repeated resize.
