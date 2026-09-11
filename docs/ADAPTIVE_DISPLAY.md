# Built-in adaptive workspace

DeskPort bundles its own macOS virtual-display process. Start Sharing to create
it; no BetterDisplay installation is required. The Sharing page reports the
current pixel size. The chosen sharing resolution is the idle mode, while
Settings → Picture → Match the client window resolution controls adaptation from
this client. These preferences are saved independently on each computer.

The client also remembers each host's last stable window geometry, maximized or
fullscreen state and negotiated 1× dimensions. The next connection starts with
that workspace instead of negotiating the default window size first. A different
display layout, scale or configured window mode invalidates this cache. Fixed
resolution connections continue to use the selected picture settings.

A mutually bound client authenticates with its existing certificate on the binding
TLS endpoint. It pins the saved server certificate, checks the advertised display
capability and claims an exclusive connection-scoped controller. No new listener,
firewall rule, PIN, or host authorization is introduced. Resize frames have bounded
sizes and sequences; late helper acknowledgments cannot complete newer requests.
Revoking a device also closes its display controller. Older servers and legacy
pairings retain fixed-resolution streaming.

After the client window settles for 900 ms and all pointer buttons are released,
DeskPort releases remote input, stops video, adjusts the virtual display, and
resumes the same remote application at the acknowledged pixel dimensions. It
preserves the client window geometry and the controller connection across this
transition. The picture pauses during negotiation. Automatic renegotiation never
honors “quit app after streaming”; an actual disconnect still honors that setting.
A failed resize disables further adaptation for that session and uses the saved
fixed resolution, avoiding a reconnect loop.

Pixel sizes are aligned to four and bounded to 640×360–7680×4320. As of
2026-09-11, automatic adaptation streams the client's drawable pixels 1:1 so
the viewer never upscales text. Clients at 150% system scale or more request a
2× HiDPI host desktop whose logical size is half the pixel size; others use 1×.
The logical desktop keeps a 960×540 minimum. For example, a 3828×2040 client
window at 150% requests 3828×2040 pixels at 2× (a 1914×1020 logical desktop).
Host interface elements then appear about 1.33× the client's own size at 150%
and 1.1× at 180%. This encodes 2.25× the pixels of the earlier 1× policy
(2026-09-10), which divided by client scale and produced visibly soft text.
Cached window workspaces from the 1× policy are discarded.

Sharing startup and the idle-mode restore also use 1×. Existing saved idle pixel
sizes are preserved. The protocol still accepts explicit 2× requests from older
clients. Native display mode is verified after every change; applying settings
alone is not an acknowledgment.

The helper separates only its own display from any asynchronously restored mirror
membership. It never selects a physical or third-party display as its capture
source. The existing physical main display remains unchanged. Automatically
mirroring physical displays from this workspace, switching the main display and
crash-safe physical-layout restoration are separate remaining roadmap work.
Do not remove an existing display utility until its other uses have been checked.

Hiding/minimizing the stream does not release the display. Losing its control
connection releases exclusive ownership; a new approved controller may take over.
A watchdog bounds silent failures, followed by a 10-second idle-mode grace period.
Stopping Sharing removes the virtual display after the host exits. The independent
native Sunshine service and its configuration are not managed by this feature.

## Validation

- `python3 scripts/test-host-lifecycle.py`: isolated helper rejection/timeout and
  existing lifecycle/port coexistence checks.
- `python3 scripts/test-host-lifecycle.py --binding`: loopback TLS certificate
  pinning, unknown device rejection, exclusive resize control and transfer.
- `python3 scripts/test-host-lifecycle.py --ui`: settings and actual stream-page
  replacement without dropping navigation or starting a quit request.
- `python3 scripts/test-translations.py`: the eight primary UI languages.
- Signed macOS packaging and `nix build`: endpoint integration.

Native acceptance also requires repeated client window resizing, maximize/full
screen, minimize/restore, fractional scaling, text and pointer checks, and a full
disconnect/reconnect. Protocol tests do not establish these visual properties.

Native helper validation on macOS: 30 alternating 1280×720, 1600×1000, 1920×1080,
2560×1440 and 2880×1800 modes passed, including changes between 1× and 2×. Each
acknowledgment was independently checked through CoreGraphics for physical pixels,
logical dimensions and an independent capture ID. The active physical main display
retained its mode and the virtual display disappeared on helper exit. This does
not substitute for end-to-end client window and input acceptance.
