# DeskPort 0.1.1

Released 2026-09-10. A simpler desktop-first connection workflow.

## Changes

- Click a paired device to connect directly to Desktop; disconnect returns to the
  device list. Applications remain available from the device menu. Existing
  non-desktop sessions are never terminated automatically.
- Retain entered DNS names when binding, recover names from older bindings and
  accept them in the device-list importer. Certificate pinning remains enforced.
- Limit automatic discovery to DeskPort's reserved base-port family. Older
  unpaired auto-discovered services on unrelated ports are hidden without deleting
  their saved data. Manual and paired custom-port services remain supported.
- Disable host audio streaming by default, with an explicit opt-in switch.
- Remember client workspace/input choices and use smaller default video packets
  on constrained links. Explicit user settings remain supported.
- Update the translated interface and isolated regression coverage.

## Validation and limits

Linux Nix builds, macOS compilation, 12 binding checks, 26 host lifecycle checks,
10 QML checks, desktop preference checks and seven-language coverage passed during
release preparation. Signed macOS bundles are checked for unresolved build-machine
libraries. Actual capture, input, sound and long-running session quality still
require native acceptance; isolated tests are not a substitute for that.

DNS retention does not repair proxy routing or path-MTU problems. macOS remains
Apple Silicon/macOS 26 only, signed with a development identity and not notarized.
The macOS package retains the pinned, previously validated host/display helpers.

## Install

Linux: use `github:keithxc/deskport/v0.1.1` as a Nix flake input, or run
`nix build github:keithxc/deskport/v0.1.1`.

macOS: extract `DeskPort-0.1.1-macos-arm64.zip` and install `DeskPort.app` in
Applications. SHA-256 checksums accompany the release. Pairing state and OS
permissions remain in DeskPort's independent application identity.
