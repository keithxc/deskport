# DeskPort 2.9.1

Stable desktop release for macOS Apple Silicon, Windows x64 and Linux x86_64.
This update revises desktop input delivery, high-resolution scrolling, macOS
virtual displays and data saver behavior. The changes below cover the public
desktop source from v2.9.0 through `1d7df96b`.

## Changes since 2.9.0

- **Desktop mouse input** (`14bd37df`, `017788c4`, `d90f79dc`). The embedded
  viewer wakes when input or window state arrives. It combines consecutive
  pointer movements and skips redundant window-state updates while the pointer
  stays over the canvas. On macOS and Windows it can wait directly on the shell
  IPC socket. A dedicated SDL event keeps these wakeups separate from text and
  other user events. The bundled host removes a 10 ms delay on left-button
  release in absolute mouse mode and lowers the motion activity threshold from
  6 to 3 pixels.
- **High-resolution touchpad scrolling** (`58cdb8a0`, `cf1486a1`). The viewer
  preserves fractional wheel movement on both axes, handles touchpads that
  report only pixel deltas, and reverses horizontal scrolling on the correct
  axis. The macOS host accumulates small deltas until they add up to a whole
  scroll point.
- **macOS virtual displays** (`a5645e57`, `a5847ead`). The host derives the
  virtual display's physical dimensions from the requested resolution and scale.
  It recreates a primary workspace's display when its geometry changes and can
  reuse a compatible extended display. Standard and HiDPI workspaces have
  distinct product IDs so macOS can remember their modes independently. Fallback
  mode corrections request permanent configuration for normal displays and
  session-only configuration for isolated displays.
- **macOS tray restart and quit** (`9300ba44`). The status menu opens after
  AppKit finishes tracking the mouse click. Once the viewer and host finish
  shutting down, Quit and Restart exit the event loop directly so the resident
  window's close-to-hide behavior cannot cancel termination.
- **Idle text-caret updates** (`3315da47`). Subscribed clients still receive
  periodic updates while a caret is active. When it disappears, the host sends
  one update and suppresses repeated idle notifications.
- **Data saver refinement and pacing** (`f8aa41d3`, `abfa8575`, `1d7df96b`).
  Eligible VideoToolbox sessions use a quantization ceiling of 34 for a new
  non-key picture and 26 for its first stationary refinement. Later refinements,
  keyframes and retries can use the full range. Live quality diagnostics now
  accept both ceilings. The host admits changed content at up to
  16 fps during input activity and up to 8 fps otherwise. VideoToolbox uses a
  2× burst multiplier in saver mode, or 1× when constrained. Remote saver
  sessions pace packets at three times the live bitrate target with an 8 Mbps
  floor.
- **Build maintenance** (`7f777d68`). The desktop build removes a duplicate Qt
  resource entry, addresses compiler warnings and adjusts qmake's macOS SDK
  warning handling.
- **Pairing and downloads** (`65d75c3c`, `31cea5e0`). The README clarifies how
  to pair a DeskPort client with its bundled host and which Linux package to
  install. The 2.9.0 download tables were updated to Android build 67.

## Desktop downloads

Choose a package from the [v2.9.1 release](https://github.com/keithxc/deskport/releases/tag/v2.9.1).

| Target | Downloads | Scope |
| --- | --- | --- |
| macOS arm64, macOS 26+ | DMG and ZIP | Viewer, bundled host and native virtual display. Public distribution requires Developer ID signatures, Apple notarization, stapling and Gatekeeper verification. |
| Windows x64 | Setup and portable ZIP | Setup installs the viewer, bundled host and virtual display integration. The portable ZIP provides viewing. The application and installer are unsigned. |
| Linux x86_64 | DEB, RPM, Arch, AppImage and tar.gz | Viewer and bundled host. Native packages and tar.gz require glibc 2.39+. AppImage carries a private runtime. |
| Linux x86_64 Flatpak | Flatpak bundle | Viewer only, Freedesktop Platform 25.08. |
| Nix | `nix run github:keithxc/deskport/v2.9.1` | Pinned-source Linux build. See [Nix installation](../LINUX_PACKAGES.md#nixos-and-nix). |

Fully quit the previous application before upgrading. Verify the exact package
with that release's `SHA256SUMS.txt`. See the
[Linux package guide](../LINUX_PACKAGES.md) for capture, desktop and input
requirements.

The Android download listed alongside the desktop packages is
**2.9.1 (71)** (`DeskPort-2.9.1-71.apk`).

## Verification

The desktop source pins shared core `49c13048b9e91b8638335a87d20cf9c545039b6d`.
For 2.9.1, match the package to its entry in `SHA256SUMS.txt` and the source
revision in `PROVENANCE.json`. Consult the matching `VERIFICATION.txt` and
`VERIFICATION-linux.txt` for the checks performed on that package.

The [2.9.0 verification report](https://github.com/keithxc/deskport/releases/download/v2.9.0/VERIFICATION.txt)
and [Linux report](https://github.com/keithxc/deskport/releases/download/v2.9.0/VERIFICATION-linux.txt)
cover the earlier release's macOS signing and notarization, Windows installer
checks and Linux package checks. The [PC support matrix](../PC_SUPPORT_MATRIX.md)
records platform evidence by source and package. End-to-end input latency,
scrolling quality and display restoration require live tests on the specific
client and host hardware.
