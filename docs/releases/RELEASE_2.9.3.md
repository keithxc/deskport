# DeskPort 2.9.3

Desktop release for macOS Apple Silicon, Windows x64 and Linux x86_64.
This update fixes limited-range H.264/HEVC color rendering in `PlVkRenderer` on
Vulkan/libplacebo viewers and carries forward all improvements introduced in
2.9.2 since 2.9.1. The changes below cover the public desktop source from
v2.9.1 through `7bd59906`.

## Changes since 2.9.2

- **Accurate H.264/HEVC color range in `PlVkRenderer`** (`7bd59906`).
  `PlVkRenderer::mapAvFrameToPlacebo()` now scopes the upstream full-range
  override to AV1 streams (preserving the Windows AMF AV1 workaround) via
  `resolvePlVkColorLevels()` instead of forcing `PL_COLOR_LEVELS_FULL` on every
  codec. Limited-range H.264 and HEVC streams (such as macOS VideoToolbox
  output) now keep `PL_COLOR_LEVELS_LIMITED`, preventing lifted blacks and
  washed-out colors on Linux and Vulkan viewers while preserving explicit
  full-range and RGB mappings. Automated regression coverage in
  `tests/test-plvk-color.py` is wired into Linux CI.

## Changes since 2.9.1 (included from 2.9.2)

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

Choose a package from the [v2.9.3 release](https://github.com/keithxc/deskport/releases/tag/v2.9.3).

| Target | Downloads | Scope |
| --- | --- | --- |
| macOS arm64, macOS 26+ | DMG and ZIP | Viewer, bundled host and native virtual display. Public distribution requires Developer ID signatures, Apple notarization, stapling and Gatekeeper verification. |
| Windows x64 | Setup and portable ZIP | Setup installs the viewer, bundled host and virtual display integration. The portable ZIP provides viewing. The application and installer are unsigned. |
| Linux x86_64 | DEB, RPM, Arch, AppImage and tar.gz | Viewer and bundled host. Native packages and tar.gz require glibc 2.39+. AppImage carries a private runtime. |
| Linux x86_64 Flatpak | Flatpak bundle | Viewer only, Freedesktop Platform 25.08. |
| Nix | `nix run github:keithxc/deskport/v2.9.3` | Pinned-source Linux build. See [Nix installation](../LINUX_PACKAGES.md#nixos-and-nix). |

Fully quit the previous application before upgrading. Verify the exact package
with that release's `SHA256SUMS.txt`. See the
[Linux package guide](../LINUX_PACKAGES.md) for capture, desktop and input
requirements.

The Android download remains **2.9.1 (71)** (`DeskPort-2.9.1-71.apk`) on the
[v2.9.1 release](https://github.com/keithxc/deskport/releases/tag/v2.9.1).

## Verification

The desktop source pins shared core `49c13048b9e91b8638335a87d20cf9c545039b6d`.
For 2.9.3, match the package to its entry in `SHA256SUMS.txt` and the source
revision in `PROVENANCE.json`. Consult the matching `VERIFICATION.txt` and
`VERIFICATION-linux.txt` for the checks performed on that package.

The [PC support matrix](../PC_SUPPORT_MATRIX.md) records platform evidence by
source and package. End-to-end color rendering, input latency, scrolling
quality, virtual-display parking and restoration require live tests on the
specific client and host hardware.

### Follow-up native Windows verification (2026-11-28)

On a dedicated Windows 11 x64 test machine (AMD Radeon 780M), the published
`DeskPort-2.9.3-windows-x64-setup-full.exe` and
`DeskPort-2.9.3-windows-x64-portable-full.zip` matched `SHA256SUMS.txt` before
and after scanning. Portable ZIP checks passed PE version (`2.9.3.0`), CLI
`--version`/`-v`/`--help`/`-h`, valid Authenticode signature on
`driver\mttvdd.cat`, and a Microsoft Defender custom scan with real-time
protection enabled and zero exclusions (677 extracted files, 0 detections).
An in-place Setup upgrade from `2.9.1.0` to `2.9.3.0` exited 0 and started the
resident host and display service.

Bidirectional live sessions between the installed Windows `2.9.3` package and an
Apple Silicon macOS `2.9.3` host/client verified:

- **Windows client -> macOS host:** dynamic `2224x1264` virtual display,
  VideoToolbox HEVC encode, D3D11VA (`hevc_d3d11va2`) hardware decode, first
  video packet in 300 ms, 46-58 fps steady rendering with 0% network drops,
  live desktop display verification, and clean physical display restoration on
  disconnect.
- **macOS client -> Windows host:** negotiated `1848x1012` virtual display,
  `amdvce` HEVC encode, Apple Silicon Metal (`vt_renderer.metal`) +
  VideoToolbox hardware decode, first video packet in 500 ms, 0% transport
  drops, and clean display-lease release on disconnect.

The Windows application and installer remain unsigned, and broader GPU, audio,
clipboard, HDR, IME and long-session combinations remain hardware-specific.
