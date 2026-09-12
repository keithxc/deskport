# DeskPort 0.1.10

2026-09-12. This update hardens session cleanup and adaptive resize recovery.

## Changes

- Executing sessions stay alive until both the streaming call and asynchronous
  transport cleanup finish, even when QML destroys the stream page. This closes
  a use-after-free risk during disconnect; the historical heap corruption still
  needs real-session regression coverage.
- Adaptive resize /resume requests time out after 15 seconds instead of the
  two-minute application-launch timeout, with a translated reconnect message.
  Normal application launch behavior is unchanged.
- Late stream-page callbacks safely handle a destroyed context.
- Linux --help and --version work without a configured display by selecting Qt's
  offscreen platform. Explicit platform selections remain respected.
- Native Wayland keeps VAAPI eligible instead of preferring unavailable VDPAU.
  The conservative Gallium RFI workaround is retained. Actual decoder selection,
  latency and power consumption need native acceptance.
- macOS UI/service test builds include the native status-menu implementation.
  New startup-management and resize-failure messages have seven translations.
- Host IDR failure logs include submitted/returned PTS and encoder name. This is
  diagnostic only; it does not change encoder settings or encoded packets.

## Validation

- Linux Nix build and headless --version/--help checks.
- Linux and macOS: 14 UI checks, 7 service checks and 6 desktop-state/lifetime checks.
- Seven-language catalog coverage and git whitespace checks.
- macOS arm64 package: signing preflight in the Aqua session, stable development
  identity, strict bundle/dependency verification and extracted-zip signature check.
- Synthetic 4K HEVC VideoToolbox encoding with the host's pinned FFmpeg libraries:
  six requested keyframes returned successfully. This does not reproduce the
  current real-session IDR warnings or prove their root cause.

## Acceptance still required

After updating both endpoints, run 30 alternating resizes, 20 reconnect cycles
and one two-hour session. Check input release, image recovery, decoder selection
and any recurring heap or IDR errors. Preserve logs for correlation.

The installed applications and running services are not restarted by this release
preparation. Switch/restart them when ready for native acceptance. The macOS asset
is a development preview using the existing local signing identity, without
Developer ID notarization.
