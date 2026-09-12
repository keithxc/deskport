# DeskPort roadmap

## Recovery and diagnostic follow-up (2026-09-12, 0.1.10)

Reason: review of current client/host logs and the retained 0.1.6 crash report.

- Keep each executing Session under C++ ownership until both exec() and queued
  transport cleanup finish. Page destruction/garbage collection must not free a
  Session still referenced by audio cleanup. This closes a lifetime hazard;
  the historical heap corruption is not proven resolved without reproduction.
- Bound adaptive /resume requests to 15 seconds, retain normal launch timeouts,
  and make late QML teardown callbacks tolerate a destroyed page context.
- Answer headless --help/--version with Qt's offscreen platform on Linux.
- Keep VAAPI eligible on native Wayland instead of preferring unavailable VDPAU.
  Retain the conservative Gallium RFI workaround; do not claim a driver-version
  cutoff or a measured latency/power improvement.
- Repair macOS test linkage for the native status menu and translate the new
  startup-management and resize-failure messages in seven languages.
- Add submitted/returned PTS and encoder name to host IDR failures. Current
  logs correlate these with client loss/queue overflow; synthetic VideoToolbox
  requests succeeded, so the real-session cause remains open. The old message
  alone does not prove SPS/VPS injection was suppressed.

Validation: Linux Nix build, headless CLI, UI/service/state tests and macOS
UI/service/state tests passed during development. Lifetime tests cover page
collection and cleanup both before and after exec returns. Final release
verification is recorded in RELEASE_0.1.10.md.

Next action: native two-device acceptance after switching to 0.1.10.
Checkpoint: 30 alternating resizes, 20 disconnect/reconnect cycles, then a
2-hour session. Check frame recovery, no stuck input, actual decoder selection,
and whether any heap/IDR errors recur. Do not erase existing diagnostic logs.

## Shorter tray menu (2026-09-12)

The tray menu's restart and quit entries dropped the application name; inside
DeskPort's own menu it said nothing.

## Tray left click moves the window (2026-09-12)

Reason: the tray had two window entries ("Open device list" and "Return to
remote desktop") and no way to put the window away again, while the left click
duplicated one of them.

The left button now toggles: it shows the remote window during a session and the
device list otherwise, and hides whichever is up. A new `DeskPortToggleWindow`
event makes that decision on the session's own thread, where the SDL window's
visibility is known. The right button owns the menu, reduced to opening the
device list, disconnecting, restarting and quitting; stopping sharing lives in
the device list with the other sharing controls.

macOS needed its own path: a menu attached to a status item is opened by either
mouse button and suppresses the button action, so the menu is popped up as a
native `NSMenu` from the `Context` activation and no Qt menu is attached there.

Validation: a Nix build and the service and UI suites on pk4, and a tray toggle
added to the 30-cycle retained-window transition test. Live clicks on both
desktops remain a user acceptance check.

## Active-session control center (2026-09-11)

Follow-up: the user reported that the first tray action did nothing while viewer
recall worked. An added regression reproduced a null Qt window passed into
`Session::exec()` from the deferred StreamSegue Loader. Use the existing root
`window` context instead of the attached `Window.window` property; assert the
session receives the actual root window, not just that its page was loaded.

Deployment: the macOS development update was installed and restarted with the
original signing identity. Strict signatures, bundle dependency checks and the
running host HTTP endpoint passed. Aqua-session packaging with the full build
PATH resolved the automation signing failure; `docs/MACOS_PACKAGE.md` records
the reproducible route and old-process shutdown pitfall. Signing now has an
early probe, verified to stop before building in the failing session. Linux was
also deployed; live control-center/recall acceptance remains with the user.

Adaptive continuation fix: the stream page for an adaptive resize was created
in the context of the page it replaced. Destroying that page aborted the new
page's Loader ("Object or context destroyed during incubation") and left the
client on "Adjusting resolution". StackView now creates the continuation and
quit pages in its own context. A regression test emits readyForDeletion after
exec() returns, as real sessions do, and requires the continuation to receive
the root window.

Deployment correction: an interim macOS install was ad-hoc signed, and the
independent Sunshine agent was removed while diagnosing an offline peer. Both
were reverted. The package was rebuilt in the Aqua session with the stable
identity, strictly verified and installed. That interim backup was removed in the
2026-09-11 `dist` cleanup; the current rollback bundle is
`dist/backup-20260911-225113/DeskPort.app` (0.1.1). The tray recall item now uses the
translated "Return to remote desktop" label.

Reason: a retained remote window had no discoverable route back to the device
list. Closing it hid the viewer, while tray activation recalled the same viewer,
leaving sharing and device management inaccessible until disconnect.

The viewer close action now releases remote input, hides the SDL window and opens
the device list without ending the session. The control center shows an explicit
active-session banner; its action or a device-card click recalls the retained
remote window instead of starting a second connection. The tray separates
**Open device list** from **Return to remote desktop**, and a second application
launch opens the device list. An explicit disconnect remains separate.

Validation: 13 isolated Linux UI checks cover a control center stacked above an
active session and cleanup after disconnection; 30 retained-window transition
cycles cover close, device-list hide, recall and explicit disconnect. A Linux
Nix build passed. Native Wayland focus placement and live input release/recall
remain real-session acceptance checks.

## Input defaults corrected (2026-09-11)

Reason: real use on the KDE Wayland client showed that retained 0.1.1 settings
left system-key capture off and clipboard sharing unset, so Super+Space stayed
local and no clipboard channel was created. DeskPort now enables system shortcut
capture and plain-text clipboard sharing for new profiles and performs a one-time
version-3 settings migration for existing profiles. Users can still disable
either option after migration. Changes take effect on the next connection.

### Editable remote endpoints (2026-09-11)

Reason: incoming bindings could retain a proxy IP and overwrite repaired host
addresses at startup. Bindings now exchange a separate DNS hostname, prefer
entered or previously saved DNS names, and use numeric addresses only when no
DNS name is available. Certificate identity remains pinned.

Saved access now includes an editor for the local device alias, domain/IP,
host port and binding port. Changes persist atomically, update the host list,
and preserve trust credentials. Invalid input and failed writes leave the old
record intact. Explicit endpoints are polled before cached discovery addresses.
Validation: 13 isolated macOS binding checks and seven-language editor coverage
passed. Native editor acceptance remains pending.

### Reconnect and application recall (2026-09-11)

- Defer stream execution until asynchronous QML Loader incubation completes.
  Replacing a page from its nested session loop previously destroyed the active
  incubator and could strand the retained window on Adjusting resolution.
- Normal GUI and streaming launches share a per-configuration process lock and
  local activation socket. Repeated launches request recall instead of creating
  another GUI or loading host state. CLI list/pair/quit commands remain separate.
- Tray Open and tray activation use the same recall action. Active streams and
  resize transitions raise their existing SDL window on its owning thread;
  idle clients raise the existing QML root window. Linux keeps the Qt event loop
  responsive while the SDL worker runs.
- Validation: 12 Linux UI checks, including nested-loop continuation and repeated
  activation/lock release, passed; the Linux package built successfully.
  Installed on the target Wayland client with persistent package rooting and
  matching command, menu and autostart entries. A real adaptive resize resumed
  streaming and decoded video after the retained-window handoff. Two additional
  launches exited successfully while the original session process remained.
  Invoking the installed tray's Open action twice also retained that one process.
  Cross-workspace focus behavior and extended resize stability remain acceptance
  checks; this does not claim a long-duration streaming soak.

Status: v0.1.0, first release, 2026-09-10. Unfinished capabilities and native
acceptance limits are listed below. See [release notes](RELEASE_0.1.0.md).

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

Client sleep and termination (2026-09-11): a Linux client holds a logind delay
inhibitor and ends the stream before suspend/hibernate, without honoring "quit
app after streaming". SIGTERM/SIGINT now stop the session and host processes;
SDL's handlers previously swallowed them until systemd sent SIGKILL. Stream pages
connect session signals once, so a control-center round trip no longer repeats
termination handling. Verified: SIGTERM exit in an isolated profile. Pending:
real suspend/hibernate and resume on the KDE client; automatic reconnection after
resume is not implemented.

Host resume hang (2026-09-11): Sunshine's macOS `dummy_img()` waited forever for
a first frame. A resume racing the idle-mode restore of the virtual display left
the single HTTPS thread blocked and the host unreachable until Sunshine was
restarted. `host/macos/patches/sunshine-capture-timeout.patch` bounds the wait to
three 2-second capture restarts, then fails encoder validation so /resume returns
an error. The streaming `capture()` loop still waits without a timeout (upstream
FIXME). Verified: patched host builds and packages; live race not yet reproduced.

Adaptive resize latency (2026-09-11): a measured resize took about 7.5 s from
restart to the resume request. Adaptive continuations now skip the 3.5 s launch
warning toast wait and the 1.5 s segue delay before connecting. Remaining work:
reuse the chosen decoder instead of re-probing, show the scaled previous frame
during transition, and eventually change resolution in-band at an IDR without
restarting the stream (as RDP 8.1 dynamic resolution does). Rate-limit layout
updates and wait for each transition to finish before sending another.

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

Client drawable pixels are distinct from logical window size. Since 2026-09-11 the
stream matches drawable pixels 1:1; clients at 150% or more use 2× host scaling,
others 1×. A 2× workspace keeps a 960×540 logical minimum (1920×1080 pixels)
because native testing found advertised small HiDPI modes that WindowServer
rejects. The 2026-09-10 1× policy divided by client scale and upscaled visibly
soft text on a 150% client. Unsupported or
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

Design clarification (2026-09-11): the user requests Deskflow-style sharing.
The current `KeyComboPasteText` sends UTF-8 input events and does not synchronize
the remote clipboard. Study Deskflow's format abstraction and sequenced clipboard
messages, adapting the design to an authenticated DeskPort channel on both ends.
Do not assume its protocol can be plugged into Sunshine unchanged.
Reference: [Deskflow clipboard messages](https://deskflow.github.io/deskflow/group__protocol__clipboard.html).

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

### Pointer-scoped keyboard capture (requested 2026-09-11)

Reason: the user wants VM-like input routing while the pointer is inside the
remote desktop, including system shortcuts rather than triggering local actions.

- [x] Extend existing SDL keyboard capture with remote-video-region entry/exit
  handling. Capture only for a visible, focused streaming window; restore local
  input on exit, focus loss, hide, disconnect and explicit release.
- [x] Preserve a documented emergency release shortcut and release held remote
  keys/buttons across transitions, including drags and modifier chords.
- [ ] Validate KDE Wayland focus behavior: pointer entry alone does not establish
  keyboard focus. If activation is unavailable, require a click and make capture
  state visible. Do not silently change the user's desktop focus policy.
- [ ] Verify Alt+Tab, Super, Super+Space and remote modifier mapping on the actual
  target. OS-reserved shortcuts are exceptions; do not promise every key can be
  intercepted. Existing SDL grab flags alone do not prove compositor acceptance.

Implementation update (2026-09-11): pointer routing and bidirectional Unicode
text exchange are implemented. PNG images remain the next clipboard phase.
Next action: native two-device acceptance. Checkpoint: 100 entry/exit cycles,
held modifiers, dragging across the edge, hide/recall and disconnect, with no stuck
remote keys or local shortcut activation while capture is effective.
Automated protocol checks do not establish compositor shortcut interception.
Reference: [SDL keyboard capture](https://wiki.libsdl.org/SDL2/SDL_SetWindowKeyboardGrab).

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

### 1× host workspace by default (2026-09-10)

User preference: reduce the host framebuffer and transmission cost. This
supersedes the earlier automatic 2× backing policy. Adaptive clients now request
1× at their logical workspace size; sharing startup and idle restore also use 1×.
Saved idle pixel sizes and explicit legacy 2× protocol requests remain supported.
A 2880×1620 client at 150% requests 1920×1080 instead of 3840×2160, one quarter
as many pixels. Text sharpness and actual bandwidth remain native acceptance items.

Investigation of the cropped image reproduced the crop in host capture before
encoding: the mirrored virtual source exposed 1280×720 logical bounds while its
physical mirrors exposed 2560×1440. A screenshot captured the full desktop, but
both AVFoundation and a ScreenCaptureKit streaming experiment reproduced the crop.
The experiment was removed; do not claim changing capture APIs fixes this issue.
Validate the 1× mode in the user's existing mirror arrangement after installation.

Validation: 12 binding/workspace checks, 26 host lifecycle checks and the Linux
`nix build` passed.
The signed macOS package was installed with sharing enabled. CoreGraphics then
reported 2560×1440 physical and logical dimensions (1×) for the virtual source
and both physical mirrors. A fresh host-side capture at 1× showed the full
desktop. The isolated virtual-display test was stopped when WindowServer changed
mirror modes, so repeated mode switching is not recorded as passed. Repeated
client resize, input and end-to-end image acceptance remain pending.

### Remembered client workspace and desktop input (2026-09-10)

- Save each host's last stable client window, maximized/fullscreen state and 1×
  stream dimensions. Reconnect requests that size immediately. Display layout,
  scale or configured window-mode changes invalidate the saved state.
- New installations capture system shortcuts in windowed mode as well as
  fullscreen. Explicit saved choices remain unchanged; existing users with
  capture set to Never must choose Always to send Super/Windows + Space as
  Command + Space to a Mac host.
- Show a local pointer by default in desktop mode, with a setting to disable it
  when the host also draws a pointer. Focus loss releases keyboard capture and
  shows the pointer; focus return reapplies capture.
- Validation: Linux `nix build`, isolated desktop preference/state persistence
  tests, QML settings/stream-page tests, seven translated catalogs and isolated
  CLI smoke passed. Restart the installed client before native acceptance of
  shortcuts, pointer visibility and reconnect without an initial resize.

### Small video packets by default (2026-09-10)

- Default video packet size is 896 bytes (16-byte aligned), leaving headroom for
  transport and tunnel headers on constrained mobile/VPN paths. This is a video
  protocol size, not the final on-wire datagram length. Explicit custom packet
  sizes remain supported.
- Unknown routes use remote mode rather than AUTO, because upstream AUTO replaces
  the requested size with 1024/1184 on public routes. LAN and VPN detection retain
  the smaller size. Resolution, bitrate and image quality settings are unchanged.
- TCP MSS belongs to the operating-system network configuration; this video
  change alone does not repair a TLS handshake affected by a path-MTU black hole.
- Validation: Linux `nix build`, isolated CLI smoke, desktop-state and QML
  regression suites and translation checks passed. Live video packet capture
  and stability on the affected mobile link remain acceptance checks.

### Durable binding addresses and local host audio (2026-09-10)

Reason: resolved proxy addresses can become stale, and desktop audio should stay
on the computer producing it by default.

- Retain locally entered DNS names for outgoing bindings and restore names from
  older saved requests. Keep certificate pinning and separate service ports.
- Disable host audio streaming by default using Sunshine's `stream_audio` setting.
  Sound settings can opt in after restarting sharing. New viewer preferences keep
  host audio playing; explicit existing viewer choices remain intact.
- Validation: Linux package build, hostname binding/legacy recovery/key-change
  rejection, desktop preference persistence, generated host audio configuration,
  and seven translated catalogs passed. The binding suite ran 11 checks successfully;
  its macOS-only adaptive-display check failed on Linux because that feature is
  unavailable there. macOS build/native audio acceptance remain pending.
  Installed applications and the active session have not been restarted.

### Installed update verification (2026-09-10)

- Fixed the device-list importer to accept URL-valid DNS hosts as well as numeric
  addresses; otherwise restored hostname bindings were rejected at import.
- Built and installed both Linux and macOS clients from matching source changes.
  macOS keeps the existing signed host/display helpers and signing identity.
- macOS binding tests: 12 passed. Host lifecycle tests: 26 passed. Bundle signature
  and dependency validation passed. Linux `nix build` passed.
- Both running host configurations disable audio streaming. Native macOS settings
  show the new switch disabled, and the saved viewer address retains its hostname.
- Remaining network issue: TLS through proxy DNS can time out even when direct VPN
  TLS succeeds with the same pinned identity. Saving a hostname does not fix that
  routing problem. End-to-end sound playback is not recorded as verified.

### Scope automatic discovery to DeskPort ports (2026-09-10)

Reason: independent Sunshine and DeskPort services on one computer have the same
machine name and were both added automatically.

- Only probe mDNS advertisements using the DeskPort base-port family: 48989 plus
  increments of 100 through 50889. Reject unrelated service ports before resolving
  or querying their server information.
- Hide and stop polling older unpaired, automatically discovered entries outside
  this family. Preserve their stored data and explicitly added or paired services.
- Manual addition and approved bindings continue to support explicit custom ports.
- Validation: Linux `nix build`, 26 macOS host lifecycle checks, and macOS bundle
  signature/dependency validation passed. Installed and restarted both clients.
  Existing native-service records are retained on disk but excluded from discovery
  and the visible device list unless explicitly added or paired.

### Connect directly to the desktop (2026-09-10)

Reason: selecting Desktop or Steam after selecting a device adds an unnecessary
step to the normal remote-desktop workflow.

- Clicking an online paired device loads and launches its Desktop entry directly,
  or resumes that desktop session. A brief loading page handles uncached app lists.
- Replace the loading page with the stream so disconnect returns to Devices.
  Leaving the loading page cancels pending launch and does not reconnect later.
- Never launch Steam or terminate another running application implicitly. Missing
  Desktop entries and busy hosts show actionable messages. The device menu retains
  Applications as an explicit fallback for custom hosts and other applications.
- Validation: 10 isolated QML checks passed, covering immediate and delayed launch,
  missing Desktop, busy hosts, cancellation and return to Devices after disconnect.
  Seven-language coverage, Linux `nix build`, macOS compilation and signed-bundle
  validation passed. Installed and restarted both clients. Native end-to-end
  desktop/input acceptance remains a manual check.


### Clipboard, pointer routing and resident service (2026-09-11)

Reason: daily cross-device development needs shared text and reliable local input
release; closing a window must not remove an unattended machine's remote access.

- Text clipboard sharing is enabled by default on both bound devices and can be
  disabled in Keyboard & pointer.
  A separate pinned mutual-TLS session starts only after streaming starts, works
  without adaptive resolution, and expires on disconnect or a missed lease.
  Initial contents are not transferred. Host revisions order concurrent copies;
  a newer local copy during a request stays pending. UTF-8 text is limited to
  1 MiB; invalid text, files and images are rejected without truncation or logging
  contents. PNG sharing remains pending. Hidden connected sessions keep sharing.
- Desktop-mode keyboard routing requires capture enabled, focus, a visible window
  and a pointer inside the video region (including letterbox boundaries). Exit,
  focus loss, hide and disconnect release held physical keys and mouse buttons.
  Ctrl+Alt+Shift+Z releases input; click inside to resume. The title shows routing
  state. Capture preferences remain respected; compositor-reserved keys still
  need native validation.
- Closing the main, viewer or resolution-transition window hides it. The tray
  recalls the window, disconnects only the viewer, stops local sharing explicitly,
  or fully exits DeskPort. Ctrl+Alt+Shift+Q disconnects the viewer. Qt background
  events remain responsive during the non-threaded macOS streaming loop; Linux
  retains its existing main-thread event pump.
- Host/helper failures retry after cleanup at 5/10/20/40/60-second intervals;
  a stable minute resets the delay. Explicit stop cancels recovery and survives
  application restart. Credentials and independent services remain untouched.
- Login startup now supervises the actual process: macOS LaunchAgent with
  unsuccessful-exit recovery; Linux XDG login entry starts a systemd user service
  with on-failure recovery and process-group cleanup. Successful tray exit does
  not respawn. New setup enables login startup; saved opt-outs stay respected.
  Existing enabled startup files refresh on launch. Service registration applies
  at the next GUI login; this is not pre-login/FileVault-unlock access.
- Validation: Mac/Linux builds, 100 per-direction text exchanges, SDL clipboard
  integration, service recovery, existing binding/lifecycle and UI regressions
  passed. See `docs/INPUT_SERVICE_ACCEPTANCE.md`. Do not mark native keyboard,
  real clipboard permissions or unattended reboot acceptance as complete from
  protocol tests or builds. Both installed applications were subsequently updated
  and restarted; macOS 0.1.1 reports sharing enabled and the bound Linux peer online, and Linux
  remains active under its user service. See the installation follow-up in
  `docs/INPUT_SERVICE_ACCEPTANCE.md`.
