# DeskPort 2.9.2

Desktop release for macOS Apple Silicon, Windows x64 and Linux x86_64.
This update improves macOS host pointer injection latency, VideoToolbox tail
delivery, Wi-Fi burst pacing, mobile background virtual-display parking, and
desktop/host idle resource efficiency. The changes below cover the public
desktop source from v2.9.1 through `c9e9d18f`.

## Changes since 2.9.1

- **macOS host input injection, tail latency, and Wi-Fi burst pacing** (`c9e9d18f`).
  The macOS `libvirtualhid` input backend sets
  `CGEventSourceSetLocalEventsSuppressionInterval` to `0.0`, caches cursor
  coordinates for 80 ms during continuous motion instead of querying
  WindowServer per packet, and reserves `CGWarpMouseCursorPosition` for
  cross-display transitions. On macOS, VideoToolbox completes the tail frame
  when the capture queue drains. Data Saver raises its interactive frame-rate
  ceiling from 16 fps to 32 fps. UDP packet bursts are paced on both Wi-Fi LAN
  and remote paths to avoid AP queue bursts when Data Saver is off, and
  reference-invalidation keyframe gating applies consistently across smart and
  standard streams.
- **Mobile background virtual-display parking** (`61cda776`). When a mobile
  client releases its display lease with `suspended: true`, the host parks the
  managed virtual display instead of tearing it down immediately, keeping open
  windows in place for faster resume. The host restores the physical display on
  local macOS keyboard/mouse activity, another client's handover, policy change,
  access revocation, or inactivity timeout.
- **Desktop and host resource efficiency** (`f7455c8a`, `2046e049`). The viewer
  reuses a single persistent snapshot worker thread per `Pacer`, removes
  exhausted `MdnsPendingComputer` timeout entries, parks `Pacer::vsyncThread`
  when idle, periodically flushes the `VTMetalRenderer` texture cache, pauses
  SDL audio while muted, and replaces polling timers across `SessionWorker`,
  `PcMonitorThread`, `ClipboardChannel`, `BrowserGateway`, `BrowserHost`,
  `PeerManager`, and `HostManager` with event-driven wakes. The host skips GPU
  color conversion while `video_encode_paused`, switches encoder and PipeWire
  idle loops to event-driven waits, optimizes macOS P010 Metal conversion with
  16×16 threadgroup shared memory, reduces ScreenCaptureKit `queueDepth` to 3,
  relaxes post-first-frame capture heartbeats, and deduplicates/suspends display
  helper caret polling.

## Desktop downloads

Choose a package from the [v2.9.2 release](https://github.com/keithxc/deskport/releases/tag/v2.9.2).

| Target | Downloads | Scope |
| --- | --- | --- |
| macOS arm64, macOS 26+ | DMG and ZIP | Viewer, bundled host and native virtual display. Public distribution requires Developer ID signatures, Apple notarization, stapling and Gatekeeper verification. |
| Windows x64 | Setup and portable ZIP | Setup installs the viewer, bundled host and virtual display integration. The portable ZIP provides viewing. The application and installer are unsigned. |
| Linux x86_64 | DEB, RPM, Arch, AppImage and tar.gz | Viewer and bundled host. Native packages and tar.gz require glibc 2.39+. AppImage carries a private runtime. |
| Linux x86_64 Flatpak | Flatpak bundle | Viewer only, Freedesktop Platform 25.08. |
| Nix | `nix run github:keithxc/deskport/v2.9.2` | Pinned-source Linux build. See [Nix installation](../LINUX_PACKAGES.md#nixos-and-nix). |

Fully quit the previous application before upgrading. Verify the exact package
with that release's `SHA256SUMS.txt`. See the
[Linux package guide](../LINUX_PACKAGES.md) for capture, desktop and input
requirements.

The Android download remains **2.9.1 (71)** (`DeskPort-2.9.1-71.apk`) on the
[v2.9.1 release](https://github.com/keithxc/deskport/releases/tag/v2.9.1).

## Verification

The desktop source pins shared core `49c13048b9e91b8638335a87d20cf9c545039b6d`.
For 2.9.2, match the package to its entry in `SHA256SUMS.txt` and the source
revision in `PROVENANCE.json`. Consult the matching `VERIFICATION.txt` and
`VERIFICATION-linux.txt` for the checks performed on that package.

The [PC support matrix](../PC_SUPPORT_MATRIX.md) records platform evidence by
source and package. End-to-end input latency, scrolling quality, virtual-display
parking and restoration require live tests on the specific client and host
hardware.
