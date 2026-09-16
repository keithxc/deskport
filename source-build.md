# DeskPort Apple source distribution

Version: 1.0, builds 1, 2, 3 and 4. Copyright (C) 2026 毛尔昊.
DeskPort integration is GPL-3.0-or-later. Upstream copyright and license notices
remain applicable. This is a modified Moonlight client, not an official Moonlight
release. No warranty is provided.

Public downloads: https://keithxc.github.io/deskport/source.html

## Contents

Each versioned archive contains the complete generated Apple Xcode project under
`Client/`, including the native client, protocol core, ENet, Reed-Solomon code,
X1Kit, assets and the exact upstream static libraries used by that build.
`Integration/` contains the editable DeskPort Apple/Shared sources and integration
scripts. `Dependencies/` contains complete version-matched FFmpeg, SDL, Opus and
OpenSSL sources, plus the original media and OpenSSL packaging build scripts.
`Licenses/` and `NOTICES.txt` preserve the applicable notices. `MANIFEST.json`
records source pins and artifact hashes; `SHA256SUMS` covers the delivered files.
There is no private development history, signing material, device data or capture
content in these source archives.

Build 4 filters native Sunshine services, uses DeskPort approval binding, and adds
identity-preserving domain/IP editing. The discovery transport is unchanged.

Build 3 changes the offline notices and fixes the existing OpenSSL package version
at 3.3.2000 (OpenSSL 3.3.2). App behavior is unchanged from build 2. Build 2's original
bundled notices are retained in its project; the additional distribution notices
are supplied alongside it in NOTICES.txt and Licenses/.

Build 1 is also supplied for recipients of the initial TestFlight build. Its project
retains the original upstream public-address discovery behavior, which was removed
in build 2. The build-1 source is generated from the integration immediately before
that removal; later documentation-only changes do not affect the app.

## Build, modify and install

Requirements: macOS, full Xcode with iOS SDK, Apple's command-line developer tools.
The release was archived with Xcode 27.0. Install any SDK components requested by
Xcode. The app supports iOS/iPadOS 15 or later. No DeskPort account is required.

Open `Client/Moonlight.xcodeproj`, select `DeskPortStore`, and build. The target and
app product retain their upstream internal name `Moonlight`; the installed display
name and icon are DeskPort. Select your own signing team and unique bundle ID for
installation on your devices. You do not need the publisher's certificates or keys.
Use Xcode's normal Run workflow and follow the device's developer-mode/trust prompts.

For an unsigned simulator build from the extracted archive root:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcodebuild -project Client/Moonlight.xcodeproj -scheme DeskPortStore \
  -configuration Release -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath Build.noindex CODE_SIGNING_ALLOWED=NO \
  MARKETING_VERSION=1.0 CURRENT_PROJECT_VERSION=4 build
```

Use CURRENT_PROJECT_VERSION=1, 2 or 3 for the corresponding earlier archive. To make a modified version,
edit the complete sources directly in Client and rebuild. Xcode resolves the exact
OpenSSL-Package version recorded in Package.resolved and downloads the artifact
whose SHA-256 is recorded in its Package.swift. No private repository is required.

## Dependency sources and rebuilding

- Moonlight iOS: 85af0f75622bb2636481afda8b0fc5cc33d5956e.
- moonlight-common-c: 48d7f1ace168d07dad339718f1350979545f15d2.
- ENet: 0032f5e750bda820fb90b25f99af65fc07ca978a.
- X1Kit: 6e842ae9d5a21916d7c9693c7385d1ac672bc4fa.
- FFmpeg: 3f890fbfd9014843c51408c8f7ab3ba4aef7d354. Its binary identifies itself
  as N-112686-g3f890fbfd9 and LGPL version 2.1 or later. Configure flags enable
  avcodec, avformat, the FLV muxer and AV1 decoder, with `--disable-all`,
  `--disable-autodetect` and `--disable-x86asm`; no GPL/nonfree codec option is used.
- Opus: official 1.4 source release, matching the embedded `libopus 1.4` string.
- SDL: 2.28.5, commit 15ead9a40d09a1eb9972215cceac2bf29c9b77f6.
- The matching upstream media build recipe is moonlight-mobile-deps commit
  dad1ce6d964b6d4cc61116f1b7170954eb08ef20. It records OS_MIN=12.0 and OPUS_VERSION=1.4.
  The embedded FFmpeg build configuration identifies Xcode 14.1 / iOS SDK 16.1.
  These establish source/recipe provenance; a bit-for-bit rebuild of those older
  upstream prebuilt libraries with current Xcode is not claimed.
- OpenSSL: 3.3.2, with krzyzanowskim/OpenSSL 3.3.2000 packaging sources, patches and
  scripts. The Swift wrapper is OpenSSL-Package commit
  b9eb055fdf73e595cb4b9f665d13dc85975bf80a. Its binary artifact SHA-256 is
  41d034ea1c075bfa74048e851358a550996c286de8230d1df39f137b06235c87.

To rebuild the media dependencies, extract the FFmpeg and SDL archives to the
`FFmpeg` and `SDL` directories inside a disposable copy of
`Dependencies/moonlight-mobile-deps`, and put `opus-1.4.tar.gz` alongside `opus.sh`.
Use its `appveyor.yml` for architecture, SDK and invocation order. Its final
FFmpeg cleanup uses Git reset/clean: omit those two cleanup lines for an extracted
source archive, or create a separate temporary Git checkout of that dependency.
Never run cleanup commands in an unrelated working directory. `archive.sh` lists
where to copy the resulting static libraries and headers into Client/libs.
The scripts use `make` and the Xcode command-line tools; packaging uses `7z`, which
is optional if copying the outputs directly. Repeat the final Xcode app link after
replacing a dependency with your modified build.

To rebuild OpenSSL, extract `openssl-3.3.2.tar.gz` or leave it beside the scripts in
`Dependencies/OpenSSL-wrapper` (the build script consumes that source archive).
The included Makefile and scripts build static libraries and an XCFramework; the
optional project regeneration uses Tuist. Use your own signing identity, or local
ad-hoc signing, rather than the upstream maintainer's default. In Xcode replace the
remote binary package with the rebuilt local XCFramework, preserving the OpenSSL
product/module name, then relink. Full crypto source and packaging patches are
included so the binary package is not the only available form.

The editable integration overlay is supplied for completeness. To regenerate the
project from upstream, create the public upstream checkouts at the pins above under
Integration/Vendor, initialize their submodules, then run
`python3 Scripts/prepare.py ios` from Integration. The generated Client project is
already complete, so regeneration is optional.

These files provide the source and build materials for modifying and relinking the
client under its licenses. They do not supply proprietary Apple SDKs, signing keys,
or a guarantee of App Store approval. Third-party names and trademarks are not an
endorsement. DeskPort supplies no third-party media catalog or paid-content access;
users connect to computers and content they are authorized to use.
