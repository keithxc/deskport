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
`scripts/build-macos-host.sh` reproduces this host build (Xcode, CMake, pkg-config,
Homebrew OpenSSL 3, ICU 78, Opus and miniupnpc are required). Upstream source,
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

Qt is deployed as dynamically linked frameworks from the build environment. Qt
source and licensing: https://www.qt.io/licensing/open-source-lgpl-obligations
SDL, FFmpeg, OpenSSL, Opus and other Moonlight dependencies retain upstream notices
and versions from the pinned Moonlight dependency tree. See README.upstream.md,
LICENSE and the relevant submodule licenses in the source distribution.

This is a local development preview signed locally using the configured identity. It has no
Developer ID distribution signing or notarization. Public release packaging must
include corresponding sources, exact dependency versions and license notices.
