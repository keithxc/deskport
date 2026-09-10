# DeskPort 0.1.0

Released 2026-09-10. First versioned release of DeskPort.

## Included

- Independent application identity, settings, bindings and host lifecycle.
- Device list, mutual binding, sharing controls, permissions and language selection.
- macOS Apple Silicon package with Sunshine and a native virtual-display helper.
- Linux Nix package with viewer and Sunshine hosting for the existing desktop.
- Adaptive macOS workspace based on client logical dimensions, defaulting to 1×
  for startup, automatic resizing and idle restore.
- Client window retained with a loading animation during resize reconnection.
- Host port conflict handling and coexistence with independently installed services.

## Validation and limits

The release includes the 1× workspace correction. Twelve binding/workspace checks,
26 host lifecycle checks, macOS packaging/signature/dependency verification and
Linux Nix build validation passed. A host-side capture in the tested 1× mirror
arrangement showed the complete desktop.

Repeated end-to-end resize, pointer accuracy, long sessions, permission retention
across updates and physical mirror changes still need acceptance. The native
isolated-display test stopped after observing a mirror-mode change; it is not
counted as a successful repeated-resize test. Lower pixel counts do not guarantee
proportional bandwidth or latency improvements; bitrate settings still apply.

Resizing reconnects the video stream. Persistent hide/show without reconnecting,
automatic physical-display topology restoration, shared clipboard and Windows
packaging are not included. Linux ARM64 has a package definition but no native
runtime acceptance. macOS builds use a local development signing identity;
notarized public distribution is not established by this tag.

## Build

```sh
git clone --branch v0.1.0 https://github.com/keithxc/deskport.git
cd deskport
nix build
python3 scripts/deskport-smoke.py ./result
```

See [macOS packaging](MACOS_PACKAGE.md) for the Apple Silicon build procedure.
