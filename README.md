<p align="center">
  <img src="app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

<p align="center">
  <b>English</b> ·
  <a href="docs/readme/README.zh-CN.md">简体中文</a> ·
  <a href="docs/readme/README.zh-TW.md">繁體中文</a> ·
  <a href="docs/readme/README.ja.md">日本語</a> ·
  <a href="docs/readme/README.ko.md">한국어</a> ·
  <a href="docs/readme/README.de.md">Deutsch</a> ·
  <a href="docs/readme/README.es.md">Español</a> ·
  <a href="docs/readme/README.fr.md">Français</a><br>
  <a href="docs/readme/README.pt-BR.md">Português (Brasil)</a> ·
  <a href="docs/readme/README.ru.md">Русский</a> ·
  <a href="docs/readme/README.it.md">Italiano</a> ·
  <a href="docs/readme/README.nl.md">Nederlands</a> ·
  <a href="docs/readme/README.pl.md">Polski</a> ·
  <a href="docs/readme/README.tr.md">Türkçe</a> ·
  <a href="docs/readme/README.uk.md">Українська</a><br>
  <a href="docs/readme/README.ar.md">العربية</a> ·
  <a href="docs/readme/README.hi.md">हिन्दी</a> ·
  <a href="docs/readme/README.id.md">Bahasa Indonesia</a> ·
  <a href="docs/readme/README.ms.md">Bahasa Melayu</a> ·
  <a href="docs/readme/README.th.md">ไทย</a> ·
  <a href="docs/readme/README.vi.md">Tiếng Việt</a> ·
  <a href="docs/readme/README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  A remote desktop workspace built on Moonlight and Sunshine.<br>
  Keep your remote desktop ready in the background, bring it onto your current
  workspace with one action, and tuck it away without reconnecting.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.6"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.6-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Main features

**0.7.6 release:** [release notes](docs/RELEASE_0.7.6.md) and
[versioned downloads](https://github.com/keithxc/deskport/releases/tag/v0.7.6).
This stable release is marked Latest on GitHub.
The current downloads include native Windows/macOS shortcut capture and the
Linux-compatible text-input fix. They replace the earlier packages under the
0.7.6 filenames containing only the version and platform; verify the current
checksums before reinstalling.

- **One desktop app for viewing and sharing.** Bundled Sunshine, device approval,
  independent credentials and settings; no separate Sunshine installation for the
  macOS, Windows and Linux host packages (not the viewer-only Flatpak).
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

DeskPort **0.7.6** running on macOS, captured on 2026-10-27 with the
Simplified Chinese interface. Click a panel for the full-size image.

[![DeskPort 0.7.6 device list on macOS](docs/media/devices-macos-076.jpg)](docs/media/devices-macos-076.jpg)

The macOS client connected to a Windows 11 Pro test machine, showing
Windows System Information:

[![DeskPort macOS client displaying Windows 11 System Information](docs/media/windows-system-information-macos-076.jpg)](docs/media/windows-system-information-macos-076.jpg)

| Appearance and keyboard shortcuts | Built-in manual |
| --- | --- |
| [![DeskPort 0.7.6 settings on macOS](docs/media/settings-macos-076.jpg)](docs/media/settings-macos-076.jpg) | [![DeskPort 0.7.6 built-in manual on macOS](docs/media/manual-macos-076.jpg)](docs/media/manual-macos-076.jpg) |

## Supported platforms

See the [PC implementation and acceptance matrix](docs/PC_SUPPORT_MATRIX.md)
for current source versus installed-package scope, compositor capabilities,
historical evidence and remaining native checks. Release availability below
does not certify every feature on every GPU or OS session. The 0.7.6 release
records exact package identities and validation limits in its verification report.

| Platform | Role | Release status | Download |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Viewer + host + native virtual display | **0.7.6**; Developer ID signed and Apple notarized | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-macos-arm64.dmg) |
| Linux x86_64 | Viewer + host; adaptive workspace on capable KWin, Mutter and Hyprland hosts | **0.7.6**: DEB, RPM, Arch, AppImage, portable archive and Nix | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.6) · [installation guide](docs/LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Viewer only | **0.7.6**, Freedesktop Platform 25.08 | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.6) |
| iPhone / iPad, iOS/iPadOS 15+ | Client only | App Store; newer builds tested separately in TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Client only | Development; physical-device checks, no public store release yet | Not yet available |
| Windows x64 | Viewer + host + virtual display | **0.7.6**; full installer hosts, portable ZIP views; application and installer unsigned | [Installer](https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Not qualified | No verified release package | — |

> **Windows antivirus notice:** Antivirus software may flag DeskPort's Windows helper components as a false positive.
> We have not encountered this in our own testing. Keep Defender enabled; if a warning appears,
> see the release verification report and [sample history](docs/WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Native Linux packages require glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
current Arch. The AppImage bundles a private runtime for older glibc desktops;
hardware drivers and compositor support still determine hosting capabilities. KDE virtual-display hosting needs
compositor permission entries. Native/Nix packages supply fixed entries; AppImage
and portable builds register the current display-helper path automatically
when sharing starts. Host input still requires device permissions. Mobile apps do
not host desktops.
A source definition or successful compilation alone is not a supported platform.

## Install

**0.7.6:** Download the package for your platform from the
[0.7.6 release](https://github.com/keithxc/deskport/releases/tag/v0.7.6).
The device list, connection controls and remote desktop share one persistent
window. Configure its global recall shortcut in Settings. See the
[release notes](docs/RELEASE_0.7.6.md) for package checks and their limits.

**macOS:** Open the [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-macos-arm64.dmg),
drag DeskPort into Applications and open it. Grant Screen Recording and
Accessibility permissions to use host features. Nix and Homebrew are not required.

**Linux:** Choose the [0.7.6 package](https://github.com/keithxc/deskport/releases/tag/v0.7.6)
for your system and follow the [distribution-specific steps below](#linux-installation-by-distribution),
including the [host input requirements](docs/LINUX_PACKAGES.md#first-connection-and-hosting).
With Nix: `nix run --refresh github:keithxc/deskport/622946ed6cfc95dde08bebf99d7ee78327089e07`.
Flatpak provides viewing only. The release includes [SHA-256 checksums](https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt) and a [verification report](https://github.com/keithxc/deskport/releases/download/v0.7.6/VERIFICATION.txt).

**iPhone / iPad:** Install [DeskPort from the App Store](https://apps.apple.com/us/app/deskport/id6812389978).
The mobile app is a paid, one-time purchase; see your storefront for current local
pricing. Desktop releases remain free and open source. Purchases support signing,
test hardware and continued development.

Start Sharing on the host, select it on the client, and approve the device on the
host. Both devices need a working network path: use a LAN or your own VPN such as
Tailscale. DeskPort does not provide an account service, hosted desktops, a relay
or a network tunnel. Legacy Sunshine PIN pairing remains available.

**0.7.3 stable (historical verification):** This desktop update brings a shared 22-locale UI
catalog alongside additional desktop legacy translations, browser language
switching, Windows signed-in sharing without repeated
UAC, virtual-display system scaling, Linux runtime repairs, guarded Hyprland/X11
workspaces, Wayland clipboard fallbacks, optional local IME and square device cards.
See the [0.7.3 release notes](docs/RELEASE_0.7.3.md) for exact scope and remaining
checks. Exact-package checks cover macOS distribution trust, Windows payloads,
native CLI and protected antivirus scanning, and Linux runtime/package scenarios.
Earlier native and integration results retain their original source identities.

**0.7.2 stable (historical verification):** The [desktop release](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
combines resize, remote-streaming, data-saver and Linux runtime improvements.
Its 21 assets include packages, corresponding sources, relinking materials and
verification records. See the [0.7.2 release notes](docs/RELEASE_0.7.2.md).
The final AppImage passed a fresh Bazzite KDE Wayland VM check with normal FUSE
startup, matching viewer/helper/recovery runtimes and verified cleanup; physical
GPU and real streaming acceptance remain separate.

Windows Setup passed an upgrade from 0.7.1 with configuration and pairing
preservation checks. Disconnected local GUI checks covered shortcut launch,
Settings/About, close-to-tray, recall and Quit. Uninstall and planned real
streaming/input acceptance remain untested. An automation focus race caused an
unintended connection to an existing paired host; subsequent checks confirmed
zero active sessions, no reserved lease and the original display layout restored.
The incident is not streaming acceptance, and diagnostics cannot establish that
no remote input was transmitted. See the release notes for the exact scope.

**Earlier candidates:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
introduced faster resize reconnects, blurred transitions and smoother macOS drags;
[0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
added the Bazzite AppImage fixes. Their [rc.1 notes](docs/RELEASE_0.7.2-rc.1.md)
and [rc.2 notes](docs/RELEASE_0.7.2-rc.2.md) retain the earlier validation scope.

**Browser access:** Desktop hosts serve an HTTPS remote desktop with a
30-second sign-in code, optional browser pairing for seven days, adaptive sizing
and fullscreen. See the [0.7.1 release notes](docs/RELEASE_0.7.1.md) and
[browser guide](docs/BROWSER_ACCESS.md). The AppImage includes a private glibc
runtime; native packages retain their glibc 2.39 requirement.

### Linux installation by distribution

These instructions target **x86_64** desktop systems and the **0.7.6**
release downloads. Native packages require glibc 2.39 or newer. These are upstream downloads rather than
packages from a DeskPort distribution repository; install future releases by
downloading and verifying the new package again.

The 0.7.3 AppImage retains the portable-runtime, display-restoration,
process-teardown, permission and login-startup fixes for Bazzite/KDE Wayland
[issue #5](https://github.com/keithxc/deskport/issues/5), including the small Qt
Wayland 6.4.2 backport. That backport is AppImage-specific; native packages and
Flatpak retain their baseline Qt runtime. The final **0.7.3** AppImage passed normal
FUSE startup and helper/runtime checks in a fresh Bazzite KDE Wayland VM with
SELinux Enforcing and software graphics. This does not verify a physical GPU or
real streaming; see the [0.7.3 verification scope](docs/RELEASE_0.7.3.md#validation-boundary).

Full-VM installation, upgrade, rollback, removal and configuration-retention
results are tracked in the [desktop lifecycle matrix](docs/LINUX_DESKTOP_ACCEPTANCE.md).
An AppImage GUI smoke does not verify native package operations.
Package installation and GUI launch alone do not verify live capture, hardware
video decoding, remote input, visual quality or latency.

The retained Ubuntu 26.04 GNOME VM reproduces a startup crash in the published
0.7.2 DEB's older private VA-API Wayland library. The final 0.7.3 DEB reuses pinned
libva 2.23.0 in both private runtime trees and isolates system utilities from host
library overrides. It passed GUI/API and upgrade checks in a fresh overlay of
that VM. Synthetic configuration remained intact through rollback and uninstall;
the old-version crash was reproduced before upgrade and after rollback. Existing release assets keep
their original hashes. GNOME capture startup and repeated virtual-output creation
remain separate acceptance gaps. Debian 13's KWin 6.3 also remains below the host
display-helper's 6.6 minimum.

| Distribution / desktop | Download route |
| --- | --- |
| Ubuntu 26.04 GNOME | 0.7.6 DEB or AppImage; GNOME capture/recovery gaps remain |
| Debian 13 KDE | DEB, installed with APT |
| Fedora 44 KDE | RPM, installed with DNF |
| Arch Linux KDE | Local Arch package, installed with `pacman -U` |
| Bazzite KDE | 0.7.6 AppImage; historical 0.7.3 VM checks are scoped above |
| Omarchy / Hyprland | Local Arch package, installed with `pacman -U` |

Open a terminal in your normal desktop session. Each command sequence verifies
the exact downloaded filename against that release's `SHA256SUMS.txt` before
installing it; the checksum must report that filename as `OK`. An absent checksum
entry or a mismatch stops the sequence.
Launch DeskPort as your ordinary user after installation.
Before an upgrade, stop sharing and fully quit the old DeskPort instance using
its **Quit** action, such as the tray menu. Closing the window may only hide it.
Launch the newly installed version afterwards so it runs the new binary.

Upgrade from an earlier release or earlier 0.7.6 package using the downloads below. Existing
DeskPort settings and pairing identities are retained. Verify the exact filename
and checksum before installation.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — download, install, update and remove</summary>

Download [deskport_0.7.6_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.6/deskport_0.7.6_amd64.deb),
then open a terminal in the directory containing that file:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.6_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install --reinstall ./deskport_0.7.6_amd64.deb &&
deskport
```

APT installs dependencies from your configured Ubuntu repositories. The leading
`./` selects the downloaded file. For later launches, open DeskPort from the
application menu. For an upgrade, download the next compatible DEB, verify its
checksum and repeat `sudo apt install ./NEW_FILENAME.deb` with its actual filename.
Remove the installed package with `sudo apt remove deskport`.

Ubuntu 24.04 also meets the native package's glibc minimum. Ubuntu 22.04 needs the
AppImage, viewer-only Flatpak or a separately supported Nix/source build.
GNOME hosting depends on the installed Mutter/capture capabilities; see the
[adaptive-display requirements](docs/LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — download, install, update and remove</summary>

Download [deskport_0.7.6_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.6/deskport_0.7.6_amd64.deb),
then open a terminal in its download directory:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.6_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install --reinstall ./deskport_0.7.6_amd64.deb &&
deskport
```

APT resolves the package's dependencies. Open DeskPort from the application menu
on later launches. Upgrade by downloading and verifying the next compatible DEB,
then installing that file with `sudo apt install ./NEW_FILENAME.deb`. Remove with
`sudo apt remove deskport`. Debian 12 does not meet the native package's glibc
minimum; use the AppImage, viewer-only Flatpak or a supported Nix/source build.

For KDE hosting, check the [compositor and input requirements](docs/LINUX_PACKAGES.md#first-connection-and-hosting)
before starting sharing.

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — download, install, update and remove</summary>

Download [deskport-0.7.6.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.6/deskport-0.7.6.x86_64.rpm),
then open a terminal in its download directory:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.6.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
if [ "$(rpm -q --queryformat '%{VERSION}' deskport 2>/dev/null)" = 0.7.6 ]; then
  sudo dnf reinstall ./deskport-0.7.6.x86_64.rpm
else
  sudo dnf install ./deskport-0.7.6.x86_64.rpm
fi &&
deskport
```

DNF resolves dependencies from enabled Fedora repositories. Open DeskPort from
the application menu on later launches. Upgrade by downloading and verifying the
next compatible RPM, then installing that file with
`sudo dnf install ./NEW_FILENAME.rpm`. Remove with `sudo dnf remove deskport`.
This route targets regular Fedora; use the AppImage instructions for Bazzite.
For KDE hosting, also check the [compositor and input requirements](docs/LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — download, install, update and remove</summary>

Complete your usual full Arch system update, then download
[deskport-0.7.6-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.6/deskport-0.7.6-x86_64.pkg.tar.zst).
Open a terminal in its download directory:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.6-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.6-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` installs the local package and resolves dependencies from configured
repositories. Open DeskPort from the application menu on later launches. Install
future downloaded packages with `sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` after
verifying their checksums. Remove with `sudo pacman -R deskport`. This download
does not add DeskPort to the official repositories or AUR.
For KDE hosting, also check the [compositor and input requirements](docs/LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, startup and removal</summary>

Download
[DeskPort-0.7.6-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-x86_64.AppImage).
Stop sharing and use **Quit** to fully exit any older DeskPort instance; closing
its window may leave it running. Save the new AppImage in a permanent location
before enabling login startup:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.6-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.6-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.6-x86_64.AppImage &&
./DeskPort-0.7.6-x86_64.AppImage
```

Launch normally; no bundled libraries need to be removed or relocated. If login
startup referenced an older AppImage, open the new file at its final path and
refresh login startup. To use the AppImage on another listed x86_64
distribution, follow these steps instead of installing its native package.

Bazzite's [AppImage guide](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/)
describes Gear Lever for application-menu integration. If an integration tool
moves the file, refresh DeskPort startup from its final location afterwards.
Upgrades require downloading and verifying the new AppImage. To remove it,
disable DeskPort login startup, quit DeskPort, delete the file, and remove any
menu entry through the integration tool that created it. Run the AppImage on
the Bazzite host for desktop sharing; a Distrobox installation does not establish
host-desktop integration. The Flatpak below provides viewing only.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — local Arch package and hosting requirements</summary>

Complete Omarchy's normal system update, then download
[deskport-0.7.6-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.6/deskport-0.7.6-x86_64.pkg.tar.zst).
Open a terminal in its download directory:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.6-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.6-x86_64.pkg.tar.zst &&
deskport
```

Use the application launcher for later starts. Upgrade by downloading and verifying
the next local Arch package, then installing it with
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst`. Remove with `sudo pacman -R deskport`.
Omarchy's repository/AUR package menu does not make this GitHub download a
repository package.

Hyprland hosting uses a dedicated headless output. The 0.7.3 source supports
mirror, turn-off-others and extension policies when monitor identity and runtime
state can be restored safely; see [Hyprland display leases](docs/HYPRLAND_DISPLAY.md).
Follow the [Omarchy/Hyprland host guide](docs/CLI.md#omarchy-and-hyprland)
for the active compositor, capture permission and SSH-service requirements.
Check the installed Hyprland version before applying permission syntax.
Package installation alone does not configure unattended desktop login or verify
a working stream.

</details>

<details>
<summary><strong>Flatpak alternative</strong> — viewing only, on systems with Flatpak configured</summary>

Download [DeskPort-0.7.6-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.6/DeskPort-0.7.6-client-x86_64.flatpak),
then open a terminal in its download directory:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.6/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.6-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user --reinstall ./DeskPort-0.7.6-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

The local bundle may download its Freedesktop Platform 25.08 runtime. It provides
viewing only, without Sunshine hosting or desktop login-service integration.
Install future downloaded bundles with `flatpak install --user --or-update ./NEW_FILENAME.flatpak`
after verifying their checksums. Remove with
`flatpak uninstall --user io.github.keithxc.DeskPort`. This is a GitHub release
bundle; the installation does not require a DeskPort Flathub listing.

</details>

For hosting, use the normal logged-in graphical session and configure the
[capture and input prerequisites](docs/LINUX_PACKAGES.md#first-connection-and-hosting).
Test sharing with a client on your intended LAN or VPN before enabling optional
login startup. Package installation does not configure network reachability or
grant host input access. Stop sharing and quit DeskPort before package removal;
disable any login startup you enabled. Package removal preserves DeskPort settings
in your home directory.

### Linux repository availability

| Channel | Status | Installation |
| --- | --- | --- |
| GitHub release packages | 0.7.6 | Download the package for your distribution above |
| Upstream Nix flake | 0.7.6 | `nix run --refresh github:keithxc/deskport/622946ed6cfc95dde08bebf99d7ee78327089e07` |
| AUR `deskport-bin` | Recipe tested; publication blocked by new-account registration pause | [Build the recipe locally](docs/LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) submitted for review; not merged | Use the upstream flake until inclusion |
| Arch official repositories / Flathub | Not listed | Use the release downloads; Flatpak is viewer-only |

The downloadable Arch package is installed with `pacman -U`; it does not make
`pacman -S deskport` available. Likewise, the upstream flake does not make
`pkgs.deskport` available in Nixpkgs. Host input, capture permissions and firewall
setup are separate from installing the application; see the
[Linux installation guide](docs/LINUX_PACKAGES.md#nixos-and-nix).

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

See the [0.7.6 release notes](docs/RELEASE_0.7.6.md),
[architecture](docs/ARCHITECTURE.md) and [roadmap](docs/ROADMAP.md).

## Build and run on Linux

With Nix and flakes enabled:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
git submodule update --init shared/deskport-core
run="local-$(date -u +%Y%m%dT%H%M%SZ)"
bash scripts/build.sh build --target linux-nix --run "$run"
work=$(DESKPORT_RUN_ID="$run" DESKPORT_TARGET=linux-nix bash scripts/build-paths.sh work)
"$work/result/bin/deskport"
```

The viewer's third-party source dependencies are vendored; the shared core and
Nix build inputs remain pinned external dependencies. Native builds must initialize
`shared/deskport-core`. See [docs/VENDORED.md](docs/VENDORED.md).
For SSH setup, local host management, VPS prerequisites and Omarchy/Hyprland,
see the [CLI guide](docs/CLI.md). `deskport doctor` checks hosting prerequisites;
`deskport status --json` inspects the running instance without opening a window.
`nix run . -- --help` lists the command-line interface. Start Sharing on the host and bind the devices before connecting. Legacy
Sunshine PIN pairing is also available. No personal host or pairing
credential is included or imported from Moonlight.
New manual addresses default to DeskPort's port `48989`. Include the port shown
on the host's sharing page if different, or use `host:47989` to connect explicitly
to a default standalone Sunshine installation. Saved/discovered endpoints retain
their own ports.

For an editable native build with external compiler output:

```sh
git submodule update --init shared/deskport-core
nix develop
source_checkout="$PWD"
native_work="$(bash scripts/build-paths.sh checkout)/native-linux/work"
mkdir -p "$native_work"
cd "$native_work"
qmake "$source_checkout/moonlight-qt.pro" CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

The upstream project filenames remain unchanged to keep the fork reviewable.
See [managed build, signing and cleanup commands](docs/BUILD_WORKFLOW.md) for
macOS, Windows and Linux packaging. Generated work stays outside source; reusable
dependencies have their own cache, and completed GitHub delivery can be verified
and cleaned with one command. Signing examples contain placeholders only.

## What is different from Moonlight?

- Bundled Sunshine host with device approval, takeover and per-device settings.
- Sessions stay connected while the viewer is hidden; recall it from the tray/menu bar.
- HTTPS browser access served by desktop hosts.
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
platform. Desktop releases cover macOS Apple Silicon, Linux x86_64 and Windows x64
as both viewer and host; mobile apps are clients only.

Client platform support and host support are separate: a Mac host does not require
a DeskPort Mac client. Hardware decode, live input, image quality and recall latency need real
session testing; a successful build does not establish them.

## Next milestone

Keep one session connected across **50 hide/show cycles**, show the window on the
current workspace, and reliably return local input. Measure fresh-frame latency
and background resource use separately from window appearance.

See [the roadmap](docs/ROADMAP.md) for acceptance criteria and deferred features,
and [upstream notes](docs/UPSTREAM.md) for provenance and maintenance boundaries.

## Validation

```sh
run="check-$(date -u +%Y%m%dT%H%M%SZ)"
bash scripts/build.sh build --target linux-nix --run "$run"
```

The smoke check uses temporary XDG configuration/cache directories and an offscreen
Qt platform. It does not pair with a host, start a stream or inject input.

## Daily desktop controls

Closing a window keeps DeskPort running in the tray/menu bar. Use **Open DeskPort**
to recall it, **Disconnect viewer** to end only the current connection, or
**Quit DeskPort** to exit the service. Local sharing continues when a viewer closes.

**Meta + '** (apostrophe) performs the same window toggle as a left click on the
tray icon: hide the foreground viewer, recall the latest session, or toggle the
device list when no session is active. Meta means Command on macOS and the
Windows/Super key on Windows/Linux. The app must be running. Hiding releases
remote input and keeps the connection alive.

Wayland uses the desktop's GlobalShortcuts portal; its permission dialog may ask
you to approve the binding, and desktop shortcut settings can override it. X11,
Windows and macOS register with their native shortcut APIs. An occupied binding,
an unsupported keyboard layout or a missing portal leaves the tray controls
available and logs the registration failure. No system shortcut configuration is
rewritten. Set `DESKPORT_DISABLE_GLOBAL_SHORTCUT=1` to skip registration for an
isolated run.

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
is bundled in the macOS, Windows and Linux host packages (not the viewer-only Flatpak) and supplied by the Linux Nix package.
Separately installed Sunshine services are kept independent.

GPL-3.0-or-later; see [LICENSE](LICENSE), retained source notices and the
license of each vendored dependency listed in [docs/VENDORED.md](docs/VENDORED.md). Original documentation is preserved in
[README.upstream.md](docs/readme/README.upstream.md).

### Diagnostics and feedback

Diagnostic logs are enabled by default. Reproduce the issue,
then create a logs ZIP and open a GitHub issue draft. Review the ZIP before attaching
it: GitHub issues are public, and DeskPort does not upload or submit anything for
you. See [diagnostic data and limits](docs/DIAGNOSTICS.md).
