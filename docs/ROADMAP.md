# DeskPort roadmap

Status: bootstrap, 2026-09-09. This is a development tree, not a stable release.

## Product goal

Keep a remote desktop ready in the background. Recall it on the current local
workspace with one action, work normally, then tuck it away without reconnecting.
The first target is a KDE Wayland / AMD Linux client and a macOS Sunshine host.
Windows and macOS clients are intended follow-ups; they are not yet validated.

Product direction (2026-09-09): a single application with both viewer and optional
host roles, one explicit mutual-pairing flow and a shared device list. See
[the architecture](ARCHITECTURE.md) for component, permission and delivery boundaries.
The macOS preview bundles all three components and manages incoming PIN pairing.
Reciprocal access through one mutual-pairing flow remains planned.

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

## Next action: one-interaction mutual pairing (2026-09-10)

User priority: after one pairing interaction, both computers should list each
other and be able to initiate a desktop connection without a second PIN entry.
This supersedes the persistent-session prototype as the next implementation task.

Current prerequisite: the Linux package is viewer-only; HostManager::available()
is disabled outside macOS. Reverse desktop access therefore requires Linux host
integration and its capture/input acceptance before mutual access can be claimed.

- [ ] Bundle and supervise a Linux host with independent DeskPort state and port
  reservation. Validate KDE Wayland capture/input and expose host readiness.
  Preserve any independently installed Sunshine and Moonlight configuration.
- [ ] Offer an explicit "Allow both computers to view and control each other"
  pairing mode. One PIN/confirmation establishes both requested directions;
  retain a one-way mode for computers that should not expose their desktop.
- [ ] Coordinate both directional GameStream pairings over a channel bound to the
  verified peer certificate. Exchange only the identity/address information and
  short-lived pairing material needed for the operation, never host admin secrets.
  Bind requests to a transaction, expire them, reject replay and unauthenticated
  reverse requests, and report partial completion without silently expanding access.
- [ ] Add each verified remote peer to the other device list automatically, using
  its stable identity and reachable host port. Preserve the local-host filter.
  Device presence, trust and capture/input readiness are separate UI states.
- [ ] Persist each direction and provide revocation. A disconnected peer must not
  prevent immediate local revocation; do not claim remote removal until acknowledged.
- [ ] Validate with two fresh profiles: one pairing interaction, both-direction
  picture and input, restart without re-pairing or duplicate records, offline peer,
  rejected/replayed requests, partial failure/retry and revocation. No personal-host
  connection or input injection in automated tests; live validation is user-assisted.

## Following: persistent-session prototype

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

### Dedicated display and window-size resolution (requested 2026-09-09)

The host is intended to stay at home as an always-on desktop-development server.
Add a dedicated remote display whose mode follows the client's content area,
similar to a VM's automatic guest-display resizing. Automatic resizing is planned,
not shipped. A manual macOS prototype using an existing BetterDisplay installation
now provides a dedicated 1280×720 logical / 2560×1440 HiDPI display; a real
2560×1440 at 60 FPS HEVC stream was received on Linux. This external setup is
not yet bundled, and reboot recovery and pointer mapping need acceptance tests.

- First prove persistent virtual-display creation and capture on macOS, including
  host reboot and operation without a physical monitor. Keep local displays intact.
- Treat window size, client device-pixel ratio, host HiDPI scale and encoded frame
  size separately. A larger window should expose more workspace without blurry text.
- Coalesce resize events after dragging stops, choose supported/aligned modes and
  retain the last working mode if the host rejects a requested size.
- Verify how capture and encoder/decoder reconfiguration behave during an active
  session. Initial session-resolution selection is not proof of seamless live resize.
- Keep the virtual display alive while the window is hidden so applications stay
  placed. Define ownership before allowing two clients to change one display.

Checkpoint: 30 alternating window sizes, including fractional client scaling;
sharp text, correct pointer coordinates, no application relocation to physical
screens, and measured interruption time. Start with explicit size presets if live
resizing requires disruptive stream restarts.

### Other daily-development work

- Session-scoped text clipboard, followed by PNG; no initial clipboard overwrite,
  echo loops or silent loss of oversized data.
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
