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

## Next action: persistent-session prototype

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
