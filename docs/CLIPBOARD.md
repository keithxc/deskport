# Shared clipboard

Current desktop scope reviewed 2026-10-17: macOS pasteboard, Linux
data-control/Qt and Windows OLE providers support text, images and files.
Windows uses a native delayed OLE provider and pipe transport; it does not use
the X11/Wayland Qt provider. The original protocol checkpoint below predates
Windows integration. Current three-platform native acceptance remains scoped in
the [PC matrix](PC_SUPPORT_MATRIX.md); browser explicit text/paste is separate.
## Native macOS marker regression (2026-10-17)

AppKit provider types require valid UTIs. The macOS adapter maps the agent's
internal MIME marker to a private reverse-DNS UTI and back; peers keep the same
protocol. A new production-agent round trip on two named pasteboards covers both
directions with real PNG pixels, chunked file/directory content, repeated reads,
echo-marker presence and stop-time revocation. The prior provider-only tests did
not expose AppKit's rejection of the raw MIME marker. See
[PC core regression](PC_CORE_REGRESSION.md) for fixtures, evidence and open physical gates.

## 0.3.2 prerelease (2026-09-15)

The active, authenticated desktop session supports bidirectional UTF-8 text,
clipboard images and file/folder copies. Existing per-device sharing preferences
remain in use. Both endpoints need 0.3.2 for images/files; older peers retain the
previous text-only protocol. No initial clipboard contents are pushed on connect.

- **Text:** send immediately after a detected change (250 ms observation cadence),
  retaining the negotiated legacy limit of 128 MiB UTF-8 for text-only peers.
  Native v2 peers support 128 MiB UTF-8 text.
- **Images:** advertise a PNG offer without reading/encoding image bytes at copy
  time. Read the source and transfer in 256 KiB chunks only when the receiver
  requests the image. macOS TIFF offers are converted to PNG on demand. The
  encoded image limit is 128 MiB; TIFF conversion is limited to 32 megapixels.
- **Files/folders:** snapshot local paths and metadata, advertise only the item
  count, then request the manifest and file chunks when the destination reads
  file URLs. Download into a private temporary directory before publishing usable
  local URLs. macOS uses one lazy NSPasteboardItem per top-level file/folder;
  Wayland supplies a text/uri-list containing all top-level URLs. No FUSE,
  macFUSE, cloud service or additional privileges are required.

File copies are **copies**, not remote moves. Symlinks and special files are not
transferred. Directories, hidden files, multiple top-level files, empty files and
empty directories are supported within a 4096-entry / 16 GiB per-copy limit.
The session also caps accumulated completed temporary file downloads at 16 GiB
and checks available disk space before downloading. Paths with unsupported
components or case/Unicode-normalization collisions are rejected as a whole.
Permissions/extended attributes/resource forks are not preserved. A macOS
package is transferred as a directory tree of regular files.

### Demand means data access, not a guaranteed user gesture

Clipboard history tools (including Klipper), previews and applications inspecting
file URLs can trigger downloads before the user chooses Paste. Finder/Dolphin
may request data to update their UI. DeskPort cannot distinguish these reads
from a genuine paste. Disabling image/file history may avoid some early reads;
DeskPort does not change the user's history preferences.

Files are fully downloaded before their local URLs are returned, so the target
application may wait on the first paste. Very large files or slow connections
can exceed an application's native clipboard timeout. This is not streaming
remote-file access and does not claim RDP/FUSE-style arbitrary-size pasting.
A request times out after 30 seconds without a response. Copying new local
content, a newer remote offer, or disconnecting cancels an active download;
partial results are never published. A completed download is reused for repeat
paste during the session. Normal helper shutdown removes temporary downloads;
forced process termination can leave private OS temporary directories behind.
No text/image clipboard history is persisted.

### Architecture and ordering

A dedicated `--clipboard-helper` process owns the platform clipboard and its Qt
event loop. The parent relays bounded JSON frames over the existing pinned,
mutually authenticated TLS connection. Only the active approved exclusive session
may start a helper. EOF closes it; host stop, access removal and disconnect revoke
its channel. The SDL/media thread never waits for file or image I/O.

V2 exchanges `offer`, `read`, `data` and heartbeat messages. Text travels in the
offer; binary payloads use bounded Base64 chunks. The host assigns revisions;
stale concurrent offers lose to that revision. Clients coalesce new copies made
before acknowledgement and resend the latest against the acknowledged revision.
Clipboard markers suppress echoes. File reads refer only to an advertised index,
never a peer-supplied local path; open/fstat checks reject replaced files.

Wayland probes `ext-data-control-v1`, then `wlr-data-control-unstable-v1`
(manager versions 1/2), on a separate connection so the unfocused helper can
observe and own the selection. If neither exists, the helper uses the XDG
Clipboard portal with a new RemoteDesktop session. It selects **zero input
devices**, requests no screen streams or persistent permission, and waits for
the system dialog and `clipboard_enabled` result before publishing data. A
portal implementation that cannot authorize clipboard-only access is unsupported;
it does not trigger an input/screen permission fallback. Older GNOME installations
without Clipboard portal support require a supported desktop/portal version.
GNOME 50's dialog disables Share when only its clipboard switch is enabled.
If necessary, enable **Allow Remote Interaction** in that dialog as well: the
request still selects zero devices, and the helper accepts only a returned
`devices=0` with `clipboard_enabled=true`. The pending helper notice explains
this upstream UI restriction. It never retries with a nonzero device mask.
The isolated Ubuntu 26.04 / GNOME 50.1 consent preflight confirmed this exact
result; the first clipboard-only UI attempt timed out and was retained as a
failed run. Consent preflight is separate from the C++ provider/data checks.

DeskPort never installs a shell extension, invokes Mutter's private D-Bus API,
fakes a keyboard gesture, or silently falls back to unfocused Qt/XWayland reads.
Missing protocols/portal, denial, request timeout, session closure, manager/seat
removal and connection loss produce helper status/failure, not successful sharing.

The initial selection establishes a baseline without fetching payloads. An
unfocused viewer keeps its approved clipboard channel; no clipboard code acquires
input or window focus. Explicit viewer hide or switching to the control center
retains the desktop session but closes the clipboard channel under the existing
Session policy. Recall creates a new helper/baseline without replaying old
contents; the portal may require consent again. This change does not retain a
permission token or alter that privacy policy. Full disconnect also cancels
pending reads/writes and closes only this helper's selection/session. Releasing an obsolete offer preserves
a newer local owner. Selection changes invalidate in-flight pipe reads, queued
provider callbacks and writes; D-Bus replies arriving after cancellation cannot
access a completed read. File/image failures close the pipe without usable file
URLs; portal writes additionally report `SelectionWriteDone(false)`. Pipe I/O and
permission requests are bounded and keep the helper event loop available.

The Linux MIME adapter also accepts/advertises
`x-special/gnome-copied-files` for Nautilus. Conversion to/from `text/uri-list`
remains lazy, and remote file copies always serialize `copy`, including when a
local source offered `cut`. Unsupported-only MIME offers cannot become successful
text or file transfers. File-transfer portal tokens without a URI/Nautilus MIME are not interpreted as
local paths; unsupported-only offers produce an explicit notice. Native adapter
changes leave the v2 wire and shared-core pins unchanged. X11/offscreen use Qt's MIME provider; macOS uses AppKit item providers.

## Validation and manual acceptance

Automated checks cover protocol ordering, 100 bidirectional Unicode text copies,
no image payload on copy, chunk boundaries, repeat-read caching, directories,
multiple and empty files, source changes, invalid paths/manifests and disconnect.
Native named macOS pasteboards and an isolated virtual KWin compositor verify
format-only queries versus actual reads from a separate process. These checks
never use the user's general clipboard or live remote hosts.

After manually activating both endpoints:

1. Copy text in both directions, including while DeskPort is inactive.
2. Copy a screenshot on each side; paste into a browser/editor that accepts
   images. Repeat paste and compare the actual contents.
3. Copy multiple files plus a small directory in Finder, paste in Dolphin;
   repeat Dolphin to Finder. Verify bytes, names and empty directories.
4. Repeat with Klipper history enabled/disabled and observe whether history
   causes early download. No claim of “only after Ctrl/Cmd+V” is made.
5. Copy new content during a large download; disconnect/reconnect; verify that
   no partial file URLs or old clipboard contents are replayed.
6. Confirm streaming/input remains responsive during transfers. First-paste
   latency and large-file Finder/Dolphin compatibility remain user acceptance.

References: [Microsoft RDP clipboard protocol](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpeclip/),
[RustDesk clipboard design](https://github.com/rustdesk/rustdesk/blob/master/libs/clipboard/README.md),
[AppKit lazy item providers](https://developer.apple.com/documentation/appkit/nspasteboarditemdataprovider),
[Wayland protocol source](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/ext-data-control/ext-data-control-v1.xml).
The vendored XML and generated C/header retain the upstream permissive license;
regenerate using wayland-scanner client-header/private-code.

## Wayland fallback checks (2026-10-17)

Run under the locked Linux devShell, with `TMPDIR` and Python cache below the
checkout build root and the shared heavy-build lock held:

```sh
nix develop -c python3 tests/test-wayland-clipboard.py
nix develop -c python3 tests/test-clipboard-demand.py
nix develop -c python3 tests/test-wayland-clipboard.py --native-build "$FIXTURE_DIR"
nix build --no-link --max-jobs 1 --cores 4
```

The first command starts its own `dbus-run-session` and synthetic portal; it does
not use the desktop bus. It covers consent, missing interface, denial, no grant,
revocation, SetSelection failure, partial MIME, format-only laziness, actual pipe
completion/failure, owner replacement, open-pipe cancellation and delayed D-Bus
replies. Per-transfer read/write errors and expired serials remain recoverable;
session revocation and service loss stop sharing. The demand suite retains 100
bidirectional text copies, lazy chunked images, directories/empty files,
changed-source rejection and disconnect; it adds missing data-control and GNOME MIME serialization checks.

The retained `native-clipboard-test` fixture must run only in an owned disposable
GNOME/Hyprland VM or isolated compositor, never in the personal desktop session:

```sh
DESKPORT_TEST_SYNTHETIC_SESSION=1 DESKPORT_TEST_WAYLAND_PROTOCOL=wlr-data-control \
  "$FIXTURE_DIR/native-clipboard-test"
```

Use `ext-data-control-v1` to require the preferred protocol or `portal` to require
the consent path. An empty protocol selects production automatic detection. The
native fixture uses two independent clipboard connections and synthetic data;
portal mode needs two separate system approvals. CI runs the private-bus and
offscreen suites and compiles this fixture, but does not run a native session.
Its scope is clipboard provider interop, 100 bidirectional Unicode samples, lazy image/URI MIME, owner preservation
and cancellation. It also runs the production v2 agents against the native
provider and a synthetic peer, checking PNG bytes, directories, empty files,
chunked file contents and changed-source failure in both directions. It does
not establish Nautilus/Dolphin UI paste behavior, hardware streaming/input/audio, physical clipboard acceptance or deployment.

Sources: [Clipboard portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Clipboard.html),
[RemoteDesktop portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html),
[vendored wlr protocol](https://github.com/hyprwm/Hyprland/blob/main/protocols/wlr-data-control-unstable-v1.xml),
[Nautilus native serialization](https://github.com/GNOME/nautilus/blob/main/src/nautilus-clipboard.c).
The wlr XML and scanner-generated client header/code retain upstream copyright
and permission notices; regenerate with wayland-scanner client-header/private-code.

### Retained validation result

| Layer | Result |
| --- | --- |
| Private D-Bus portal fixture | PASS: 19 checks, including cancellation, late replies, partial MIME, denial/revocation and transfer-failure recovery. No desktop service activation directories. |
| Offscreen v2/demand fixture | PASS: 8 checks; platform-specific native cases skip here. Includes observation reentrancy and unsupported-format recovery. |
| Core/pins | PASS: unchanged pins, catalog/session checks and 24 Qt workspace vectors. |
| Native Nix package and packaged helper | PASS: `nix build --no-link` with a scheduling-only builder override; actual compilers verified on CPUs 0–3, nice 15, four jobs. Packaged helper ACK, EOF and malformed-frame checks pass. |
| Ubuntu 26.04 / GNOME 50.1 | PASS: no ext/wlr protocol available; automatic portal fallback, two approved zero-input-device sessions, native provider and v2 data/owner/file-failure checks. The dialog workaround above is required on this baseline. |
| Omarchy 4.0.4 / Hyprland 0.56.2 | PASS: automatic ext v1 and explicitly selected wlr v2 each pass the same native provider/v2 suite. |

Each native mode includes 100 bidirectional Unicode samples, lazy native image
and URI access, an unsupported MIME read, old-owner preservation, production v2
PNG/file/directory materialization and changed-source failure. No native claim is
made for older GNOME without Clipboard portal or a compositor exposing only wlr
v1. The vendored v1 request subset still compiles; that older native environment
was not exercised. Portal-only file-transfer tokens and desktop file-manager UI
paste, physical streaming/input/audio and user activation remain outside this
record.

Both disposable native VM runs exited 0, verified unchanged baseline hashes,
stopped their owned QEMU/SSH forwards/sockets, and released their canonical VM
lock. The earlier capability/consent preflights have separate records, including
the failed Share-disabled attempt. The native fixture artifact SHA256 is
`ce11954f79e8d0d9ffab3d8a6bfbed5c0b2d89dde0350cc481801b5bc6a5f69f`.
Its runtime carries the fixture's resolved `ldd` dependency cohort and library
hashes in the private manifest. No deployed application,
personal clipboard, service, pairing, settings or mynix activation was changed.
