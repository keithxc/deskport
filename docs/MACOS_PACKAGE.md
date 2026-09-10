# macOS all-in-one preview

The package contains one `DeskPort.app` with three components:

- The Moonlight-derived Qt viewer and its device list.
- An unmodified, signed Sunshine host inside `Contents/Helpers/Sunshine.app`.
- DeskPort's native virtual-display helper inside `Contents/Helpers/deskport-display`.

The installed app does not require Nix, Homebrew, BetterDisplay or a separately
installed Sunshine. The current build targets Apple Silicon and macOS 26 because
its Qt bottles target that OS. Older macOS versions and Intel Macs are not qualified.

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

Build and staging apps are stored in `.noindex` directories, reached through the
`build-macos` and `dist` convenience symlinks, to avoid duplicate application icons.

Initialize the pinned upstream submodules, including `libs`, before native builds.
Build dependencies are Xcode and Qt with qmake/qmlimportscanner (currently Homebrew Qt).
The build uses the project's Moonlight sources, not the official Moonlight binary.

```sh
DESKPORT_SIGN_IDENTITY='your local signing identity' bash scripts/package-macos.sh
```

`DESKPORT_QT_BIN` and `DEVELOPER_DIR` can override the tool locations. The script
pins the Sunshine DMG version/hash, preserves its publisher signature, deploys Qt,
checks linked-library paths and signs the outer app and helper. Do not ad-hoc
re-sign an installed host as an update strategy. Public distribution needs a
Developer ID signature, notarization and a corresponding-source release; the
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
