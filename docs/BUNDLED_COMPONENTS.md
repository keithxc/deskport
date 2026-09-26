# Bundled components

DeskPort is an independent derivative of Moonlight Qt, initially v6.1.0, under
GPL-3.0-or-later. Source and modifications: https://github.com/keithxc/deskport
Upstream: https://github.com/moonlight-stream/moonlight-qt/tree/v6.1.0
The virtual-display helper is part of the DeskPort source tree.

The macOS package builds a modified Sunshine v2026.906.222525 host from commit
cb72dffa3233c5815cd5ba88f09f049dd679ba75. Its libvirtualhid dependency at commit
6fdb8bd4de3b68d96c30e5303ac2ebb333c09746 receives the patch in
`host/macos/patches/libvirtualhid-target-display.patch`: input follows DeskPort's
verified capture display without changing the physical main display.
`sunshine-capture-timeout.patch` bounds the capture first-frame wait;
`sunshine-idr-diagnostics.patch` adds submitted/returned frame timestamps to
keyframe failure logs without changing encoder settings or packet contents.
`sunshine-pkgconfig-link.patch` links miniupnpc using the full path resolved by
pkg-config, including Nix store libraries.
`sunshine-smart-streaming.patch`, `host/common/smartstream.h`, and
`host/macos/pixelmatch.h` add exact static-frame suppression and session-scoped
FEC-driven frame-rate limits. The new patch and headers are also included in the
bundled host resources under `deskport-smart-source`.
`scripts/build-macos-host.sh` reproduces this host build. Use the locked macOS
devShell for CMake, pkg-config, OpenSSL, ICU, Opus, miniupnpc and Boost, plus
Apple Xcode; see `docs/MACOS_PACKAGE.md`. Upstream source,
dependency gitlinks, and license notices:
https://github.com/LizardByte/Sunshine/tree/cb72dffa3233c5815cd5ba88f09f049dd679ba75
The checksum-verified official package supplies same-version web assets and notices.
The modified nested host is signed using the configured DeskPort identity, not the
upstream publisher's signature. Independently installed Sunshine is never modified.
Changing the nested host's signing identity may require macOS privacy approval once;
subsequent builds retain the same configured identity.

The Linux Nix package references Sunshine 2026.516.143833 from the locked nixpkgs
revision as a separate runtime dependency. Upstream source:
https://github.com/LizardByte/Sunshine/tree/v2026.516.143833

The 0.6.3 portable Linux AppImage, DEB, RPM and pacman packages build Sunshine
2026.906.222525 from commit cb72dffa3233c5815cd5ba88f09f049dd679ba75 using
`scripts/build-linux-host.sh`. The session-settings, session-takeover and
Linux-display patch scripts are shared with the Nix build.
`host/linux/patches/sunshine-gcc13-log.patch` replaces one C++23-only debug-log
formatting expression with equivalent formatting supported by Ubuntu 24.04. The checksum-pinned
upstream AppImage supplies same-version assets, runtime libraries and notices;
its executable is replaced with the patched host and required libraries are
collected from the build environment. The host remains in a separate
`usr/libexec/sunshine` tree. DeskPort's launcher supplies private configuration
without invoking upstream installation/service commands.
Source: https://github.com/LizardByte/Sunshine/tree/cb72dffa3233c5815cd5ba88f09f049dd679ba75
Asset URLs and SHA-256 values are in `scripts/linux-tools.json`. The host
uses the same pinned FFmpeg fb216b5 binaries as Nix, with three Vulkan lifetime
backports in `host/linux/ffmpeg-vulkan-*.patch`. The portable build recompiles
only four affected units against matching source and Vulkan headers, after
comparing ABI headers. Source and relinking materials accompany the release.

Portable viewer Qt 6, SDL2, FFmpeg and supporting libraries come from the Ubuntu
24.04 build environment. The build emits `build-packages.txt` with exact package
versions, and linuxdeploy retains available distribution copyright files.
The Flatpak reuses the portable viewer inside Freedesktop Platform 25.08 and
does not include or escape the sandbox to launch Sunshine. Its host features
and desktop login service integration are outside this package's scope.

Qt is deployed as dynamically linked frameworks from the build environment. Qt
source and licensing: https://www.qt.io/licensing/open-source-lgpl-obligations
SDL, FFmpeg, OpenSSL, Opus and other Moonlight dependencies retain upstream notices
and versions from the pinned Moonlight dependency tree. See [upstream README](readme/README.upstream.md),
LICENSE and the vendored dependency licenses listed in VENDORED.md, all of which
are present in the source distribution.

Development builds use the configured local identity. Developer ID distribution
uses `scripts/release-macos.sh` to re-sign all bundled executable code under the
distributor's team, without borrowing an upstream publisher's identity. A valid
signature alone does not establish notarization: release artifacts must also pass
Apple notarization, stapled-ticket validation and Gatekeeper assessment. Public
release packaging must include corresponding sources, exact dependency versions
and license notices.

## ScreenCaptureKit overlay (0.1.13)

`host/macos/patches/sunshine-screen-capture-kit.patch` follows the smart-streaming
patch and removes its CPU pixel comparison. `host/macos/screen-video.h` and
`screen-video.m` implement native BGRA/NV12 capture, idle lifecycle callbacks and
retained-surface refresh. They are copied into the host's corresponding-source
resources. P010 retains upstream AVFoundation capture without pixel comparison.
ScreenCaptureKit and QuartzCore are operating-system frameworks.

## Operating-system marks (0.2.7)

The OS marks in `app/res/os` come from Simple Icons 11.15.0
(https://github.com/simple-icons/simple-icons/tree/11.15.0), distributed under
CC0-1.0; the license is bundled alongside them. Colors are adapted for the cards.
The generic computer mark comes from the existing bundled Material icon set.
The DeskPort application mark remains the existing project asset.


## Portable DeskPort catalog — 2026-09-20

The generated product catalog is independently MIT-licensed. Its full notice is
embedded at `:/licenses/deskport-catalog.txt` and retained in the pinned core
`portable/LICENSE`. Existing GPL workspace and upstream licenses are unchanged.
