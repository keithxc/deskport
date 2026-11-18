# macOS all-in-one package

For new builds, prefer the [managed build workflow](BUILD_WORKFLOW.md). It calls
these same recipes with isolated run directories, private signing configuration,
recorded artifacts and verified GitHub cleanup. Copy the placeholder-only
`config/build-signing.env.example` outside the repository; use your own signing
identity and keychain profile. Never add private keys or signing credentials to
Git, source archives or release uploads.

## Required activation workflow (2026-09-14)

Build and verify, publish a GitHub prerelease, then update and push the consuming
mynix release URL/hash and source pin. The user runs `rebuild switch` manually.
Do not replace the installed application, request administrator installation,
run switch or restart deployed services as part of publishing an update. The
manual installation/recovery examples below are historical or for a separately
authorized recovery; they do not override this workflow. Verify live behavior
after the user confirms activation.

## Developer ID distribution

The application icon uses the layered `app/AppIcon.icon` document compiled by
Xcode's `actool` into `Assets.car`, with `deskport.icns` as the static fallback.
qmake builds and bundles both resources; the bundle checker requires them and
the matching `CFBundleIconName`/`CFBundleIconFile` entries. Full Xcode 26+ is
required, selected per command through `DEVELOPER_DIR`. See
[Application icon](APP_ICON.md) for the shared vector source and regeneration.

Public macOS packages target Apple Silicon and macOS 26 or newer. Download the
current signed and notarized package from the [stable release page](https://github.com/keithxc/deskport/releases/latest).
Each release includes its SHA-256 checksums and verification report. The release
workflow verifies nested Developer ID signatures, Apple acceptance, app/DMG
stapled tickets and Gatekeeper after ZIP extraction. Package verification is
separate from fresh-machine permission and streaming acceptance.

`scripts/package-macos.sh` still produces the existing local-development build.
To prepare public distribution, pass that verified application to the separate
release workflow. It preserves its Bundle ID, copies it into a new output
directory, then signs every Mach-O file, nested code bundle and the outer app
with one explicitly selected Developer ID Application identity. This includes
the Moonlight-derived viewer, Qt/QML plugins, media libraries, patched Sunshine
and virtual-display helper. Upstream notices remain included; no upstream private
key or certificate request is needed. The Developer ID team can differ from the
old local development team, so test privacy-permission migration before installing
on an unattended Mac. Do not change the installed remote-access application as
part of preparing distribution artifacts.

Select the intended identity with `security find-identity -v -p codesigning`.
Use its exact SHA-1 to disambiguate duplicate display names. Keep the team and
Bundle IDs stable across updates; renew certificates when needed rather than
treating their fingerprints as permanent application identity. Store private
keys only in a protected keychain/backup, never in this repository.

```sh
export DESKPORT_SIGN_IDENTITY='DEVELOPER_ID_CERTIFICATE_SHA1'
export DESKPORT_SIGN_TEAM='APPLE_TEAM_ID'
source_app=$(DESKPORT_NIX_DEPS=1 bash scripts/build-paths.sh macos-dist)/DeskPort.app
candidate=$(bash scripts/build-paths.sh releases)/developer-id-candidate
nix develop -c bash scripts/release-macos.sh prepare \
  "$source_app" "$candidate"
```

The same-session signing preflight must pass before copying/signing the candidate.
`prepare` deliberately refuses an existing output directory. It creates a signed
application and `submission.zip`, not a completed public release. The script uses
secure timestamps and hardened runtime, and validates each embedded signature.
It does not rebuild or alter the original binary's functionality.

Configure the Apple notary credential once in an interactive terminal (a remote
SSH terminal with a TTY is sufficient if the keychain is accessible). This command
prompts for the Apple account and app-specific password; never send passwords to
chat or put them in shell arguments, files, source control or logs:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  xcrun notarytool store-credentials deskport-notary --team-id APPLE_TEAM_ID
```

Create an app-specific password in the Apple account's Sign-In and Security page
if needed. An existing authorized App Store Connect API key is another supported
credential method; Xcode/Transporter login is not itself a notarytool profile.

```sh
export DESKPORT_NOTARY_PROFILE=deskport-notary
nix develop -c bash scripts/release-macos.sh notarize \
  "$candidate"
```

The workflow saves submission IDs and polls them, allowing interrupted runs to
resume. Apple must accept the app archive before its ticket is stapled. The DMG
then contains the stapled app and an Applications shortcut; the DMG is separately
signed, notarized and stapled. Finally, the ZIP is created from the stapled app and
tested after Info-ZIP extraction, including ticket validation and Gatekeeper.
Publish only when `STATUS.txt` says `NOTARIZED`, with the generated `SHA256SUMS`
and matching source/version notices. Keep submission logs private. Do not modify
the candidate between prepare, submit and resume; create a new candidate if any
content changes. A fresh Mac/browser-download test remains the final user-facing
installation check. Signing never pre-authorizes Screen Recording or Accessibility.

References: [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[custom workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).

The package contains one `DeskPort.app` with three components:

- The Moonlight-derived Qt viewer and its device list.
- A patched, signed Sunshine host inside `Contents/Helpers/Sunshine.app`.
- DeskPort's native virtual-display helper inside `Contents/Helpers/deskport-display`.

The installed app does not require Nix, Homebrew, BetterDisplay or a separately
installed Sunshine. The current build targets Apple Silicon and macOS 26 because
the package currently sets that deployment target. Older macOS versions and Intel Macs are not qualified.

## Unattended operation

After installing in `/Applications/DeskPort.app`, open Sharing and turn on
**Unattended operation**. DeskPort enables its normal login item and registers
`io.github.keithxc.DeskPort.Recovery` using SMAppService. Approve DeskPort's
background service in System Settings when prompted. The UI distinguishes the
request from approval and reports whether the helper has checked in recently.

The signed helper and launchd plist remain inside the application bundle. Every
30 seconds, the system job identifies the console user, permanently drops root
privileges, and checks that user's opt-in and pause markers. It starts only that
user's missing DeskPort GUI agent. Existing processes, disabled login items and
other applications are left alone. It does not accept client commands, arbitrary
paths or configuration arguments. Settings and heartbeat are stored under
`~/Library/Application Support/DeskPort/unattended/`, without pairing secrets.

Choose **Pause and quit** to stop recovery until DeskPort next opens (including
normal login startup). Turn off **Unattended operation** for a persistent opt-out;
normal login startup remains a separate preference. Restart keeps recovery enabled.
Stopping sharing remains off until explicitly started again. This process recovery
does not diagnose a hung encoder, a disconnected network or missing capture access.

A logged-in Aqua session is required. This can recover the app while post-update
Setup Assistant delays automatic GUI launches, but it does not finish Setup
Assistant, unlock FileVault, enable automatic login or bypass macOS permissions.
See [Apple's SMAppService documentation](https://developer.apple.com/documentation/servicemanagement/smappservice)
and [background task management](https://support.apple.com/en-au/guide/deployment/depdca572563/web).

An older Nix-specific recovery job must be removed through its system configuration
before enabling the app-owned service. Do not run both recovery managers. The
0.3.1 consuming mynix update removes the earlier unpublished Nix-specific proposal.

## Install and use

Open the DMG and drag DeskPort into Applications. Open DeskPort, then select
**Share this computer**. Choose a HiDPI display size and start sharing. Use the
permission buttons to grant the host's screen-recording and accessibility access
in macOS. System authorization remains a one-time user step for a new code identity.

On the updated DeskPort iPhone/iPad client, select this computer's DeskPort
service (initially `48989`) and approve the incoming popup on the Mac. This grants
one-way client access, without a PIN or a host running on the mobile device.
Desktop-to-desktop DeskPort requests retain mutual binding. Legacy Moonlight and standalone Sunshine workflows still use their PIN pairing UI;
DeskPort exposes it under Sharing → Compatibility & diagnostics → Legacy PIN pairing.
Saved access persists across launches. Granting access restarts the embedded host.

Starting authentication and stopping sharing run asynchronously. If either host
component exits, DeskPort cleans up its own child processes and allows retry.
A running-host message does not establish capture or input permission readiness.

Closing the window while sharing hides it. Use DeskPort's tray menu to reopen it,
stop sharing, or quit. Enable **Start sharing when I log in to this Mac** after
installing in `/Applications/DeskPort.app` to register the user login launcher.
A logged-in desktop session is required. Login startup does not unlock FileVault
or configure macOS automatic login.

DeskPort manages its own virtual workspace for admitted sessions. Adaptive
resolution follows the viewer size, subject to the platform's supported modes
and HiDPI bounds. Session transitions restore the saved display topology and
release remote input; the helper removes its owned virtual display when its
lease ends. Existing windows on physical monitors are not automatically moved.
macOS may relocate windows when an owned virtual display is removed. Physical
monitor, mirror, resize and restoration behavior require native acceptance;
package checks alone do not establish them.

## Isolation and diagnostics

Host data lives under Qt's DeskPort application-data directory in a private `host`
subdirectory. The UI's **Host logs** button opens it. Host configuration, pairing
state, API credentials and certificates are independent of external Sunshine.
DeskPort initially uses base port 48989. Before creating a display it reserves the
whole TCP/UDP group; if any member is busy, it tries another DeskPort group at
100-port intervals (49089 through 50889). It remembers the last started group's
base port and tries it first next time. The UI, generated host configuration and
local pairing API all follow the selected group. Approved DeskPort peers on
0.1.14 or later automatically refresh changed ports through the fixed connection
entry, without re-pairing. Legacy manually entered Sunshine hosts still require
address updates; Bonjour discovery uses the advertised port.

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
resize are checked at runtime and can break on a future OS release. The desktop distribution
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
package_logs=$(bash scripts/build-paths.sh logs)
mkdir -p "$package_logs"
launchctl submit -l io.github.keithxc.deskport.package-once \
  -o "$package_logs/package-install.log" \
  -e "$package_logs/package-install.err" \
  -- /usr/bin/env "PATH=$PATH" "DESKPORT_SIGN_IDENTITY=$DESKPORT_SIGN_IDENTITY" \
  "DESKPORT_BUILD_ROOT=$(bash scripts/build-paths.sh root)" \
  /bin/bash -c 'cd "$1" && nix develop -c bash scripts/package-macos.sh; echo $? > "$2/package-install.rc"; exec sleep 86400' \
  deskport-package "$PWD" "$package_logs"
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

Build and staging apps are stored in `.noindex` directories under the shared
build root to avoid duplicate application icons. Packaging does not create
`build-macos` or `dist` symlinks in the source checkout.

The pinned upstream dependencies, `libs/mac` included, are vendored in this
repository; only `git submodule update --init shared/deskport-core` is needed
before native builds. See [VENDORED.md](VENDORED.md).
The nested Sunshine build also downloads one checksum-pinned Apple Silicon
FFmpeg archive described by `host/vendor/sunshine-macos-ffmpeg.json`. Do not
replace it with the upstream `latest` URL: that mutable archive can add static
library dependencies without a corresponding change to the pinned host source.
macOS dependency relocation preserves libraries with the same basename when
their contents differ and rewrites each consumer to its exact source ABI. Run
`python3 tests/test-fix-macos-dependencies.py` after changing relocation.
Links are not the only build-machine dependency. Apple's libiconv as built by
Nix loads its converter modules and `csmapper` tables from absolute
`/nix/store` paths at run time, so on any other Mac every conversion failed and
GLib produced empty error messages; libnice then stopped answering ICE checks and
browser video never started. Relocation rewrites that library's consumers to the
system `/usr/lib/libiconv.2.dylib` (same Apple ABI, system data paths) and the
bundle checker rejects any bundled library that still loads such data. Other
embedded store strings (source paths, locale and CA directories, OpenSSL module
directories) are fallbacks that DeskPort's pinned-certificate connections do not use.

For troubleshooting, start DeskPort with `DESKPORT_HOST_LOG=/absolute/path` to
append the host and display helpers' raw output to a private (0600) file, capped
at 32 MiB; the host then logs at debug level. It is off by default because raw
output contains addresses and paths that diagnostics deliberately drop. The
isolated `--host-session-test` instance accepts `DESKPORT_DISPLAY_REAL_TOPOLOGY=1`
to reproduce the installed primary/mirror display policy, journaling its layout
in the temporary state directory instead of extending to the right.

The bundle checker executes the packaged nested Sunshine with `--help` inside a
write- and network-denied sandbox; this dynamic-loader smoke is required in
addition to signing, notarization, stapling and Gatekeeper checks.
The Apple Silicon macOS devShell manages Qt, CMake, pkg-config, Python, Git, Make,
OpenSSL, Opus, miniupnpc, ICU, Boost and Node.js/npm with the project's locked nixpkgs revision.
Xcode supplies Apple's compiler and SDK; Keychain/Aqua supplies code signing.
The existing pinned upstream viewer media libraries and Sunshine FFmpeg prebuilts
remain in use. This is a Nix-managed development environment, not a sandboxed
macOS Nix derivation or a complete source rebuild of those media dependencies.

```sh
DESKPORT_SIGN_IDENTITY='your local signing identity' \
  nix develop -c bash scripts/package-macos.sh
nix develop -c python3 tests/test-host-lifecycle.py --ui
```

Since 2026-10-03, generated files default to `$HOME/mygit/build/deskport` to keep
the workspace root and source checkout clean. `DESKPORT_BUILD_ROOT` overrides
that root. Each checkout gets a directory named from its basename and an
absolute-path checksum, so separate worktrees have independent caches. Nix and
Homebrew builds use `macos-arm64.noindex/nix/{build,dist}` and
`macos-arm64.noindex/homebrew/{build,dist}` within that checkout directory.
`DESKPORT_MACOS_BUILD_DIR`, `DESKPORT_MACOS_DIST_DIR` and
`DESKPORT_HOST_SOURCE_DIR` override individual paths; by default the prepared
host source is inside the selected build directory. Use absolute override paths
under the shared root. Use
`DESKPORT_DEVELOPER_DIR` to override the devShell's Xcode location.

Resolve paths without creating any output directories:

```sh
bash scripts/build-paths.sh checkout
DESKPORT_NIX_DEPS=1 bash scripts/build-paths.sh macos-build
DESKPORT_NIX_DEPS=1 bash scripts/build-paths.sh macos-dist
bash scripts/build-paths.sh releases
```

Put each signed candidate in a fresh directory under `releases.noindex`, named
with its immutable release/test ID. Put logs, exported sources, downloaded
assets and validation output under the same checkout build directory. Do not
move old CMake/qmake caches to reuse them: they contain absolute paths. Source
worktrees remain separate from generated build output. Native `nix build` uses
the Nix store as usual; choose `--no-link` or an explicit `--out-link` beneath
the checkout build directory.

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

For subsequent development, keep the working lock when a dependency update would
require large local Qt/Boost builds or a complicated migration. The project builds
itself locally; avoiding substantial third-party source builds is the constraint.
The Nix migration landed in DeskPort `d7c014f1`; mynix `eadc754` removed its
DeskPort-only Homebrew declarations. The Nix trial artifacts are local, separate
from the published `v0.1.10` assets. Neither a successful build nor a mynix commit
means the installed application has been switched or restarted.

The Homebrew fallback remains available outside `nix develop`, using
`DESKPORT_QT_BIN` and `DEVELOPER_DIR` overrides. It requires Qt, miniupnpc, Opus,
OpenSSL and ICU to be installed explicitly; mynix no longer retains these solely
for DeskPort. The script builds the patched Sunshine executable from pinned
sources and verifies the pinned publisher-signed DMG used for its resources.
It then relocates libraries and signs all nested code with the chosen identity.
Do not ad-hoc re-sign an installed host as an update strategy. Public distribution
needs a Developer ID signature, notarization and source-provenance metadata;
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

See [macOS lifecycle acceptance](MACOS_LIFECYCLE_ACCEPTANCE.md) for synthetic
physical-topology fixtures, the opt-in native extended-display harness and the
separate install/upgrade/rollback/privacy/recovery approval window.

Run `python3 tests/test-host-lifecycle.py` with native Qt available to exercise
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

### 0.1.11 packaging dependency correction

The locked devShell now includes Node.js/npm in both its packages and explicit
PATH. Sunshine checks for npm during CMake configuration even when building only
the host target. The previous shell depended on a cached NPM path; clean
configuration exposed the missing dependency. The cache dry-run required only
the shell derivation, without third-party source builds.
