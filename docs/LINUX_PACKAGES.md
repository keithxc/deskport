# Linux release packages

DeskPort 0.6.3 provides x86_64 Linux downloads for users who do not build from
source. The AppImage and native packages contain the viewer, a separate Sunshine
host tree, Qt and media libraries. The Flatpak is a **client-only** package.

## Choose a download

| Format | Target | Install |
| --- | --- | --- |
| portable `.tar.gz` | x86_64, glibc 2.39+ | Extract and run `DeskPort.AppDir/AppRun` |
| `.deb` | Ubuntu 24.04+, Debian 13+ | `sudo apt install ./deskport_0.6.3-1_amd64.deb` |
| `.rpm` | Fedora 44 | `sudo dnf install ./deskport-0.6.3-1.x86_64.rpm` |
| `.pkg.tar.zst` | Current Arch Linux | `sudo pacman -U ./deskport-0.6.3-1-x86_64.pkg.tar.zst` |
| `.AppImage` | Modern glibc-based desktops, glibc 2.39+ | Make executable, then open |
| `.flatpak` | Distributions with Flatpak and Freedesktop Platform 25.08 | See below |
| Nix | NixOS / Linux with Nix | `nix run github:keithxc/deskport/v0.6.3` |

Native packages install a private runtime in `/opt/deskport`, an application-menu
entry and `/usr/bin/deskport`. Package managers install required system graphics,
font, audio and PipeWire libraries. These are upstream portable binary packages,
not entries in the distributions' official repositories or the AUR. RPM support
does not imply compatibility with RHEL, CentOS or openSUSE. ARM Linux builds are
not included in this release. Ubuntu 22.04 and Debian 12 need the Flatpak or a
separately supported source/Nix build rather than these glibc 2.39 binaries.

For AppImage:

```sh
chmod +x DeskPort-0.6.3-x86_64.AppImage
./DeskPort-0.6.3-x86_64.AppImage
```

Keep the AppImage in a permanent location before enabling login startup. Moving
or deleting it breaks that saved path; reopen it at its new location and refresh
login startup. On NixOS, prefer the native Nix package; `appimage-run` can run the
AppImage with an FHS environment. Desktop graphics drivers remain system-provided.

For Flatpak:

```sh
flatpak install --user ./DeskPort-0.6.3-client-x86_64.flatpak
flatpak run io.github.keithxc.DeskPort
```

The bundle references Flathub for its Freedesktop 25.08 runtime. Runtime installation
may require an additional download. The application itself is a GitHub release
bundle, not a Flathub listing. It permits network, Wayland/X11, audio and GPU access;
it does not grant host filesystem access or execute Sunshine outside the sandbox.
Host sharing and desktop login service integration are not supported by this
client-only package. Use a native package or AppImage when this Linux computer
must also act as a host.

## First connection and hosting

Open DeskPort and add the other device's reachable address. Approved DeskPort
peers use the connection entry, initially port 48991, and refresh streaming ports
automatically. LAN or a separately configured VPN must provide reachability;
installing DeskPort does not create an Internet tunnel.

For Linux hosting, KDE and GNOME require their supported virtual-display APIs;
see [adaptive display requirements](LINUX_ADAPTIVE_DISPLAY.md). Native installers
include KWin permission entries for the display helper and bundled host.
The source fix for [issue #2](https://github.com/keithxc/deskport/issues/2)
is included in 0.6.3 and adds automatic display-helper permission setup for
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
bash scripts/package-linux.sh
bash scripts/package-flatpak.sh build-linux.noindex/DeskPort.AppDir \
  build-linux.noindex/flatpak-output "$(cat app/version.txt)"
```

For an AppImage-only candidate, use
`DESKPORT_LINUX_FORMAT=appimage bash scripts/package-linux.sh`.
This skips native installer creation and writes checksums only for the candidate
AppImage. It still builds the viewer and bundled host and runs the package checks.

The rootless Podman build uses a pinned Ubuntu 24.04 image and checksum-pinned
packaging tools/Sunshine assets. The portable host is compiled from the exact
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

`scripts/test-linux-installers.py WORK_DIR VERSION` installs packages in clean
Ubuntu, Debian, Fedora and Arch containers and runs the same isolated smoke check.
`scripts/test-flatpak-singleinstance.py` checks session-bus ownership and crash
recovery without the sandbox; `scripts/test-flatpak-package.py VERSION` repeats
GUI launch, duplicate activation and crash recovery inside the installed Flatpak
with a private session bus and networking disabled. Set a private `FLATPAK_USER_DIR`
for test installation.
Use separate test configuration for AppImage/Flatpak and disable network during
GUI automation to avoid discovering personal hosts. Native GPU decoding, live
Wayland input, desktop permissions and long sessions require hardware acceptance;
headless installation/GUI checks do not establish those results.

For the KDE portable-permission regression, build the unwrapped helper with
`qmake /path/to/source/host/linux/linux.pro` in a separate build directory using the locked
devShell, then run `python3 scripts/test-linux-display.py /absolute/path/to/deskport-display --auto-permission`.
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
