<p align="center">
  <img src="app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

<p align="center">
  <b>English</b> ·
  <a href="docs/readme/README.zh-CN.md">简体中文</a> ·
  <a href="docs/readme/README.zh-TW.md">繁體中文</a> ·
  <a href="docs/readme/README.ja.md">日本語</a>
</p>

<p align="center">
  A remote desktop workspace built on Moonlight and Sunshine.<br>
  Keep your remote desktop ready in the background, bring it onto your current
  workspace with one action, and tuck it away without reconnecting.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.6.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.6.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Main features

- **One desktop app for viewing and sharing.** Bundled Sunshine, device approval,
  independent credentials and settings; no separate Sunshine installation for the
  macOS and native Linux packages.
- **A workspace that follows your client.** Adaptive sizing on compatible macOS
  and KDE hosts, HiDPI, portrait/landscape mobile layouts and per-device size tuning.
  Size changes briefly reconnect video; they are not seamless encoder changes.
- **Choose the host display policy.** Virtual primary with mirrored screens,
  virtual primary with other screens disabled, or an extended workspace on capable
  hosts. DeskPort restores the saved layout when the session ends.
- **Move between authorized devices.** Confirm before taking over an active
  session; the displaced client receives an authenticated takeover explanation.
- **Keep desktop work within reach.** Hide and recall a connected viewer, release
  remote input when hidden, and disconnect viewing independently of local sharing.
- **Save settings per computer.** Picture, audio, input, desktop tuning and address
  editing stay with the device; cards, pins, themes and localized interfaces.
- **Share desktop clipboard content on demand.** Text updates immediately;
  supported images and files are fetched when requested, between opted-in bound
  desktop peers. See [clipboard scope and limits](docs/CLIPBOARD.md).
- **Native mobile controls.** iPhone/iPad and the Android development client offer
  touch/trackpad input, keyboard controls and direct entry into the remote desktop.
- **Optional login startup and macOS recovery.** These require an active graphical
  login and system approval; they do not unlock FileVault or log in for you.

## Interface preview

Screenshots from the current interface with synthetic demo devices. Click a panel
for the full-size image; these previews do not imply store or release availability.

| Devices and groups | Per-device screen settings | Built-in manual |
| --- | --- | --- |
| [![Devices and groups](docs/media/devices.png)](docs/media/devices.png) | [![Per-device screen settings](docs/media/device-settings.png)](docs/media/device-settings.png) | [![Built-in manual](docs/media/manual.png)](docs/media/manual.png) |

## Supported platforms

| Platform | Role | Release status | Download |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Viewer + host + native virtual display | **0.6.3 stable**, Developer ID signed and Apple-notarized | [DMG](https://github.com/keithxc/deskport/releases/download/v0.6.3/DeskPort-0.6.3-macos-arm64.dmg) |
| Linux x86_64 | Viewer + host; adaptive virtual workspace on capable KDE hosts | **0.6.3 stable**: DEB, RPM, Arch, AppImage, portable archive and Nix | [Release](https://github.com/keithxc/deskport/releases/tag/v0.6.3) · [installation guide](docs/LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Viewer only | **0.6.3 stable**, Freedesktop Platform 25.08 | [Release](https://github.com/keithxc/deskport/releases/tag/v0.6.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Client only | App Store; newer builds tested separately in TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Client only | Development; physical-device checks, no public store release yet | Not yet available |
| Windows x64 | Viewer + host + virtual display | **0.6.3 stable**; installer and portable ZIP; see verification scope below | [Installer](https://github.com/keithxc/deskport/releases/download/v0.6.3/DeskPort-0.6.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Not qualified | No verified release package | — |

> **Windows antivirus notice (2026-09-26):** The exact 0.6.3 installer and
> extracted payload passed a protected Defender scan and native CLI checks.
> Installation and live streaming acceptance remain separate. Microsoft classified the
> previously reported 0.5.6 r4 helper as **Not malware** and stated that its
> detection was removed. That decision applies to the submitted old sample,
> not every newer build. Keep Defender enabled; do not add exclusions or restore
> quarantined files to bypass a detection. See [sample details and review status](docs/WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Native Linux packages require glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
current Arch, or a compatible AppImage system. KDE virtual-display hosting needs
compositor permission entries. Native/Nix packages supply fixed entries; 0.6.3
AppImage and portable builds register the current display-helper path automatically
when sharing starts. Host input still requires device permissions. Mobile apps do
not host desktops.
A source definition or successful compilation alone is not a supported platform.

## Install

**macOS:** Open the [notarized DMG](https://github.com/keithxc/deskport/releases/download/v0.6.3/DeskPort-0.6.3-macos-arm64.dmg),
drag DeskPort into Applications and open it. Grant Screen Recording and
Accessibility permissions to use host features. Nix and Homebrew are not required.

**Linux:** Choose the [0.6.3 package](https://github.com/keithxc/deskport/releases/tag/v0.6.3)
for your system and follow the [installation guide](docs/LINUX_PACKAGES.md),
including host input permissions. With Nix: `nix run github:keithxc/deskport/v0.6.3`.
Flatpak provides viewing only. Release assets include [SHA-256 checksums](https://github.com/keithxc/deskport/releases/download/v0.6.3/SHA256SUMS.txt) and a [verification report](https://github.com/keithxc/deskport/releases/download/v0.6.3/VERIFICATION.txt).

**iPhone / iPad:** Install [DeskPort from the App Store](https://apps.apple.com/us/app/deskport/id6812389978).
The mobile app is a paid, one-time purchase; see your storefront for current local
pricing. Desktop releases remain free and open source. Purchases support signing,
test hardware and continued development.

Start Sharing on the host, select it on the client, and approve the device on the
host. Both devices need a working network path: use a LAN or your own VPN such as
Tailscale. DeskPort does not provide an account service, hosted desktops, a relay
or a network tunnel. Legacy Sunshine PIN pairing remains available.

## Comparison with similar products

This is a workflow and platform comparison, checked against the linked official
documentation on **2026-09-20**, not a latency or image-quality benchmark. Product
features and platform limits may change.

| Aspect | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Main workflow | Persistent remote desktop workspace, adaptive sizing and device handoff | Game/desktop streaming through separate client and host apps | General remote control and support | Interactive remote desktop and collaboration |
| Desktop host platforms | macOS Apple Silicon, Linux x86_64 and Windows x64 | Windows, macOS, Linux and FreeBSD, subject to platform limits | Windows, macOS and Linux | Windows and macOS; Linux cannot host |
| Desktop clients | macOS Apple Silicon, Linux x86_64 and Windows x64 | Windows, macOS and Linux, among others | Windows, macOS and Linux | Windows, macOS, Linux and supported Chromium browsers |
| Mobile clients | iPhone/iPad released; Android in development | iOS/iPadOS and Android | iOS/iPadOS and Android; iOS cannot host | Android; no iOS/iPadOS client |
| Setup model | Bundled host in native desktop packages; approve devices in DeskPort | Install and configure Sunshine separately, then pair Moonlight | Public server infrastructure or a self-hosted server | Parsec account and application |
| Network/service model | Bring your own LAN/VPN reachability; no DeskPort relay | Self-hosted streaming; configure network reachability | Public infrastructure or self-hosted OSS/Pro server | Parsec account/service infrastructure |
| Source and distribution | GPL desktop, free desktop packages; paid Apple app distributed separately | Open-source clients and host | Open-source client and OSS server; commercial Pro server option | Proprietary application/service |

Sources: [Moonlight](https://moonlight-stream.org/),
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk clients](https://rustdesk.com/docs/en/client/),
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/),
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
DeskPort's current platform limits are listed above; this table does not imply
feature parity across every OS or a measured performance advantage.

See the [0.6.3 release notes](docs/RELEASE_0.6.3.md),
[architecture](docs/ARCHITECTURE.md) and [roadmap](docs/ROADMAP.md).

## Build and run on Linux

With Nix and flakes enabled:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

The viewer's third-party source dependencies are vendored; the shared core and
Nix build inputs remain pinned external dependencies. Native builds must initialize
`shared/deskport-core`. See [docs/VENDORED.md](docs/VENDORED.md).
`nix run . -- --help` prints the inherited command-line interface. Start Sharing on the host and bind the devices before connecting. Legacy
Sunshine PIN pairing is also available. No personal host or pairing
credential is included or imported from Moonlight.
New manual addresses default to DeskPort's port `48989`. Include the port shown
on the host's sharing page if different, or use `host:47989` to connect explicitly
to a default standalone Sunshine installation. Saved/discovered endpoints retain
their own ports.

For an editable native build:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

The upstream project filenames remain unchanged to keep the fork reviewable.

## What is different today?

- Separate `deskport` executable, `DeskPort` Qt settings namespace and
  `io.github.keithxc.DeskPort` Linux application ID.
- Windowed streaming and absolute-pointer control by default.
- Mute on focus loss; game optimization, gamepad mouse, multi-controller mode,
  background gamepad input and Discord presence disabled by default.
- No upstream Moonlight update prompts for this independent application.
- A locked Nix environment and a Linux build workflow.

The desktop interface provides device, sharing and settings pages, with language
selection and separate host permissions. Closing the viewer keeps its session available and opens the device list.
An active device offers Return to desktop; other devices show details until the
current session is disconnected. Pin frequently used devices and choose a compact
list or cards. Appearance follows the system, with light and dark overrides.

## Platform scope

See the [platform table](#supported-platforms) above for release state per
platform. KDE Wayland / AMD on Linux x86-64 is the first live-use target.

The first development workflow is Linux → macOS through Sunshine. Client platform
support and host support are separate: a Mac host does not require a DeskPort Mac
client. Hardware decode, live input, image quality and recall latency need real
session testing; a successful build does not establish them.

## Next milestone

Keep one session connected across **50 hide/show cycles**, show the window on the
current workspace, and reliably return local input. Measure fresh-frame latency
and background resource use separately from window appearance.

See [the roadmap](docs/ROADMAP.md) for acceptance criteria and deferred features,
and [upstream notes](docs/UPSTREAM.md) for provenance and maintenance boundaries.

## Validation

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

The smoke check uses temporary XDG configuration/cache directories and an offscreen
Qt platform. It does not pair with a host, start a stream or inject input.

## Daily desktop controls

Closing a window keeps DeskPort running in the tray/menu bar. Use **Open DeskPort**
to recall it, **Disconnect viewer** to end only the current connection, or
**Quit DeskPort** to exit the service. Local sharing continues when a viewer closes.

Plain-text clipboard sharing and system-shortcut capture are enabled by default on
both bound devices. Setting changes apply after reconnecting. Only new copies are
shared, up to 1 MiB. Supported images and files are fetched on demand between
compatible opted-in desktop peers; see [limits](docs/CLIPBOARD.md). In desktop pointer
mode, keyboard routing follows the pointer inside the focused video.
**Ctrl+Alt+Shift+Z** releases input;
**Ctrl+Alt+Shift+Q** disconnects the viewer. Click inside to regain input after
explicit release. OS-reserved shortcuts depend on the desktop compositor.

Login startup and recovery require an active graphical login session. See
[acceptance checks and limitations](docs/INPUT_SERVICE_ACCEPTANCE.md) before relying
on a computer for unattended access.

## License and attribution

DeskPort is an independent derivative of [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
initially based on v6.1.0. It is not an official Moonlight or Sunshine release.
Moonlight provides the streaming foundation; [Sunshine](https://github.com/LizardByte/Sunshine)
is bundled in the macOS and portable Linux host packages and supplied by the Linux Nix package.
Separately installed Sunshine services are kept independent.

GPL-3.0-or-later; see [LICENSE](LICENSE), retained source notices and the
license of each vendored dependency listed in [docs/VENDORED.md](docs/VENDORED.md). Original documentation is preserved in
[README.upstream.md](docs/readme/README.upstream.md).

### Diagnostics and feedback

Diagnostic logs are enabled by default. Reproduce the issue,
then create a logs ZIP and open a GitHub issue draft. Review the ZIP before attaching
it: GitHub issues are public, and DeskPort does not upload or submit anything for
you. See [diagnostic data and limits](docs/DIAGNOSTICS.md).
