# DeskPort 2.9.0

Stable desktop release for macOS Apple Silicon, Windows x64 and Linux x86_64.
The desktop client and host have matured through the 0.7.x releases. This
release moves desktop numbering directly to 2.9.0 to align with the iOS and
Android clients. The downloads cover desktop platforms.

## Changes since 0.7.8

- **Portable desktop policy, preserving behavior** (`86020e9a`): desktop
  workspace sizing, stream/budget arithmetic and parsed acknowledgment checks
  now use the shared portable core. Existing drawable floors, display-mode
  ranking, 8 fps saver bitrate policy, older-host fallback/recovery and SDL
  keyboard behavior remain intact. This is a policy migration, not a new set
  of streaming modes. The repository includes differential coverage against
  the pinned pre-migration sources.
- **macOS session shutdown** (`32053a05` and follow-ups): fence video/audio
  senders before retiring a stream session, discard only that session's queued
  packets, retain stopped queues through worker shutdown and join the final
  broadcast owner while its session resources remain alive. Event/queue reads
  are synchronized and expired mailbox entries are replaced safely. These
  changes address shutdown lifetime faults during resize/reconnect; they do
  not replace the existing shutdown watchdog.
- **Client-driven keyboard repeat** (`70a3aa87`): desktop clients mark key
  events and forward eligible local repeat presses; compatible hosts re-emit
  those presses without starting a second host repeat timer. Modifier/lock
  keys, shortcut chords and locally consumed text input are excluded. Older
  hosts and unmarked clients retain host-timed repeat.
- **Background viewing and power recovery** (integrated in `5350d601`):
  retained viewers use authenticated shell visibility for canvas sessions,
  fence input timers and release held input on lifecycle transitions. Native
  sleep/lock events feed quiet recovery; wake/unlock does not raise the viewer,
  change saved settings or initiate an unauthenticated host session. Native
  occlusion is limited to supported native SDL presentation paths. See
  [background sessions](../BACKGROUND_SESSIONS.md) and
  [client power recovery](../CLIENT_POWER_RECOVERY.md).
- **Browser playback and sizing recovery** (`5350d601`): recover from blocked
  video/audio playback with a user-gesture action, report unsupported media,
  refresh sizing after viewport/display-density changes and terminate congested
  input safely. Shared localized recovery messages are included. Browser,
  certificate, touch/audio and host-platform acceptance remain scoped to the
  [browser compatibility records](../BROWSER_COMPATIBILITY.md).
- **Encoder and codec qualification** (`5350d601`): integrate acknowledged
  live-rate transactions with rollback on rejection, reconnect fallback for fixed backends, exact
  codec/profile qualification and native ten-bit color conversion. VideoToolbox
  transactions preserve the existing saver cadence, burst and quality bounds;
  equal-rate saver changes also require encoder acknowledgment. D3D11 checks
  HDR10 presentation support before selecting that output path, and macOS
  qualifies decoding with a hardware-only VideoToolbox session. These paths
  do not establish end-to-end HDR or a performance improvement on every GPU.
  See [live encoder rate](../LIVE_ENCODER_RATE.md) and
  [HDR qualification](../HDR_CODEC_QUALIFICATION.md).
- **Desktop dialogs and integration checks** (`5350d601`): improve RTL
  navigation, message scrolling, keyboard focus and button text after language
  changes. Integrate synthetic clipboard regression tools, checked Windows
  installer helper launches and additional macOS topology/recovery fixtures.
  These source and fixture improvements do not establish current full Windows
  lifecycle, clipboard or cross-platform hardware parity. See
  [desktop typography](../DESKTOP_TYPOGRAPHY.md),
  [PC regression](../PC_CORE_REGRESSION.md),
  [Windows installation](../WINDOWS_INSTALL_LIFECYCLE.md) and
  [macOS lifecycle](../MACOS_LIFECYCLE_ACCEPTANCE.md).
- **Shared manual and terminology** (`0bd08426`, `0745ab9e`, `37cfd10b`,
  `bfa74494`): fill previously English-only offline manual steps in the shared
  catalog, use consistent binding terminology, correct Polish UI translations
  and tuning guidance, and remove obsolete text-only clipboard descriptions.
  The shared manual is also available on the project site. Native-language
  review remains separate from catalog completeness.

The planned **Standard / Game / Data saver** selector is deferred; the existing
data saver switch remains the release behavior. **Seamless** remains an explicit
experiment: integrated lab/supervisor work does not deliver the admitted product
transport or a supported stable Seamless mode. See the
[roadmap](../ROADMAP.md) and [integration ledger](../PC_TASK_INTEGRATION.md).

## Desktop downloads

Choose a package from the [v2.9.0 release](https://github.com/keithxc/deskport/releases/tag/v2.9.0).

| Target | Downloads | Scope |
| --- | --- | --- |
| macOS arm64, macOS 26+ | DMG and ZIP | Viewer, bundled host and native virtual display. Developer ID distribution with Apple notarization. |
| Windows x64 | Setup and portable ZIP | Setup installs the viewer, bundled host and virtual display integration. The portable ZIP provides viewing. The application and installer are unsigned. |
| Linux x86_64 | DEB, RPM, Arch, AppImage and tar.gz | Viewer and bundled host. Native packages and tar.gz require glibc 2.39+. AppImage carries a private runtime. |
| Linux x86_64 Flatpak | Flatpak bundle | Viewer only, Freedesktop Platform 25.08. |
| Nix | `nix run github:keithxc/deskport/v2.9.0` | Pinned-source Linux build. See [Nix installation](../LINUX_PACKAGES.md#nixos-and-nix). |

Fully quit the previous application before upgrading. Verify the exact package
with `SHA256SUMS.txt`. See the [Linux package guide](../LINUX_PACKAGES.md) for
capture, desktop and input requirements.

## Verification

`PROVENANCE.json` records the exact desktop source revision and shared core
`49c13048b9e91b8638335a87d20cf9c545039b6d`. `VERIFICATION.txt` and
`VERIFICATION-linux.txt` give the scope of package, CI and native checks.
Upstream notices are included in the packages.

The [PC support matrix](../PC_SUPPORT_MATRIX.md) summarizes platform and
compositor support. Reconnect, physical input, clipboard, audio, HDR and
display restoration depend on the client and host hardware. The
[roadmap](../ROADMAP.md) records rotation stress checks for earlier signed
macOS test builds.
