# Linux release packages

For full desktop VM install/upgrade/rollback/removal coverage and its limits,
see [Linux desktop acceptance](LINUX_DESKTOP_ACCEPTANCE.md). Container package
checks and AppImage GUI smoke do not establish that lifecycle matrix.

DeskPort 0.7.8 provides x86_64 Linux downloads for users who do not build from
source. The AppImage and native packages contain the viewer, a separate Sunshine
host tree, Qt and media libraries. The Flatpak is a **client-only** package. Linux remote video uses libplacebo
and requires a working Vulkan driver; the device list remains available when
Vulkan cannot initialize.

## AppImage runtime

The AppImage bundles a private glibc loader and runtime for the viewer and
Sunshine host, using pinned sharun 0.8.1. It also includes matching PipeWire SPA
plugins and VA-API runtime support. Native packages and portable archives retain
their glibc 2.39 minimum. Hardware encoding, decoding and virtual displays still
depend on the host kernel, GPU drivers and compositor.

Verify downloads with `SHA256SUMS.txt` from the
[release page](https://github.com/keithxc/deskport/releases/tag/v0.7.8).
See [Linux verification](https://github.com/keithxc/deskport/releases/download/v0.7.8/VERIFICATION-linux.txt)
and [Linux provenance](https://github.com/keithxc/deskport/releases/download/v0.7.8/PROVENANCE-linux.json)
for the package identities, checks and limits.

## Upgrading

Fully quit the old DeskPort instance before installing the new package, then
launch the new installation. Closing the window may only hide it. Existing
settings and pairing identities are retained.

<a id="replacing-the-earlier-074"></a>
The historical 0.7.4 replacement used `r2` filenames while retaining version
0.7.4. Its immutable release assets and verification records remain separate
from 0.7.6.

Release filenames contain the application version and target platform. To
replace an installed package of the same version, request reinstall: for RPM
use `sudo dnf reinstall ./PACKAGE.rpm`; for a fresh install or an older version
use `sudo dnf install ./PACKAGE.rpm`. DEB and Flatpak examples below request
reinstall. The 0.7.8 desktop binaries use source
`ec4eb0861744dd04edea0ceda705c6ba9ece49b0`; Nix examples select the `v0.7.8`
release tag, which adds release documentation to that binary source.

Debian 13's KWin 6.3 lacks the 6.6+ virtual-display helper prerequisites.
The 0.7.7 Omarchy isolated canvas test reached 11 stages before the same
fullscreen-restore limit observed with the 0.7.6 control. These checks do not
establish physical input or live-streaming behavior.

## Choose a download

| Format | Target | Install |
| --- | --- | --- |
| portable `.tar.gz` | x86_64, glibc 2.39+ | Extract `DeskPort-0.7.8-linux-x86_64.tar.gz` and run `DeskPort.AppDir/AppRun` |
| `.deb` | Ubuntu 24.04+, Debian 13+ | `sudo apt install --reinstall ./deskport_0.7.8_amd64.deb` |
| `.rpm` | Fedora 44 | Existing 0.7.8: `sudo dnf reinstall ./deskport-0.7.8.x86_64.rpm`; fresh install or older version: use `dnf install` instead |
| `.pkg.tar.zst` | Current Arch Linux | `sudo pacman -U ./deskport-0.7.8-x86_64.pkg.tar.zst` |
| `.AppImage` | x86_64 glibc desktops, private runtime included | Make executable, then open |
| `.flatpak` | Distributions with Flatpak and Freedesktop Platform 25.08 | See below |
| Nix | NixOS / Linux with Nix | `nix run --refresh github:keithxc/deskport/v0.7.8` |

Native packages install a private runtime in `/opt/deskport`, an application-menu
entry and `/usr/bin/deskport`. Package managers install required system graphics,
font, audio and PipeWire libraries. These are upstream portable binary packages,
not entries in the distributions' official repositories or the AUR. RPM support
does not imply compatibility with RHEL, CentOS or openSUSE. ARM Linux builds are
not included in this release. Ubuntu 22.04 and Debian 12 need the AppImage, Flatpak or a separately
supported source/Nix build rather than the native glibc 2.39 binaries.

For AppImage:

```sh
chmod +x DeskPort-0.7.8-x86_64.AppImage
./DeskPort-0.7.8-x86_64.AppImage
```

Keep the AppImage in a permanent location before enabling login startup. Moving
or deleting it breaks that saved path; reopen it at its new location and refresh
login startup. On NixOS, prefer the native Nix package; `appimage-run` can run the
AppImage with an FHS environment. Desktop graphics drivers remain system-provided.

For Flatpak:

```sh
flatpak install --user --reinstall ./DeskPort-0.7.8-client-x86_64.flatpak
flatpak run io.github.keithxc.DeskPort
```

The bundle references Flathub for its Freedesktop 25.08 runtime. Runtime installation
may require an additional download. The application itself is a GitHub release
bundle, not a Flathub listing. It permits network, Wayland/X11, audio and GPU access;
it does not grant host filesystem access or execute Sunshine outside the sandbox.
Host sharing and desktop login service integration are not supported by this
client-only package. Use a native package or AppImage when this Linux computer
must also act as a host.

## Arch Linux and AUR

**Status (2026-09-27):** the `deskport-bin` recipe is prepared in
[`packaging/aur/deskport-bin`](../packaging/aur/deskport-bin), but has not been
published to AUR. The maintainer encountered AUR's temporary new-account
registration closure on 2026-09-27. Its notice states there is no manual
registration queue and asks users not to script retries. Follow
[Arch news](https://archlinux.org/news/) and
[aur-general](https://lists.archlinux.org/mailman3/lists/aur-general.lists.archlinux.org/)
for reopening updates. Publication awaits an AUR account with a registered SSH key;
the release package and local recipe remain usable.
Do not use `yay -S deskport-bin` until this page confirms publication.

For the existing release package:

```sh
sudo pacman -U ./deskport-0.7.8-x86_64.pkg.tar.zst
```

The retained AUR candidate recipe targets historical version 0.6.3, not the
current 0.7.8 release. Use the release package above for the current version.
To build that historical candidate locally, from a checkout containing the recipe:

```sh
cd packaging/aur/deskport-bin
makepkg -si
```

Read `PKGBUILD` first and run `makepkg` as a normal user. The recipe downloads the
upstream portable release, verifies SHA-256, and packages its private runtime in
`/opt/deskport`; it does not compile the application. `deskport-bin` provides and
conflicts with `deskport`, so remove/replace the old package through pacman rather
than forcing file overwrites. No device rules, group changes or running services
are modified by the recipe. Updates will follow stable upstream releases.

AUR is a community recipe repository, separate from Arch's official binary
repositories. Future AUR publication will not enable `pacman -S deskport`.

## NixOS and Nix

**Available now:** the upstream flake, pinned to the release tag:

```sh
nix run --refresh github:keithxc/deskport/v0.7.8
```

For a flake-based NixOS configuration, add the input and install its package in a
module that receives `inputs` through your existing `specialArgs` configuration:

```nix
# flake.nix inputs
inputs.deskport.url = "github:keithxc/deskport/v0.7.8";

# NixOS module
{ inputs, pkgs, ... }: {
  environment.systemPackages = [
    inputs.deskport.packages.${pkgs.stdenv.hostPlatform.system}.default
  ];
}
```

These are snippets for your existing configuration, not a complete flake. Keep
DeskPort's own locked nixpkgs input; overriding it with `follows` has not been
qualified for the released flake. Change the revision and update your lock file when
upgrading. Build and review your configuration before activating it yourself.

**Nixpkgs status (2026-09-27):** a standalone source-package candidate is prepared
under [`packaging/nixpkgs`](../packaging/nixpkgs). [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) is ready for upstream review following maintainer review. It is not merged;
`pkgs.deskport` and `nix run nixpkgs#deskport` are not installation instructions
for the current release. Upstream review and channel propagation are separate
steps. Initial candidate scope is Linux x86_64, viewer plus private session host.

For hosting, use a logged-in supported graphical session and configure access to
`/dev/uinput`. For example, in a NixOS module (replace `YOUR_USER`):

```nix
hardware.uinput.enable = true;
users.users.YOUR_USER.extraGroups = [ "uinput" ];
```

This grants input-injection capability to that user. Review and activate the
configuration yourself, then log in again for group changes. The package alone
does not grant this access. KDE capture entries are supplied by the package. Do not
enable a separate Sunshine service to launch DeskPort's private host.

For the default host port group, allow TCP **48984, 48989, 48991, 49010** and UDP
**48998, 48999, 49000, 49002** only on the trusted LAN/VPN interfaces you use.
If DeskPort selects another group because of port conflicts, use the ports shown
by the application instead. Keep the host administration port 48990 private.
LAN discovery additionally needs Avahi/mDNS and user-service publication;
manually entering a reachable address does not require discovery. Installing
DeskPort does not configure a VPN, router forwarding or public Internet access.

## First connection and hosting

Open DeskPort and add the other device's reachable address. Approved DeskPort
peers use the connection entry, initially port 48991, and refresh streaming ports
automatically. LAN or a separately configured VPN must provide reachability;
installing DeskPort does not create an Internet tunnel.

For Linux hosting, KDE and GNOME require their supported virtual-display APIs;
see [adaptive display requirements](LINUX_ADAPTIVE_DISPLAY.md). Native installers
include KWin permission entries for the display helper and bundled host.
The source fix for [issue #2](https://github.com/keithxc/deskport/issues/2)
is included in 0.7.1 and adds automatic display-helper permission setup for
portable builds. Older 0.6.0 AppImages require the workaround below. Remote keyboard/mouse input requires
permission to access `/dev/uinput`. The application shows when input setup is
needed. This release does not silently install privileged device rules, grant
capabilities, join the user to input groups or replace a standalone Sunshine
service. Device access must be configured by the system administrator using the
distribution's supported method. A logged-in desktop is required.

Close hides the application; its tray menu can reopen it or quit. Native packages
can register DeskPort's own user login service when enabled. Existing Nix-managed
startup entries retain ownership. The bundled host uses DeskPort's private
configuration and port selection, independently of standalone Sunshine.

## KDE permissions in portable builds

On KWin 6.6+, starting sharing registers the display helper's screencast permission
if it is missing, refreshes the KDE application cache, and opens a new Wayland
connection. This runs only when sharing is requested, not when the viewer opens.
It needs no root access, manual permission-file editing or session logout.
Installed native/Nix grants continue to work without writing a user entry.

For a fixed executable path the grant is reused on later sharing starts and
logins. AppImages use different temporary mount paths: DeskPort automatically
registers the current resolved executable each time it encounters a new path.
It does not need a stable extraction directory. Generated entries for vanished
executables are cleaned during the next permission setup; existing installations
and user-written entries are preserved. Entries are hidden from application menus
and request only `zkde_screencast_unstable_v1` for `deskport-display`, not the main
viewer. Sunshine maintains its own capture grant.

The files live in `${XDG_DATA_HOME:-$HOME/.local/share}/applications/` with names
`io.github.keithxc.DeskPort.kwin-display-*.desktop`. To revoke the generated grant,
stop sharing and remove these entries; explicitly starting sharing again recreates
the needed entry. KWin's global permission checks remain enabled.

If the data directory is not writable or desktop cache discovery fails, sharing
reports the failing helper and setup step. Check that directory, run
`kbuildsycoca6 --noincremental`, and restart sharing. Unsupported KWin versions
produce a separate protocol-requirement error. This does not grant `/dev/uinput`
access or change input-device permissions.

### Workaround for the released 0.6.0 AppImage

Extract the AppImage into a permanent directory using `--appimage-extract`, then
launch its `AppRun`. Create a hidden desktop entry under
`~/.local/share/applications/` (or `$XDG_DATA_HOME/applications/`):

```ini
[Desktop Entry]
Type=Application
Name=DeskPort display permission
NoDisplay=true
Exec="/absolute/resolved/path/DeskPort.AppDir/usr/libexec/deskport-display"
X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1
```

Replace the example with the executable's `realpath`, including `/var/home` on
systems where `/home` is a symlink. Run `kbuildsycoca6 --noincremental`, then restart
sharing. The reporter also logged out/in; this is not required by the new automatic
setup, which reconnects after refreshing the cache. Moving the extracted directory
requires updating this manual entry.

Implementation references:
- [KWin executable matching](https://github.com/KDE/kwin/blob/Plasma/6.6/src/utils/serviceutils.h)
- [KWin per-connection interface checks](https://github.com/KDE/kwin/blob/Plasma/6.6/src/wayland_server.cpp)
- [Sunshine runtime permission registration](https://github.com/LizardByte/Sunshine/blob/master/src/platform/linux/kwingrab.cpp)

The XDG ScreenCast portal also supports persistent restore tokens, subject to
compositor policy and revocation. That is a different capture route; adopting it
would require separately validating DeskPort's virtual-output resizing and
session layout contract, rather than just replacing this permission setup.
See the [portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html).

## Build and validate

Initialize the shared core with `git submodule update --init shared/deskport-core`,
then run:

```sh
bash scripts/build.sh package --target linux-portable --run example-linux-076 \
  --format all --flatpak
```

Choose a new run ID for each build. The [managed build workflow](BUILD_WORKFLOW.md)
keeps work, packages, logs and tests under
`$HOME/mygit/build/deskport/<checkout>-<path-checksum>/runs.noindex/<run>/linux-portable/`,
with dependency caches and small records retained separately.
Set `DESKPORT_BUILD_ROOT` to relocate the shared root. Native Nix builds use the Nix store; use
`--no-link` or an explicit `--out-link` below the checkout build directory to
avoid creating `result` links in the source tree.

For an AppImage-only candidate, use
`bash scripts/build.sh package --target linux-portable --run example-appimage`.
This skips native installer creation and writes checksums only for the candidate
AppImage. It still builds the viewer and bundled host and runs the package checks.

The rootless Podman build uses a pinned Ubuntu 24.04 image and checksum-pinned
packaging tools/Sunshine assets. Upstream publishes linuxdeploy, its Qt plugin,
appimagetool and the AppImage runtime under moving `continuous` tags; when those
files change, the build falls back to the verified copies attached to the
[`build-tools-2026.09`](https://github.com/keithxc/deskport/releases/tag/build-tools-2026.09)
release, and every tool is still checked against its locked SHA-256.
The portable host is compiled from the exact
Sunshine revision in `scripts/build-linux-host.sh`, with the same session-settings,
authenticated takeover, Linux display and reconnect-lifetime patches used by
the Nix package. The FFmpeg Vulkan backports rebuild four translation units
against checksum-pinned matching binaries/source/headers; ABI header comparison
rejects mismatched cached inputs. Its
pinned upstream submodules and build dependencies are fetched during the build. Ubuntu package versions are recorded in the build
output; apt repositories are not a historical snapshot. Qt 6 deployment includes
Wayland and offscreen plugins. `qmake -r` refreshes nested version headers. The
pipeline copies the read-only checkout into a writable build snapshot for
translation generation. It checks the internal version, CLI, packaged QML startup, host state
isolation and authenticated session API before producing packages. The API check
also rejects unpaired admission and browser-origin/unauthenticated requests. nFPM creates the DEB/RPM/pacman metadata;
the payload is shared, rather than rebuilding against each distribution's Qt.

`tests/test-linux-installers.py WORK_DIR VERSION` installs packages in clean
Ubuntu, Debian, Fedora and Arch containers and runs the same isolated smoke check.
`tests/test-flatpak-singleinstance.py` checks session-bus ownership and crash
recovery without the sandbox; `tests/test-flatpak-package.py VERSION` repeats
GUI launch, duplicate activation and crash recovery inside the installed Flatpak
with a private session bus and networking disabled. Set a private `FLATPAK_USER_DIR`
for test installation.
Use separate test configuration for AppImage/Flatpak and disable network during
GUI automation to avoid discovering personal hosts. Native GPU decoding, live
Wayland input, desktop permissions and long sessions require hardware acceptance;
headless installation/GUI checks do not establish those results.

For the KDE portable-permission regression, build the unwrapped helper with
`qmake /path/to/source/host/linux/linux.pro` in a separate build directory using the locked
devShell, then run `python3 tests/test-linux-display.py /absolute/path/to/deskport-display --auto-permission`.
This starts an isolated KWin/PipeWire/D-Bus session with permission checks enabled
and no initial helper grant. It checks first-use recovery, grant reuse, changing
mount paths, canonical paths, Unicode/space/quote handling, stale-entry cleanup,
setup-write errors and cache-refresh fallback, followed by the regular virtual
display lifecycle checks. The normal test without the flag covers preinstalled
permissions. On 2026-09-25 both modes passed with KWin 6.6.6; the unmodified helper
failed in the empty-permission fixture with the exact issue #2 error. This models
AppImage mount identities; it is not physical Bazzite or packaged AppImage
streaming acceptance.

Verify downloads with the release's SHA-256 file. Keep upstream notices and exact
source references; see [bundled components](BUNDLED_COMPONENTS.md). Deeper source
integration is explicitly deferred to a later release.
