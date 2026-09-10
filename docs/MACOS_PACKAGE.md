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

On another client, add the Mac's reachable address with port `48989` (for example,
`your-computer:48989`). Keep one incoming pairing dialog open, then enter that client's four-digit PIN
in DeskPort's sharing page. Pairing stays saved. This preview pairs each direction separately; unified
mutual pairing is not implemented yet.

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
The preview uses ports based at 48989, avoiding the default Sunshine base 47989.
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
local development builds do not satisfy those public-release requirements. The
initial local preview uses an ad-hoc signature (`DESKPORT_SIGN_IDENTITY=-`) because
the development identity is locked to an interactive keychain session. It is not
the stable-signature deployment intended for a permanent unattended host.

`DeskPort.app/Contents/MacOS/DeskPort --host-self-test` starts the two host components
using temporary state, verifies the HTTP server identity, then stops them. It does
not pair, capture screenshots or inject input. Port 48989 must be free. Passing
this test does not establish remote input, unattended reboot or long-session quality.

`DeskPort --share` opens the sharing page and starts the default virtual display.
