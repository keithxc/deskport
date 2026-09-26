# Built-in adaptive workspace

The shared wire contract and workspace policy now live in
[`deskport-core`](../shared/deskport-core/protocol/SPEC.md); see
[the integration guide](SHARED_CORE.md) for pinned dependency updates.

DeskPort bundles its own macOS virtual-display process. Start Sharing to create
it; no BetterDisplay installation is required. The Sharing page reports the
current pixel size. The chosen sharing resolution is the idle mode, while
Settings → Picture → Match the client window resolution controls adaptation from
this client. These preferences are saved independently on each computer.

The client also remembers each host's last stable window geometry, maximized or
fullscreen state and negotiated workspace dimensions and backing scale. The next connection starts with
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

After the client window settles and all pointer buttons are released,
DeskPort releases remote input, stops video, adjusts the virtual display, and
resumes the same remote application at the acknowledged pixel dimensions. It
preserves the client window geometry and the controller connection across this
transition. The picture pauses during negotiation. Automatic renegotiation never
honors “quit app after streaming”; an actual disconnect still honors that setting.
A failed resize disables further adaptation for that session and uses the saved
fixed resolution, avoiding a reconnect loop.

Pixel sizes are aligned to four and bounded to 640×360–7680×4320. The
current shared policy uses 2× backing scale for any client density above 1×,
otherwise 1×. It avoids undersampling and aims for a minimum 960×540 logical
desktop while respecting the pixel cap. For example, 3828×2040 drawable pixels
at 150% client density request 5104×2720 backing pixels at 2×. This policy and
its fractional-scale/portrait/limit cases are defined and tested in the pinned
core, rather than reimplemented by each client. Window cache policy remains local.

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

## Session display ownership and text caret — 2026-09-15

An authenticated resize now includes a local helper `session` flag. While a
session owns the display, the virtual display becomes main and every other online
display mirrors it. Enabling sharing alone keeps the original layout. The existing
10-second controller-disconnect grace interval preserves fast reconnects; after
that interval the helper restores the saved physical layout and idle resolution.
Helper EOF and SIGTERM also restore the layout. A private, atomically written
UUID-based journal permits recovery on the next helper start after a crash.

The helper runs an AppKit event loop and subscribes to display reconfiguration.
A Foundation-only run loop left CoreGraphics mode observations stale after mirror
transactions; merely adding a delay did not solve repeated mode changes. Restore
and idle-mode application are separated by a run-loop interval. Successful resize
responses require both the requested backing/logical mode and the mirror layout.

Clients may opt into `textCaret: true` on `display-resize`. Only that authenticated
exclusive lease receives `text-caret` messages containing `caret.valid` and
normalized `caret.x`/`caret.y` (the insertion point's bottom edge). The helper reads
AXSelectedTextRange and AXBoundsForRange, never the text/value or selected text.
Queries have bounded accessibility messaging timeouts and run at 5 Hz only during
a session. Unsupported focus, unavailable accessibility access, or a caret outside
the streamed display reports invalid geometry. Legacy clients receive no new
messages. The iPad client expires stale geometry and falls back to its touch anchor.

### Validation boundary

A development bundle based on published 0.3.2 was built through the Nix macOS
shell, Developer ID signed, and installed only in the isolated Tart guest.
The native helper passed three portrait/landscape/idle cycles. Independent
CoreGraphics observations during the real iPad test showed virtual-main + console
mirror, restoration to the original console-main 1024x768 logical / 2048x1536
backing mode, then a successful second virtual-main session at 960x1440 logical /
1920x2880 backing. The real text-editor test observed caret geometry before and
after newline input and the iPad kept the new caret above its keyboard.

Host lifecycle tests: 26 passed. Binding tests: 27 passed, including a regression
that unsolicited caret data never interrupts an old desktop client's heartbeat.
This is VM/simulator evidence, not physical multi-monitor, hotplug, headless,
unattended crash-recovery or remote IME-candidate acceptance. A display attached
after acquisition is reconciled by the next resize request; continuous hotplug
reconciliation is not yet implemented.

Final helper revision also reapplies the physical layout after virtual-display
removal, retaining the recovery journal across that removal. The final helper
passed the same three cycles plus orderly EOF shutdown. The final installed
bundle's caret-only live test passed after launching the disposable editor once
the display had settled; a combined run had no valid text focus after reconnect
and therefore failed the caret assertion, while its topology transitions passed.

Known VM presentation issue: Tart's built-in console can retain the pre-mirror
scanout while the iPad receives the live virtual screen. CoreGraphics reports the
expected main/mirror topology, but this does not establish correct console scanout
or physical-monitor mirroring. Window refresh attempts did not resolve it. Treat
that visual acceptance as open rather than equating topology ACKs with pixel proof.

### Windows and Linux caret readers — 2026-09-26

Windows and Linux now supply the same optional `text-caret` geometry used by
Apple clients. A successful session display request starts sampling at 5 Hz;
resize, restore, disconnect and host shutdown invalidate and stop the reader.
PeerManager continues to forward updates only to the authenticated, opted-in
exclusive display lease. There is no new public wire message or core pin.

The reader runs as `deskport-display --text-caret` in a separate, short-lived
process, before the display-owner/driver initialization path. A 400 ms deadline
kills an unresponsive reader; Windows also has a 350 ms in-process watchdog for
provider RPCs. Accessibility errors cannot stop capture or display restoration.
Only geometry is emitted; no text, selected content or accessible names are read,
and raw provider output is not copied into diagnostics.

- Windows uses a DPI-aware UI Automation caret range, with a collapsed selection
  fallback for older text providers. Providers without empty-range rectangles may
  supply the enclosing character's geometry. Native Win32 caret rectangles are
  the final fallback. Normalize physical coordinates against the verified capture
  output, including mirrored-output selection and negative desktop origins.
- Linux uses the AT-SPI D-Bus interfaces already supported by QtDBus, without a new
  package dependency. It discovers a focused element under an active, showing
  window, then revalidates a cached element on subsequent samples. It queries the
  insertion range or nearby character geometry (including the preceding glyph
  when an end-of-document insertion rectangle is unavailable) and normalizes it
  against the matching capture output's Qt logical geometry. Tree size, depth,
  per-call latency and total query time are bounded.
- Linux requires an enabled accessibility bridge in the desktop/application. The
  reader does not change global accessibility settings. Unsupported providers,
  undiscoverable focus, empty-line/end-of-document geometry that the provider
  cannot describe, unavailable output mappings and out-of-display coordinates
  report invalid. Mixed XWayland/Wayland scaling needs real application checks.
  A client's last local pointer/touch position remains the fallback; it must not
  be presented as a verified insertion point.

Run `python3 scripts/test-text-caret.py` in the Linux devShell for an isolated
D-Bus fixture (no desktop session or personal content). It checks discovery,
movement, cached focus, inactive/hidden focus, normalized coordinates, timeout,
output bounds and stop/restart behavior. `scripts/test-host-lifecycle.py --binding`
checks the existing opt-in lease forwarding contract. Actual Windows UIA/Win32,
KDE/GNOME applications, IME candidates and mixed-DPI screens remain live acceptance
checks. A successful helper build is not that acceptance.
