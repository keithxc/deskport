# DeskPort 0.1.12

Smart desktop streaming reduces work on unchanged Mac screens and adds a more
conservative client bandwidth budget and smooth frame pacing.

- **Static-screen savings (Mac host, enabled by default):** compare every visible
  byte of BGRA/NV12/P010 frames, including chroma, and omit exact duplicates before
  encoding. Refresh the capture snapshot at least once a second and keep an idle
  encoding target of at most 5 fps instead of the previous half-stream-rate floor
  (30 fps for a 60 fps stream). A changed pixel immediately becomes eligible for
  delivery. Screen capture and comparison remain active; this is not zero-power
  idle or a guaranteed bandwidth percentage.
- **Congestion response (Mac host):** consume existing session-scoped Sunshine FEC
  reports. Three loss episodes within a rolling burst window reduce the frame-rate
  ceiling, with an eight-second adjustment cooldown and a 15 fps floor (or the
  requested rate if lower). Frames are omitted before encoding, never after encoding
  where reference-frame loss would corrupt subsequent pictures. Recovery requests
  bypass throttling. After 30 seconds without loss and enough active frames,
  increase one step. No automatic resolution change or session reconnect is used.
- **Smart client defaults:** use a resolution/frame-rate-aware initial bandwidth
  ceiling (15 Mbps at 2560×1440/60 fps), never above the saved user limit. Enable
  frame pacing while display synchronization is enabled. Existing manual values
  remain saved; turning Smart streaming off restores their use.
- **Diagnostics:** expose pacing and streaming-statistics controls. Log recent
  statistics about every ten seconds. Rename the presentation-queue drop counter
  so it is not incorrectly presented as proof of network jitter.
- Seven desktop-language catalogs include the new controls.

The adaptation in this release changes **output cadence**, not the encoder's
live bitrate parameter. The bandwidth control is a startup ceiling. It does not
infer network congestion from low-content frame rates or presentation drops.
Linux hosts retain their upstream capture/encoding behavior; the Mac-host savings
require updating and restarting sharing on the Mac as well as updating the viewer.
The host savings/congestion mode can be disabled in Sharing preferences.

## Validation

- Linux `nix build` and native macOS host compilation passed during development.
- Policy tests cover burst hysteresis, floor/cooldown/recovery, keyframe priority,
  malformed/repaired FEC reports, padding, exact single-byte changes, and bitrate limits.
- Native synthetic CoreVideo buffers exercise BGRA, NV12 and P010, including
  single-byte changes in the final luma/chroma row. On the development Mac, 1440p
  duplicate comparison measured approximately 0.13–0.31 ms per frame; these are
  synthetic timings, not end-to-end stream or power measurements.
- Final packaging and platform test results are recorded in `docs/ROADMAP.md`.

## Try and roll back

After updating both devices, restart DeskPort and reconnect. Compare ten minutes
of a static editor, scrolling, and video at the same window size. The idle receive
rate is intentionally lower. Use the statistics overlay/logs to compare network
loss, presentation drops, decode time and visible pauses. Disable Smart streaming
or the Mac host savings option independently for an A/B comparison.

Native video smoothness, actual idle traffic/power and long-session acceptance
remain user checks. This release does not claim to resolve every historical IDR
or heap-corruption report. Roll back to 0.1.11 without deleting pairing/settings.

macOS assets retain the existing Apple Development identity. They are development
preview builds, not Developer ID notarized distribution packages.
