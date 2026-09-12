# macOS all-in-one preview

The package contains one `DeskPort.app` with three components:

- The Moonlight-derived Qt viewer and its device list.
- A patched, signed Sunshine host inside `Contents/Helpers/Sunshine.app`.
- DeskPort's native virtual-display helper inside `Contents/Helpers/deskport-display`.

The installed app does not require Nix, Homebrew, BetterDisplay or a separately
installed Sunshine. The current build targets Apple Silicon and macOS 26 because
the package currently sets that deployment target. Older macOS versions and Intel Macs are not qualified.

## Install and use

Open the DMG and drag DeskPort into Applications. Open DeskPort, then select
**Share this computer**. Choose a HiDPI display size and start sharing. Use the
permission buttons to grant the host's screen-recording and accessibility access
in macOS. System authorization remains a one-time user step for a new code identity.

On another client, add the Mac's reachable address with the port shown on the
sharing page (initially `48989`, for example `your-computer:48989`). Keep one incoming pairing dialog open, then enter that client's four-digit PIN
in DeskPort's sharing page. Pairing stays saved. This preview pairs each direction separately; unified
mutual pairing is not implemented yet.

Starting authentication and stopping sharing run asynchronously. If either host
component exits, DeskPort cleans up its own child processes and allows retry.
A running-host message does not establish capture or input permission readiness.

Closing the window while sharing hides it. Use DeskPort's tray menu to reopen it,
stop sharing, or quit. Enable **Start sharing when I log in to this Mac** after
installing in `/Applications/DeskPort.app` to register the user login launcher.
A logged-in desktop session is required. Login startup does not unlock FileVault
or configure macOS automatic login.

The host creates its own virtual display. If macOS remembers that display as a
mirror sink, DeskPort captures the mirror source and preserves the mirror layout.
In that case, choose the same HiDPI pixel size as the source; a mismatched preset
is rejected rather than changing physical displays. Otherwise it is extended. Existing windows on physical monitors
are not automatically moved. Select a fixed size before sharing; live adaptation
to a remote viewer's window is a separate pending feature. Stopping sharing removes
this helper-owned display, so macOS may relocate its windows to another display.

## Isolation and diagnostics

Host data lives under Qt's DeskPort application-data directory in a private `host`
subdirectory. The UI's **Host logs** button opens it. Host configuration, pairing
state, API credentials and certificates are independent of external Sunshine.
DeskPort initially uses base port 48989. Before creating a display it reserves the
whole TCP/UDP group; if any member is busy, it tries another DeskPort group at
100-port intervals (49089 through 50889). It remembers the last started group's
base port and tries it first next time. The UI, generated host configuration and
local pairing API all follow the selected group. If it changes, update manually
entered client addresses; Bonjour discovery uses the advertised port.

The native Sunshine default group is never a candidate. A DeskPort instance lock
also prevents another DeskPort using the same state directory from starting a
second host or replacing its credentials. The bundled host's own tray icon is
disabled, leaving sharing controls in DeskPort and the native Sunshine tray alone.
UPnP is disabled. Reachability must be provided by the LAN or a network such as
Tailscale; installing the package does not create a network tunnel.

The local control API is authenticated with a generated secret and pins the host's
self-signed certificate. The application exposes only the pairing operation in its
UI. The upstream credentials command briefly receives the generated local secret
as an argument during initialization; it is not a user-supplied password.

The native virtual-display API is private CoreGraphics functionality. Creation and
resize are checked at runtime and can break on a future OS release. This preview
is not an App Store package.

## Build

### Local update troubleshooting (2026-09-11)

`errSecInternalComponent` was reproduced from the automation session even after
the login keychain was unlocked and a helper was signed successfully in the user's
terminal. Do not infer a per-bundle key ACL from this alone: execution-session
access remains the unresolved distinction. The package script now signs a temporary
probe before building, so a key-access failure stops early. Never change the
installed signing identity or use ad-hoc signing to get past this failure.

The previous successful update used an Xcode GUI build phase to sign the staged
application. Keep signing in an authorized GUI session when automation cannot
access the key. Supply a PATH containing `nix` to launchd, then enter the
project devShell for the build; a minimal launchd PATH cannot find Nix. Do not
store passwords in scripts, logs or shell arguments.

Before replacing `/Applications/DeskPort.app`, verify the staged bundle with
`codesign --verify --deep --strict` and `scripts/check-macos-bundle.py`. Move the
old installation into a timestamped `.noindex` backup, stop only DeskPort's own
processes, install and relaunch, then verify the running executable and bundled
host. Record the backup and source revision; version 0.1.2 alone does not identify
an uncommitted development update. Keep independently installed Sunshine running.

Verified recovery: launching the full packager with `launchctl submit` in the
logged-in Aqua session succeeded, including stable signatures for all three
executables, strict verification, dependency checks and DMG creation. From the
repository root with the desired identity exported:

```sh
launchctl submit -l io.github.keithxc.deskport.package-once \
  -o "$PWD/build-macos.noindex/package-install.log" \
  -e "$PWD/build-macos.noindex/package-install.err" \
  -- /usr/bin/env "PATH=$PATH" "DESKPORT_SIGN_IDENTITY=$DESKPORT_SIGN_IDENTITY" \
  /bin/bash -c "cd '$PWD' && nix develop -c bash scripts/package-macos.sh; echo \$? > '$PWD/build-macos.noindex/package-install.rc'; exec sleep 86400"
# launchctl submit jobs are KeepAlive: a bare packager call reruns forever,
# including after failure. Wait for package-install.rc, then remove the job.
launchctl list io.github.keithxc.deskport.package-once
launchctl remove io.github.keithxc.deskport.package-once
```

Do not run duplicate packaging jobs. A launchctl bootout may leave a manually
started DeskPort alive; SIGTERM also did not terminate the old viewer in this
update. Check exact PIDs and executable paths before and after stopping. If the
old viewer persists, terminate that verified PID before starting the replacement,
then stop its verified helper PIDs. Otherwise the single-instance check can send
the new launch to the old binary and silently leave the update inactive.

Do not touch `/Applications/Sunshine.app` or its `org.nixos.sunshine` agent. It
uses different ports from DeskPort's host and is not the cause of an offline
peer. If its agent is removed, restore the nix-darwin copy from
`/run/current-system/user/Library/LaunchAgents/` and bootstrap it. An ad-hoc
installed DeskPort also loses the stable identity's privacy grants.

Build and staging apps are stored in `.noindex` directories, reached through the
`build-macos` and `dist` convenience symlinks, to avoid duplicate application icons.

Initialize the pinned upstream submodules, including `libs`, before native builds.
The Apple Silicon macOS devShell manages Qt, CMake, pkg-config, Python, Git, Make,
OpenSSL, Opus, miniupnpc, ICU and Boost with the project's locked nixpkgs revision.
Xcode supplies Apple's compiler and SDK; Keychain/Aqua supplies code signing.
The existing pinned upstream viewer media libraries and Sunshine FFmpeg prebuilts
remain in use. This is a Nix-managed development environment, not a sandboxed
macOS Nix derivation or a complete source rebuild of those media dependencies.

```sh
DESKPORT_SIGN_IDENTITY='your local signing identity' \
  nix develop -c bash scripts/package-macos.sh
nix develop -c python3 scripts/test-host-lifecycle.py --ui
```

Nix builds use `build-macos.noindex/nix` and `dist.noindex/nix`, separate from
Homebrew builds and published artifacts. `DESKPORT_MACOS_BUILD_DIR` and
`DESKPORT_MACOS_DIST_DIR` override these locations. Use
`DESKPORT_DEVELOPER_DIR` to override the devShell's Xcode location.
The shell selects split Nix Qt tool, QML and plugin paths explicitly. Packaging
makes copied store files writable before relocation and signing, and verifies
that no linked library requires `/nix/store`, Homebrew or a user directory.

On 2026-09-12 all third-party Nix dependencies used binary substitutes: about
33 MiB of additional downloads on the existing machine, including Boost headers.
Viewer/host compilation, 14 isolated UI checks, stable signing, ZIP extraction
verification and a packaged QML/TLS probe passed. The probe used a cleared
environment and checked loaded libraries for Nix/Homebrew paths. Native streaming
acceptance remains separate. Cache availability can change: inspect a future
`nix build .#devShells.aarch64-darwin.default --dry-run` before accepting
substantial dependency source builds.

The Homebrew fallback remains available outside `nix develop`, using
`DESKPORT_QT_BIN` and `DEVELOPER_DIR` overrides. It requires Qt, miniupnpc, Opus,
OpenSSL and ICU to be installed explicitly; mynix no longer retains these solely
for DeskPort. The script builds the patched Sunshine executable from pinned
sources and verifies the pinned publisher-signed DMG used for its resources.
It then relocates libraries and signs all nested code with the chosen identity.
Do not ad-hoc re-sign an installed host as an update strategy. Public distribution
needs a Developer ID signature, notarization and a corresponding-source release;
local development builds do not satisfy those public-release requirements.
Use the same Apple Development signing identity and bundle identifier across local
updates. Ad-hoc signing requires explicit `DESKPORT_ALLOW_ADHOC=1` and is only for
disposable builds; its changing code hash can leave a stale enabled privacy switch
while TCC rejects access. After replacing an ad-hoc installation with a stable
signature, remove and re-add DeskPort in Screen Recording once, then relaunch it.
Do not reset or remove an independently installed Sunshine's permissions.
The outer app declares audio input access because macOS attributes requests from
the bundled host to DeskPort. Allow keychain access interactively if signing asks;
do not fall back to ad-hoc signing for an installed update.

`DeskPort.app/Contents/MacOS/DeskPort --host-self-test` starts the two host components
using temporary state, verifies the HTTP server identity, then stops them. It does
not pair, capture screenshots or inject input. It selects an available DeskPort
port group and queries that group. Passing
this test does not establish remote input, unattended reboot or long-session quality.

`DeskPort --share` opens the sharing page and starts the default virtual display.

`DeskPort --no-host-autostart` opens the viewer UI without starting sharing for
that launch. It preserves the saved login-start preference and is useful when
checking an updated application while another remote-access service is in use.

## Safe development validation

Run `python3 scripts/test-host-lifecycle.py` with native Qt available to exercise
startup cancellation, failure cleanup, retries and forced termination. This harness
uses fake helper/host executables, temporary state and loopback-only TCP/UDP
reservations; it does not create a virtual display, pair a device or start Sunshine.

Do not replace an application currently providing remote access during testing.
Validate development packages separately, and request local permission/stream
checks before creating displays or starting the bundled host on an active desktop.

## Port coexistence

| Purpose | Protocol | Offset from selected base | Initial port |
| --- | --- | --- | --- |
| GameStream HTTPS | TCP | -5 | 48984 |
| GameStream HTTP / discovery endpoint | TCP | 0 | 48989 |
| Local administration / PIN API | TCP | +1 | 48990 |
| RTSP | TCP | +21 | 49010 |
| Video | UDP | +9 | 48998 |
| Control | UDP | +10 | 48999 |
| Audio | UDP | +11 | 49000 |
| Reserved microphone slot | UDP | +13 | 49002 |

The current generated host configuration explicitly uses IPv4. Reservations cover
IPv4 wildcard listeners in normal use, including conflicts with loopback listeners.
They are released immediately before launching Sunshine because it cannot inherit
them. Another process can still race that handoff; startup failure cleans up only
DeskPort's children. No existing process is killed, no foreign configuration is
changed, and no router/firewall rule is installed to resolve a conflict. If all
20 groups are occupied, startup stops before creating the virtual display.

New manually entered addresses default to DeskPort's 48989. Explicit ports and
saved endpoints are retained; enter `host:47989` to deliberately select a default
native Sunshine host. Address-based CLI lookup also distinguishes ports on the
same machine. Bonjour keeps the shared GameStream service type and resolves each
advertised port; it does not replace or restart the system mDNS service.

Offsets and discovery behavior were checked against the bundled upstream version:
[stream ports](https://github.com/LizardByte/Sunshine/blob/v2026.906.222525/src/stream.h),
[RTSP port](https://github.com/LizardByte/Sunshine/blob/v2026.906.222525/src/rtsp.h),
[macOS Bonjour registration](https://github.com/LizardByte/Sunshine/blob/v2026.906.222525/src/platform/macos/publish.cpp).
