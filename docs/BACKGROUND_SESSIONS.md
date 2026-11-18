# Retained viewers and background activity

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

Updated 2026-10-17. Window visibility and input ownership are client adapters;
shared video-pause negotiation remains unchanged. A viewer stays connected when
hidden, minimized or occluded. Returning to Devices or closing the last viewer
keeps its worker; explicit disconnect ends only that worker, and Quit ends all.
Selecting another device waits for the previous worker's input-release/hide
acknowledgement before showing the selected viewer. An unresponsive worker is
terminated before ownership passes; unrelated connections are retained.

## Visibility capabilities

The worker logs one `Viewer visibility:` capability line when streaming starts.
Unsupported detection conservatively keeps video running. Focus loss alone never
means that video is invisible. No window enumeration, image comparison or extra
polling thread/timer is used.

| Client | Reliable inputs used | Explicit fallback |
| --- | --- | --- |
| macOS Cocoa | SDL hide/minimize and `NSWindowDidChangeOcclusionStateNotification`, sampled once at registration | Without a Cocoa native window, hide/minimize only |
| Windows | SDL hide/minimize; own-window WinEvent cloak/uncloak with DWM state query on registration/event | Hook/query failure uses hide/minimize; ordinary overlap is not detected |
| Linux X11 | SDL hide/minimize; existing graphical-session lock monitor | Arbitrary overlap/workspace/activity visibility is not detected |
| Linux Wayland / SDL2 compatibility | Explicit hide/close; SDL minimize if actually delivered; existing graphical-session lock monitor | No promised minimize, suspended/occluded, desktop or KDE activity notification through the current SDL2 API |

Cocoa uses Apple's [window occlusion notification](https://developer.apple.com/documentation/appkit/nswindow/didchangeocclusionstatenotification?language=objc)
and [occlusion state](https://developer.apple.com/documentation/appkit/nswindow/occlusionstate-swift.property).
It includes fully occluded windows and can reflect Space changes. Windows uses
[cloak/uncloak WinEvents](https://learn.microsoft.com/en-us/windows/win32/winauto/event-constants)
through a [WinEvent hook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook).
The hook receives only the two cloak event types and filters the target process
before reading its own window property; no foreign window property is interpreted
as an in-process pointer. Cloak can reflect virtual-desktop changes; this is not
general overlap detection. Neither is a guarantee about every compositor/desktop configuration.
Wayland downgrade is deliberate: no private SDL3 handles or second Wayland
surface role are used to bypass the pinned SDL2 compatibility layer.

The current default canvas worker has an intentionally hidden SDL decoder
context. It consumes authenticated shell visibility/focus through CanvasBridge,
so that hidden context cannot pause a visible Qt canvas. Native SDL presentation
retains the OS-specific observers above. Canvas mode reports native occlusion
as unavailable and uses shell hide/minimize and normalized lock state.

## Pause, input and audio

An unavailable viewer immediately releases keys, mouse capture and held buttons,
fences in-flight input timer callbacks, stops controller mouse-emulation and
touch gesture timers, cancels touches/pen, and clears controller axes/buttons. It
rejects physical input events until it is available again. A hidden worker never
owns input, even with background gamepad delivery enabled. A selected visible
viewer can retain the user's existing background-gamepad behavior on focus loss.

Video pauses after five continuously unavailable seconds. Repeated hide events,
or a change from minimized to locked, do not restart that deadline. A reveal
cancels it immediately; short hide/recall bursts avoid a pause. Existing optional
host negotiation sends the pause/resume request and requests a fresh key frame
on resume. A new/recovering transport must receive its first key frame before
being paused again. This is a sender transmission gate, not capture/encoder
suspension or a promise of zero CPU/GPU/memory. No media queues, codec, capture
backend or audio route are replaced.

Receive-sound off always skips playback. With mute-on-focus-loss enabled,
unavailable or unfocused viewers mute. With it disabled, background audio stays
permitted, including when video is paused or another retained viewer is shown.
Audio uses each connection's immutable preference snapshot and existing output
backend. Hiding no longer silently overrides that setting. The audio callback's
mute flag is atomic.

Recall shows/restores the same window, cancels video pause and lets the existing
focus/pointer policy regain input. Native visibility changes never select another
host or reconnect a transport. Hidden time cannot settle adaptive desktop size.
Background recovery restores state quietly, without raising an unrelated viewer.

## Verified checks (2026-10-17)

- Linux x86_64 flake package build passed. A test-only pre-unpack hook restricted the
  actual Nix builder to CPUs 0-3 at nice 15; the original build phases and pinned
  dependencies were retained. Headless desktop-state: 12 passed; viewer activity:
  11 passed; peer binding: 172 passed; the full shared-core suite passed. Two
  workers from the new package passed 20 isolated empty-host presentation
  switches, independent cancellation and IPC-loss exit. No native Linux desktop
  notifications or real stream were exercised by these checks.
- macOS locked-devShell compilation of the eight affected production objects:
  session, input, gamepad, absolute/relative touch, audio and both visibility
  adapters passed. This is not a full
  signed/notarized package or an installed-app test.
- macOS dummy visibility/activity checks: 11 passed; desktop-state: 12 passed;
  shared core/Qt workspace adapter: 24 vectors passed.
- macOS native synthetic window notifications passed, including 20 hide/raise cycles,
  minimize/restore, three complete Cocoa occlusion/reveal cycles and observer
  recreation on a retained window. The concurrent timer test verifies that hide
  waits for an in-flight send and rejects subsequent callbacks, previous-visibility
  epoch tokens after recall, and callbacks after handler destruction. A separate
  queued-hide-after-restore regression ensures input resumes exactly once even
  when native visibility has already advanced ahead of the event queue. The
  fixture draws its own opaque Cocoa cover
  and raises its viewer, avoiding unrelated-window stacking and transparent
  backing stores. It does not inspect work applications.
- Windows retained-cross-SDK compilation of all seven affected production
  objects passed. A separate statically linked fixture ran in an interactive
  Windows 11 session: 11 passed, including 20 hide/recall cycles, minimize/restore,
  native DWM cloak/reveal and observer recreation. The harness required the
  native capability and minimize event to be exercised. Its temporary scheduled
  task, process and exclusive GUI lease were removed afterwards.
- Multi-worker IPC: 8 passed on both macOS and Linux, including repeated hide/show,
  slow/stalled handoff,
  independent cancellation, quiet recovery and a synthetic exclusive input
  lease. No two workers acquired that fixture lease simultaneously; this is
  protocol/lifecycle evidence, not physical input injection acceptance.

## Resource baseline and remaining acceptance

Before changing resource policy, two production workers with empty isolated
settings, no saved host and no stream were sampled for 20 seconds at two-second
intervals: mean CPU 0.150% and 0.350% of one core, peak RSS 40,544 and 40,560 KiB.
Twenty switches, independent cancellation and IPC-loss exit passed. This measures
connection-wait overhead only. It is not an active/paused decode or network A/B;
no decoder/capture optimization is justified by this sample.

Reproduce offline checks in the locked devShell, with `TMPDIR` inside the build
root (use a short path for Unix local sockets):

```sh
nix develop -c python3 tests/test-viewer-activity.py
nix develop -c python3 tests/test-host-lifecycle.py --sessions
nix develop -c python3 tests/test-desktop-state.py
nix develop -c python3 tests/test-core.py
nix build --no-link
```

`test-viewer-activity.py --native` opens only synthetic windows and requires
exclusive GUI ownership. It tests OS hide/restore, available minimize events,
Cocoa overlap and Windows cloak where supported. It does not connect to a host
or inject physical input. Dummy fixtures and synthetic worker IPC checks are
separate from those native notifications.

Real-stream acceptance still needs a deliberately authorized isolated test host:
measure sender frames/suppression and bytes, client CPU/RSS/GPU, wakeups and fresh
frame recovery for visible, five-second hidden, minimized and restored phases.
Keep resolution, codec, FPS, audio choices and content identical. Test 20 repeated
hide/recall cycles, rapid host selection, close-to-background, last-window close,
Quit, recovery during hide, held keys/buttons and competing controllers. Verify
mute-on-focus-loss both ways and sound-off independently. Do not use personal
pairings, clipboard data or existing streaming sessions for automated checks.
