## Bazzite AppImage runtime and disconnect recovery — 2026-10-15

Delivered as the Linux x86_64 [0.7.2-rc.2 AppImage test release](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2),
after merging the fixes with main through `bda002ff`. Tag source `4124e631`
and binary source `75cba028` have identical trees; both pin core `4064059`.
The integrated compressed artifact passed a fresh 20 disconnect/reconnect cycles
and 60 exact resizes: 483 output events, 490 Qt screen events and 614 fixture
frames, with the real GUI alive and every baseline restored. The integrated
native checks passed 39 lifecycle, 172 binding, 13 service and 27 UI tests,
plus KWin 6.6.6/6.7.5 baselines and four Sunshine software capture/encoder probes.
All tag-source GitHub checks passed, eight public assets were anonymously
downloaded and verified, and the resource audit found no owned process,
container, active socket or occupied test port. Complete source and tested
relinking materials accompany the artifact. See the release verification record
for exact hashes, historical controls and device-acceptance limits.

The reporter confirmed issue #5's KWin output-registry and VA-API fixes on
Bazzite 44 / KWin 6.7.5, then reported a helper glibc mismatch and a Qt Wayland
disconnect crash. Reproduce these follow-ups using the official Bazzite 44 image
and the reported glibc 2.43-9 package cohort, with an isolated compositor and
application profile.

AppImage child launchers now retain the selected host loader's matching library
cohort, including the independent display-recovery process. Viewer and Sunshine
launchers still select their own graphics runtimes. Automatic KWin grants use
the actual running helper identity; extracted AppImage login startup returns to
AppRun instead of bypassing its environment setup.

Disconnect and exact-size output replacement restore local outputs before
releasing the virtual primary. Normal helper EOF first requests restoration
through its independent recovery connection, with bounded retries. Display
shutdown gets its full recovery grace after stdin closes, including when
Sunshine is slow to terminate. Owned Sunshine children are terminated on Linux
parent death; existing display recovery and journal retention remain in place.
These are native lifecycle and packaging fixes, with no shared wire or media
policy change.

The original 2560x1440 regression also reproduced the reporter's exact Qt
`QScreen::geometry()` null-pointer stack even when raw Wayland outputs never
reached zero. Qt Wayland 6.4.2 retained its deleted placeholder in the virtual
screen siblings during offscreen-surface reconstruction. The AppImage viewer
backports upstream fix `e038d0b750cf238fef6eeb07a90b7fc1b52e4640`, clearing that
pointer before removal callbacks. Only Wayland Client is rebuilt against the
matching 6.4.2 SDK; complete sources, notices and rebuild materials accompany it.

Historical validation before the main integration (`dd8af971` candidate): the
compressed AppImage, extracted without library removal,
passed 20 disconnect/reconnect cycles and 60 exact resizes under Bazzite 44,
glibc 2.43-9 and KWin 6.7.5. Its real GUI and persistent Qt offscreen/window
fixture used the verified backport, observed 483 output events and 488 Qt screen
events, and restored every baseline without a zero-output interval. Actual
viewer/helper/recovery libc and loader hashes agreed, and automatic permission
entries matched the running helper. The old Qt runtime crashed in the same
original-resolution case; the old 3-second recovery budget failed the delayed
post-removal verification while the new budget passed all three policies.

The final native Nix build, 39 supervision tests, KWin 6.6.6/6.7.5 baselines,
actual software capture/encoder probes, and service/runtime/source checks passed.
Owned test processes and ports were released. The reproducible lab uses an
isolated official Bazzite container userspace and headless compositor on a
NixOS kernel. Issue #5 remains open for the reporter's new-package retest;
physical streaming, input and latency acceptance remain separate.

## Data saver that actually saves — 2026-10-15

Reason: a 100-minute remote session with data saver on still averaged 1.4 Mbps of
video (2.7 Mbps on the Tailscale interface, about 5-10 GB per working day). The
picture was never idle: a blinking caret or typing re-armed the 500 ms full-rate
refinement and the input boost, so the encoder ran at 57-58 fps of ~3 KB frames.
On a remote path each frame pads to a full 1 KB packet and carries at least two
parity packets, so frame count, not picture content, set the cost.

- Data saver now has its own cadence on the host (`host/common/framecadence.h`,
  `scripts/patch-host-data-saver.py`): a change is sent at once, continuous change
  at most 20 fps, four refinement duplicates per second for one second after a
  change or input, then a 5 s refresh. Resolution and the negotiated frame rate are
  unchanged; recovery key frames are never delayed. Both encoder paths (async
  VideoToolbox and the synchronous VAAPI loop) use it.
- FEC: with data saver on and no unrecoverable block for 60 s, a frame carries one
  parity shard instead of the viewer's minimum of two; any unrecoverable block
  restores the minimum for a minute.
- The host learns the state at launch (`deskportSaver`) and from the optional
  `saver` field of `video-rate` (core contract), so a live toggle switches the
  cadence too, including a manual bitrate below the saver cap.
- Audio is unchanged: the macOS host currently sends almost none (17 packets/s
  with video paused). A stereo stream would add about 0.3 Mbps (96 kbps CBR in 5 ms
  packets with 50% FEC); revisit if audio capture is enabled.
- Desktop mode no longer swallows the first click: a press inside the picture
  takes input back and is sent to the host.
- Validation: data saver cadence/gate/FEC policy tests, both host overlays,
  peer-binding `saver` relay, echo, invalid and conflicting duplicate cases.
  Live traffic on the 5G path is still to be measured.
- First live check (bda002ff): the host still encoded 56 fps with data saver on.
  The content gate only saw images already queued, while most images arrive
  during the encoder's 2-10 ms wait in `pop()`. The gate now applies after a pop:
  a denied picture is converted and held, and sent once the gate opens.

## Live data saver and background pause — 2026-10-15

Reason: switching data saver reconnected the stream (about 2.7 s on 5G).

- Core contract `protocol/VIDEO_RATE.md` (core `369d0fb`, branch `video-rate`):
  hosts advertise `videoRate: 1`; opted-in viewers send `video-rate` with a
  ceiling in requested-bitrate units and receive `live`. The host scales its
  encoder target by min(1, ceiling / requested) without a key frame or a
  resolution change. Hosts that cannot change rate live answer `live: false`
  and the viewer reconnects, as it does with older hosts.
- Viewers that see the capability negotiate the full automatic budget once and
  pass the data saver ceiling as `deskportRateCap` in launch/resume; toggling
  then only moves the ceiling. Sunshine exposes `rate` on the lease-checked
  loopback API (`patch-host-rate-api.py`, after the video pause overlay).
- Background pause stays limited to a viewer hidden from the tray: video
  pauses after 5 s hidden and resumes with a new key frame when it is shown.
  Minimizing is not used. On KDE Wayland a client receives no minimize event;
  KWin's xdg_toplevel `suspended` state (minimized, other desktop or activity,
  show desktop, lock screen, display off; never mere overlap) would be exact,
  but sdl2-compat drops SDL3's occluded event and masks its flag, so reading
  it would mean bypassing the compat layer. Deferred until there is a reason.
- Closing the viewer with its close button hides it the same way. On Linux a
  locked screen also counts as hidden: `ScreenLockMonitor` follows logind's
  `LockedHint` of the user's graphical session (`User.Display`, since the
  resident client is a user service outside any session), so video pauses 5 s
  after the screen locks and resumes on unlock. Client sleep already ends the
  session before suspend through the logind delay inhibitor.
- Desktop mode takes input again when the viewer regains focus (recalled from
  the tray, a new connection, Alt+Tab) instead of waiting for a click that was
  swallowed. Routing still needs focus and the pointer over the picture.
  Relative mode keeps click-to-grab, and Ctrl+Alt+Shift+Z stays released until
  the next click inside.
- 5G comparison of 68ce0e4e against the previous build (19 vs 24 min, same
  path): key frames 2.2 vs 3.6 per minute, no 5 s window with three or more key
  frames (was 6, up to 8), rate at the floor once (was 8). Two windows still lost
  over half their frames after a large frame was lost: with data saver a still
  picture refreshed only every 5 s, so a lost recovery key frame was noticed late.
  A recovery key frame now starts the 500 ms full-rate refinement window.
- Validation: 9 new protocol cases (host: live, recorded, legacy, intruder,
  invalid; viewer: legacy, live, recorded fallback, error) among 170 binding
  checks, ceiling policy tests, diagnostics, UI and multi-session suites.
- Before a public release, merge core `video-rate` and pin a core main commit.

## 5G session fixes — 2026-10-15

Reason: a recorded pk4 → mm4 session over a direct Tailscale path on 5G
(IPv6, 40–70 ms RTT) showed three problems.

- Rate control cut repeatedly to the floor while an external RTT spike was
  already draining (279 → 205 → 135 → 98 ms) and the stream sent only about
  1.2 Mbps. The controller now holds while RTT is at least 10% below the value
  at the last cut (for up to 6 s), and cuts 15% instead of 30% when the measured
  send rate is under half the target. The network thread reports the send rate
  once a second.
- Key-frame storms: after 5G loss the client asked again for nearly every frame,
  producing eight 113 KB key frames in five seconds and a 97% frame-loss window
  (and 95% again a minute later). `deskport::KeyframeGate` coalesces recovery
  requests for max(250 ms, 2×RTT) up to 1 s; FFmpeg encoders answer reference
  invalidation with a key frame, so both share the gate.
- Data saver now also lowers the still-picture refresh from 1 fps to 0.2 fps
  (`deskportSaver` launch argument; older hosts ignore it). A lost final frame is
  then noticed up to 5 s later.
- Switching data saver still reconnects once (about 2.7 s on this path). A live
  ceiling change over the display-control channel is the next step and needs a
  shared-core contract change.

## Automatic bitrate and data saver — 2026-10-15

Reason: the user wants bitrate chosen automatically, with a single switch for
saving data instead of a manual Mbps value. Measured 5K scrolling needed well
above the saved 25 Mbps limit on a LAN.

- Automatic picture mode (smart streaming) now uses the resolution-aware budget
  (`DeskPortStream::automaticBitrate`, 5–80 Mbps) and ignores the saved manual
  bitrate. Manual mode keeps the saved value.
- A global data-saver switch in the top bar caps either mode at a quarter of the
  automatic budget, 3–12 Mbps (`dataSaverBitrate`), stored as the device-
  independent `datasaver` key. Toggling reconnects live sessions once, through
  the existing manual-reconnect path that rereads the saved profile.
- The Advanced page shows "Automatic" for bandwidth in automatic mode.
- Resolution always follows negotiation. Host live rate control still lowers the
  bitrate on congested remote paths. The seven maintained catalogs are
  translated and pass the source and compiled translation checks.
- Validation: policy tests, 27 UI page checks (switch placement, narrow widths),
  translation coverage and compiled catalogs.

## Daily traffic budget — 2026-10-15

Reason: the user wants a daily data allowance with a reset, amber above 70% and
red above 85%.

- `app/backend/trafficaccount.h` adds cumulative per-session counters (a falling
  counter is a new session) and rolls over at local midnight; `TrafficBudget`
  keeps the enabled flag, limit (GB), today's usage and the day in local settings,
  writing at most every 10 s. Worker sessions report through the shell; an
  in-process session is sampled once a second.
- The top-bar traffic chip shows today's percentage while the budget is on and
  turns amber at 70% and red at 85%. Clicking it always opens the panel, now with
  a daily budget switch, limit (0.5 GB steps), usage bar and Reset.
- At 100% data saver turns on once for the rest of the day (a reset re-arms it);
  nothing disconnects. Counts application bytes of every session on this computer,
  not IP/VPN overhead or the carrier's bill. Hidden on Windows, which has no
  session byte counters.
- Validation: accounting policy tests, UI checks for chip colours, panel controls,
  once-per-day exceeded signal and day rollover, translation checks.

## Slow remote links — 2026-10-14

Reason: pk4 → mm4 over Tailscale measured 328 ms RTT through the DERP relay and
roughly 3 Mbps of TCP throughput, while smart streaming started at 15 Mbps for
2560×1440. The negotiated resolution is never reduced; only bitrate, pacing and
encoder quality floor adapt.

- Client: the smart budget is unchanged on every route. An initial 8 Mbps remote
  cap was withdrawn the same day: Tailscale paths on a fast LAN count as remote,
  and it visibly blurred scrolling text. The host adapts on slow links instead.
- Host (both pinned revisions, `scripts/patch-host-remote-rate.py`): sessions
  whose video peer is not on-link/private (Tailscale 100.64/10 and
  fd7a:115c:a1e0::/48 count as remote) sample the existing ENet control RTT twice
  a second. `host/common/ratecontrol.h` lowers the target by 30% when RTT stays
  above the 30-second minimum by max(30 ms, 25%) or on unrecoverable FEC, at most
  every two seconds, then restores 5% of the ceiling every three quiet seconds.
  Floor is max(1 Mbps, ceiling/8). The learned rate survives adaptive-resize
  reconnects for five minutes. LAN sessions never receive samples.
- Live encoder rate: only FFmpeg VideoToolbox (H.264/HEVC) implements it, by
  reading the pinned FFmpeg encoder's session prefix and verifying the
  CoreFoundation type (`host/macos/vtbitrate.h`). Other backends log
  `live_encoder=no` and keep the negotiated rate.
- Frame size: below half the ceiling, VideoToolbox gets MinAllowedFrameQP 32.
  Short DataRateLimits windows were measured to have no effect on content-change
  frames, so they are not used.
- Remote pacing: four times the live target, at least 20 Mbps, instead of about
  800 Mbps, with send batches limited to one pacing slot. LAN pacing unchanged.
- Contiguous frame numbers: rate-controlled encoders may drop frames. A numbering
  gap made the client treat it as network loss and request an IDR; packets are now
  numbered by produced frame. Recovery matching still uses the original PTS.
- Still-picture refinement: after the last captured change, duplicates are
  encoded at the full (congestion-limited) cadence for 500 ms instead of the
  one-fps idle floor, on both the async (macOS) and sync (Linux) paths.
- Diagnostics: numeric network, rate and encoded-video records, including the
  controller start (ceiling, floor, resumed rate, live encoder support).

Live LAN check (pk4 viewer → mm4 host over its Tailscale address, 2026-10-14):
the session was classified remote with 5–9 ms RTT and no drops. A 40 s
competing bulk transfer to pk4 raised RTT to 37 ms; the target fell from 5708 to
3995 kbps within about three seconds and returned to the ceiling about 30 s after
the load, with no client network drops. A static text page scrolled with real
wheel input on pk4 showed the cause of blurry text: frames compared against the
fully settled picture stayed at 21 dB PSNR for about two seconds after scrolling
stopped, then improved about once per second (37, 44, 49, 60 dB) until identical
after about six seconds — the one-fps refinement cadence.
After activating 9ec33772 on pk4/mm4/mm1, the same page and probe gave 41 dB
at the first capture after scrolling stopped and an identical picture by the
next capture (about one second), in both directions. The session started with an
18988 kbps ceiling and `live_encoder=yes`; client network drops stayed zero.

Scrolling clarity while moving: an offline VideoToolbox HEVC sweep (same host
options, 2560×1440 rendered text, luma PSNR against exact source rows) found a
cliff between 88 and 96 px of vertical motion per frame: 51 dB below it at any
bitrate from 15 to 40 Mbps, about 20–25 dB above it even at 40 Mbps, while the
encoder spent its whole budget. One instant 100 px jump scored 24–35 dB; the
same 100 px over four frames scored 51 dB at 3.3 instead of 19 Mbps. Bitrate,
30 fps, H.264 and a max-QP cap did not move the cliff. The macOS host now paces
injected wheel scrolling (`host/macos/scroll-pacer.h`,
`libvirtualhid-paced-scroll.patch`): at most 40 pt per 16 ms step, first step
immediate, total distance and direction preserved. Live fast-scroll baseline
before this change (a notch every 30 ms): mid-scroll Laplacian variance about
7000 against 11200 settled. Linux host encoders were not measured.

That first version did not change the live result. Measured on mm4: the pk4
session encodes the full 5104×2720 backing workspace (1 pt = 2 px) at about
33 fps, so a 40 pt / 16 ms step still moved 100–200 px per encoded frame (page
log: median 50 pt, p90 80 pt per 30 ms). At 5104×2720 the cliff is the same
~90 px; below it the 19 Mbps ceiling (the saved 25 Mbps setting minus FEC) also
limited quality: 80 px/frame scored 43 dB mean at 19 Mbps but 51 dB at a 40 Mbps
ceiling while only using about 21 Mbps, and 52 dB at 60 Mbps. Steps are now
bounded to 40 captured pixels per 16 ms using the display's backing scale (20 pt
at 2x). The smart budget clamp rises from 40 to 80 Mbps so a 5K workspace can
use its computed budget when the user's limit allows; the host adapts on slow
links. The user's saved bitrate remains the ceiling.

Live check of that build: Chrome received paced wheel events (40 + 10 pt about
every 16–20 ms), but the step stayed 40 pt — the host process resolved a 1×
scale. Its CoreGraphics mode cache can miss the per-session virtual display, as
`admitted-display.h` already documents; the pacer now takes the scale from the
owner's published admitted-display mode first and falls back to CoreGraphics.

After activating 260ecf22 (and a 60 Mbps saved limit, 44 Mbps ceiling after
FEC/audio): Chrome received 20 pt wheel steps (458 of 479 events); per 30 ms the
page moved a median 40 pt (80 captured px), at most 60 pt. Mid-scroll Laplacian
variance rose from about 7000 to 8700–11100 against 11200 settled. Cost: pacing
limits throughput to about 1250 pt/s on a 2x display, so a 33-notch/s spin
(3.0 s of input) finished scrolling 4.3 s after it began; ordinary speeds below
about 25 notches/s see no backlog.

Evidence: `tests/vt-live-rate.cpp` (same prebuilt FFmpeg, 2560×1440 synthetic
content, no capture/input) on mm4. Lowering a live HEVC/H.264 session from 15 to
3 Mbps cut high-motion output from about 14–15 Mbps to about 2 Mbps, with frames
dropped by VideoToolbox. Content-change frames stayed about 400 KB with 100 ms
limits; QP floor 32 reduced them to about 280–310 KB (static scene). Policy
regressions are in `tests/smart-stream.cpp`; both host overlays pass
`scripts/test-host-cadence-overlay.py`.

Not yet verified: live streams over the real relayed path, host CPU, visual
quality at the floor, and whether pacing helps on this TCP-relayed path. Restoring
a direct Tailscale path (or a nearby DERP) remains the largest improvement and
belongs to the system configuration, not DeskPort.

## Native application icon appearances — 2026-10-12

Preserve DeskPort's mint/amber kite and tilted terminal display while improving
their proportions, frame weight and material highlights. One maintained SVG now
generates portable ICO/ICNS, Linux hicolor PNGs and Apple's layered .icon artwork.
macOS builds compile Assets.car and preserve the system-selected Default, Dark
and Clear appearances; bundle validation requires both native and fallback icons.
Qt and SDL retain the native icon. Windows Setup/portable resources and Linux
desktop/AppImage/Flatpak references use the same brand source. Native appearance
and package validation are tracked separately from deployment and streaming.
Apple uses only its system outer contour, without an inset second tile. The same
generator exports native Apple and adaptive/themed Android client snapshots with
source/asset hashes; brand packaging stays outside the shared protocol core.
Validation: signed macOS package and Finder preview, native Apple appearance
previews, pk4 Nix build with installed SVG/PNG checks, Windows executable build
with all seven ICO images verified, and native/Flatpak icon-staging checks passed.
The mobile consumers also pass isolated iOS/Android compilation and packaged-icon
checks. Physical Windows/phone launcher acceptance and deployment remain separate.

## Desktop 0.7.1 formal release — 2026-10-11

Released the stable PC matrix as [0.7.1](https://github.com/keithxc/deskport/releases/tag/v0.7.1)
from desktop source `57b5ffed8664efceed3c8fce38ec456137903b5e` and shared core
`108ca7d24c023b443687971690b1066427012608`: macOS arm64 notarized DMG/ZIP,
Windows x64 offline Setup/portable ZIP, Linux x86_64 DEB/RPM/Arch/AppImage/portable,
Nix and client-only Flatpak. All 19 public assets were downloaded without login
and matched local bytes, GitHub digests and the published checksum manifest.
Exact dependency sources, licenses, relinking materials and the verification
report accompany the binaries. Mobile distribution remains separate. User
activation and current physical streaming/input acceptance remain separate from
compilation, isolated regressions and package checks.

Windows build and source-material export now accept an independent build root so
formal builds preserve source checkouts and reused dependencies. No wire or media
policy changes are part of release preparation.

## Preparation failures and private integration — 2026-10-09

Client preparation now distinguishes cancellation, denial, busy/expired admission,
host unavailability, virtual-display failure and capture/encoder launch failure.
Existing protocol results retain their admission rules; an unavailable host is
never interpreted as denial or permission to capture a physical display.
Diagnostics retain only fixed failure stages/codes and allowlisted runtime
backend choices, revalidated at ZIP export. Raw error strings remain excluded.
KWin 6.7 and portable libva/Mesa fixes are being integrated for private testing;
issue #5 remains open pending the reporter's physical Bazzite acceptance.

## KWin 6.7 output discovery — 2026-10-09

Reason: [issue #5](https://github.com/keithxc/deskport/issues/5) reports an
AppImage virtual output being removed before session admission on KWin 6.7.5.
KWin 6.7 announces devices through `kde_output_device_registry_v2` rather than
individual globals. Bind that registry, observe device removal, and retain the
KWin 6.6 global path. The output probe follows the same protocol transition.
Custom modes select the nearest advertised refresh because KWin 6.7 derives
CVT timings (for example 59.934 Hz for a 1920x1080 request at nominal 60 Hz).
Pixel dimensions and scale must still match exactly after application.
KWin 6.7.5 output-management v21 also rounds custom widths to multiples of eight.
The helper first reuses exact advertised modes. If custom mode generation cannot
provide the requested pixels, it sequentially replaces its owned output with an
exact initial mode, retaining the name, policy and original recovery snapshot.
The original 1668x2388@2 portrait regression remains required on both versions.

The isolated regression adds eight repeated 2560x1440 admissions and records
owned child PIDs. The AppImage-only `0.7.0-rc.3` candidate must pass KWin 6.6.6
and 6.7.5 lifecycle, policy, permission and packaged-runtime checks before
publication. Bazzite hardware capture, input and VA-API remain reporter checks.

Final artifact capture exposed a separate portable-runtime omission: the bundled
PipeWire library could not load its `support.system` SPA factory on a host without
Ubuntu's plugin paths. Bundle matching SPA plugins, client modules and client
configuration, with private launcher paths and corresponding source notices.
Also bundle Mesa's GLVND EGL vendor JSON so the software capture fallback is
discoverable when Ubuntu's system vendor paths are absent.
Require real packaged Sunshine capture/encoder probes in the final display matrix,
not just native capture and package startup.

Acceptance checkpoint: the final extracted AppImage passed all eight KWin
6.6.6/6.7.5 runs (policies 0/1/2 and automatic permission/remount handling),
including real software capture/H.264 encoder probes at four sizes, exact-pixel
resizing, physical-fallback rejection and recovery. Process mappings verified
that SPA and PipeWire client modules came from the private bundle. Ubuntu 22.04
and Fedora 44 package startup/session-API checks passed. The native Nix build and
its packaged KWin 6.7.5 policy-0 capture/lifecycle check also passed. This is an
AppImage-only prerelease on a fix branch; stable/main and deployed services remain
unchanged. Keep issue #5 open for the affected user's actual Bazzite retest.

## Compact browser top bar — 2026-10-09

- Align the Web header with the native top bar: shared icon, inline product name
  and build version, 56px desktop / 52px phone height, compact connection state
  and disconnect action. Long host names truncate while actions remain reachable.
- Keep browser pairing, media controls and fullscreen behavior unchanged. The
  version follows the serving binary; narrow screens hide it like native UI.
- Verified: 66 isolated Chrome/WebRTC checks, including 320px long-name
  layout, and 163 isolated HTTPS/authentication checks including serving-version
  substitution. Physical-device acceptance remains separate.
- Full Nix build remains unverified: the local macOS flake has no default package;
  the aarch64 Linux builder killed `cc1plus` while compiling the unchanged Sunshine
  dependency, including a retry requesting one core.

## Persistent browser pairing — 2026-10-05

Reason: the user requested native-client-like reconnects in ordinary Chrome and
iPad Safari, without an installed web app or repeated manual code entry.

- Keep the permanent six-character code for initial enrollment and temporary
  access. Remembered browsers receive independent HttpOnly cookie credentials;
  the server atomically persists only their hashes and device metadata.
- Restore a short-lived session after page, browser or server restart.
  Starting media remains an explicit click. Disconnect keeps the pairing;
  forgetting or locally removing it invalidates the browser's sessions.
- Add the paired-browser list and removal inside the existing Sharing section,
  plus `deskport web list` / `deskport web remove BROWSER_ID` for SSH operators.
- Verified: 130 HTTPS/authentication checks, 21 browser lifecycle checks,
  31 local CLI checks, 27 UI checks and 53 Chrome interaction regressions.
  Ordinary Google Chrome 154 passes 71 production-HTTPS pairing checks, including
  full browser/server process restarts, temporary mode, independent profiles,
  forgetting, and replay rejection after local revocation. No PWA is required.
- The final native Linux host passes 72 real capture/audio/pairing checks:
  closing an idle paired tab preserves the active owner; revocation stops media
  and releases admission; new enrollment starts changing video and audio again.
  All fixture processes exit. macOS and Linux Nix application builds and the
  final packaged Linux CLI enrollment/list/removal checks pass.
- Browser automation explicitly accepts its isolated self-signed certificate.
  An additional WebKitGTK probe did not reach the initial pairing state and
  completed no product assertions; its cause remains unresolved.
  Physical iPad Safari, initial certificate trust, native input, compositor
  display restoration and deployment remain separate acceptance checkpoints.

## Browser client — 2026-10-05

Reason: devices without DeskPort, including an iPad browser, need a direct URL
or QR entry. The requested first authentication version is one permanent
server-generated six-character code; rotating codes and key enrollment are
deferred.

- Add a default local HTTPS entry, embedded touch-oriented web client, local QR
  and code display, and `deskport web info`.
- Reuse native capture/encoding and shared session admission through a direct
  H.264/Opus WebRTC transport. Stop media/input, restore display, then release
  the exclusive session reservation.
- Provide touchpad/direct touch, keyboard, composed text, explicit paste, audio,
  fullscreen and disconnect/re-entry. The initial version required manual code
  entry for each new page; persistent pairing is tracked above.
- Verified: macOS application/host and native Linux Nix builds; 68 HTTPS checks,
  21 browser lifecycle checks, 30 local CLI checks, 26 existing UI checks, and
  31 Chrome interactions with the production WebRTC transport and encoded test
  media. The final packaged Linux CLI also passes without a display.
- Native media checkpoint: isolated Xvfb capture and a private PulseAudio tone
  pass 25 browser checks over the production HTTPS gateway, web client and final
  host. Both connections decode changing 1280 × 720 frames at 30 fps, receive
  nonzero Opus audio, stop cleanly, and require the same permanent code again.
  The fixture verifies admission is free after disconnect and cleans up every
  owned process. It uses a test management adapter and disables native input;
  it does not exercise the production compositor/display manager.
- Physical iPad Safari certificate/audio/touch acceptance, native OS input,
  compositor display restoration, installed host activation and distribution
  remain separate checkpoints. See [browser access](BROWSER_ACCESS.md).

## SSH mobile pairing invitations — 2026-10-04

Reason: an SSH operator needs to offer a phone a verifiable connection entry
without manually copying a host address and approving a separate desktop dialog.

- Add a five-minute, single-use invitation and terminal QR to the existing local
  CLI. Explicit address, TLS certificate pin and host UUID identify the host.
- Mobile preview performs TLS/hello verification only. A user confirmation sends
  the invitation token and begins the existing client-only binding handshake.
- Replacement, revocation, expiry and restart invalidate unused invitations;
  consumed tokens never return after authorization failure. Normal manual
  requests keep their existing explicit approval flow.
- Checkpoint: isolated host/CLI/QR decode checks, mobile cold/warm deep-link and
  confirmation checks, then physical-camera and real-client acceptance. No
  deployed services are changed by implementation or fixture tests.

## SSH host management and Omarchy — 2026-10-03

Reason: a Linux host must be configurable and manageable through SSH, including
Omarchy/Hyprland sessions and VPS desktops without a local control window.

- Add a user-scoped local management endpoint for status, sharing, device
  approvals and configuration. Keep the existing binding authorization and
  credentials; expose no network management API or automatic approval.
- Add a foreground Linux host, service configuration and prerequisite diagnostics.
  A VPS still needs a compositor, capture, input, audio and encoding resources.
- Implement Hyprland virtual output ownership and direct capture of that output.
  Keep physical monitor layouts intact; unavailable capture must fail explicitly.
- Native Linux builds, the packaged CLI without a display, 13 control tests,
  12 service tests, 30 Linux setup checks and 21 Hyprland fixture checks pass.
  A real isolated Hyprland compositor passes three pixel/scale modes, WLR
  capture and software H.264 probing, target-loss rejection and cleanup after
  disconnect, EOF, SIGKILL and group SIGTERM. See [CLI verification](CLI.md).
- Next checkpoint: an Omarchy installation and a real client must verify
  frames, input/audio, reconnect and reboot/logout behavior. The isolated
  checks do not establish unattended acceptance on a GPU-less VPS.

## Linux repository packaging — 2026-09-27

Prepare AUR `deskport-bin` from the stable portable archive and a standalone
Nixpkgs source candidate with the release's private Sunshine host and FFmpeg ABI.
Track publication separately: AUR new-account registration is temporarily closed; Nixpkgs
requires maintainer review and disclosure before submission. README installation
instructions keep the existing release and upstream flake available while these
channels are pending. Checkpoints: isolated Arch install/upgrade/removal, sandboxed
Nix build and version test, then actual publication/review and channel availability.
The candidate also isolates the session host's default configuration even for
CLI version queries, matching the portable launcher's behavior. A capability
probe now skips Vulkan on fallback windows without SDL_WINDOW_VULKAN, avoiding
a null SDL2-compat Vulkan entry point during headless startup; a packaging
backport and Nix headless-startup regression test cover the released source. These packaging
checks do not add physical streaming/input acceptance.

## Presentation-independent session continuation — 2026-09-28

Reason: opening Devices during a connection must be a presentation change.
Replacing the StreamSegue stack entry during transport recovery or resolution
changes could remove the control center above it and reveal the viewer again.

- Retain the stream page and reconnect its signals to the next transport owner.
  Preserve the viewer visibility request across every continuation path.
- Retain the control-center page and its models across tray switches. Suppress
  adaptive resize decisions as soon as the viewer is no longer requested.
- Record fixed navigation stages, first-frame delay and Qt event-loop stalls;
  no device identity or UI contents enter these timing records.
- Live latency and connection continuity remain unverified until tested on the
  target compositor. Multiple simultaneous servers are a future requirement:
  they need independent transport ownership and a separate active-view selector;
  the current singleton transport is not multi-session support.

## Nonblocking device actions and live revocation — 2026-09-28

Reason: removing an offline binding restarted the local sharing helper and
interrupted unrelated connections; tray actions could remain inside the
streaming invocation or wait for shutdown destructors.

- The authenticated loopback trust API atomically removes one authorization
  while preserving other clients and the admission lease. Older helpers fail
  the operation without a stop/restart fallback. Stopped-helper file writes run
  on a worker; startup readiness is awaited asynchronously.
- Device removal publishes a pending state before work and reports completion
  or failure. Native tray actions are queued after menu dispatch returns.
- Linux streaming returns to Qt's normal event loop while its SDL worker runs;
  session ownership lasts until both execution and transport cleanup finish.
- Disconnect and restart show progress. Explicit exit drains helper processes
  asynchronously before quitting Qt; explicit restart shows the next window
  even when the previous instance started in the background.
- Tests cover live revocation, preserved authorization/lease, failure without
  restart, queued tray dispatch and UI heartbeat during slow helper shutdown.
  Physical tray latency during real streams remains a deployment acceptance check.

## Manual device entry overlay — 2026-09-28

Reason: adding a device should use the same dismissible floating panel as device
settings and details. Every add entry point now opens the shared centered dialog
without changing the underlying page. Its body scrolls, its close button stays
visible, and clicking outside or pressing Escape dismisses it. A successful new
binding closes the panel; background peer refresh does not interrupt text entry.

## Device removal and responsive grid — 2026-09-28

Reason: offline devices returned after restart, and resizing could leave an empty
column or animate cards between inconsistent layouts.

- Remove device now revokes locally stored binding access and clears its saved
  device profile, alias and layout membership. A hashed removal marker blocks
  automatic discovery/replay until an explicit new add or binding.
- All device models detach removed hosts before asynchronous disposal; queued
  callbacks and the delayed settings writer cannot access retired rows.
- Desktop grid columns and integer cell widths follow the same current viewport.
  Resize does not run reorder animations or delayed-width layout calculations.
- Isolated regressions cover restart, binding cleanup, multiple models and grid
  column boundaries. Native resize and live streaming remain activation checks.

## Desktop UI alignment — 2026-09-28

Reason: bring the current mobile visual design to the desktop client.

- Theme tokens (canvas, surface, raised, line, text, muted, accent, tint and
  status colours) drive every card, panel and control and switch live.
- One floating panel style for every choice, device menu, device settings,
  confirmation, message, text entry and wait: centred, at most 460 wide and
  82% tall, dimmed backdrop, fixed title row with a close button. Outside
  clicks, Esc and Cancel close it; destructive actions are red. Combo-box
  drop-downs and context menus are replaced by option panels.
- Device cards: static status light, status-tinted surface, system icon opens
  the device panel, one action button (Connect, Set up access, Checking…,
  Troubleshoot, Return to desktop; groups Open). The add card has a dashed
  outline, a large plus and two tint buttons.
- Device panel: icon, name, coloured status and gear in the header; facts and
  each device action once. Session controls (reconnect, full screen,
  disconnect) appear only for the connected device.
- Device settings: basic rows, then advanced streaming settings expanding in
  place; the full upstream option list opens as a panel. No page is pushed.
- Settings: grouped rows with a three-way theme control and privacy/licence
  reading panels. Manual: chapter cards with icon tile, first-step preview,
  rotating chevron and numbered steps.

2026-09-28 review: pinned the translated manual at public core commit
`78fe2897` in both the Git submodule and Nix lock. Core contract checks, seven
translation catalogs and all 24 macOS isolated UI tests pass, including
light/dark panels and narrow-window screenshots. Activation and real streaming/input checks remain with the
user; this does not request a public release or mobile upload.

## Desktop 0.6.3 release preparation — 2026-09-26

Prepare the integrated main branch for a formal PC release: Linux x86_64
Nix/portable/native/AppImage and client-only Flatpak, Windows x64 offline Setup,
and Developer ID signed/notarized macOS arm64. Rebuild final versioned binaries
and record exact-package checks before publishing. Existing deployed-package
acceptance does not establish acceptance of these new bytes. Service activation
and final physical streaming/input checks remain separate manual steps.

The portable Linux host now applies the same display/EGL/Vulkan ownership
overlays and three FFmpeg Vulkan lifetime backports as the Nix package. It
uses checksum-pinned FFmpeg binaries, matching source and Vulkan headers,
checks ABI headers, and rebuilds only the four affected translation units.

Keep build concurrency at four or fewer and use low-priority build sessions.
The macOS packagers now accept DESKPORT_JOBS (default four). Nix daemon worker
priority and affinity require separate verification from the client process.

## Windows first-launch sharing (2026-09-26)

Default the interactive installer's Run DeskPort checkbox to enabled. A fresh
Windows profile starts sharing when the application opens, without waiting for
setup completion or login-start registration. Preserve saved sharing-off choices,
isolated test behavior and `--no-host-autostart`. Silent installation remains
noninteractive. Native installation and live streaming acceptance remain separate
from compilation and isolated lifecycle checks.

## Linux residual allocation investigation (2026-09-26)

Upstream PipeWire 1.6.8 fixes a client-node mix cleanup error in
[ed9f83d8](https://gitlab.freedesktop.org/pipewire/pipewire/-/commit/ed9f83d805474e877cffaecface9c0bd73bb01cf).
That change does not match the previously observed 32 KiB native-connection
buffer stack: `module-protocol-native/connection.c` is identical between the
reviewed 1.6.6 and 1.6.8 sources. Do not report an untested dependency upgrade as
the fix for this candidate.

A separate allocator observer now records each exact 32 KiB allocation and its
stack, tracks realloc/free by allocation identity, and requires a complete log.
It is a Linux/glibc-only diagnostic under `scripts/diagnostics`, not linked or
loaded in shipping builds. Positive/negative controls include 8 concurrent
threads and 16,001 allocations, with both deliberate retention and complete
cleanup. Across 140 actual Vulkan capture/encoding reconnects, all 852 observed
PipeWire native buffers were released. This narrows the intermittent
candidate but does not establish its absence on all machines or timing paths.

The EGL records match upstream display-identity caching. Mesa's
[`_eglFindDisplay`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-26.1.8/src/egl/main/egldisplay.c)
retains display handles for repeated calls with the same platform/native-display
parameters; libglvnd's
[`__eglMappingTeardown`](https://github.com/NVIDIA/libglvnd/blob/v1.7.0/src/EGL/libeglmapping.c)
releases its mappings on library teardown. `eglTerminate` does not remove that
identity mapping. In isolated native tests, 1,000 serial reconnects used one
EGLDisplay, as did 1,000 cycles on a retained Wayland connection. A distinct
identity control held 100 native connections and produced 100 EGLDisplay
mappings. More importantly, ten actual capture sessions made 32 queries with
23 distinct native/EGL identities. This is a process-lifetime cache whose size
depends on identities, not evidence of a missing host `eglTerminate`.

The host now retains one query-only Wayland connection behind a mutex. Each
query still initializes/terminates EGL and refreshes capabilities; capture
connections retain their independent ownership. A failed roundtrip or changed
Wayland/runtime route discards the connection before retrying. The change avoids
adding an EGL identity per reconnect without unloading graphics libraries.
Ownership/recovery tests exercise 9,004 queries, including concurrent queries,
route changes, disconnects and failed initialization retries. A full Linux Nix
build passed. With the patched production host, 30 actual Vulkan capture/encoding
reconnects returned frames and made 92 queries with one native/EGL identity;
all 184 observed native buffers were released. A separate ten-session test
restarted the isolated compositor halfway through while retaining the host PID:
all sessions returned encoded frames, all 64 native buffers were released, and
queries retained one identity (allocator address reuse across compositor restart
is allowed). These checks cover normal reconnects and one compositor recovery,
not arbitrary driver stacks or unbounded compositor restarts.

Open gate: reproduce the intermittent 32 KiB native buffer with an unmatched
allocation identity and release/teardown evidence. These isolated encoded-frame
tests do not constitute deployed GUI or complete long-duration acceptance.

## Windows reconnect handle ownership (2026-09-26)

Native handle creation stacks identify the three remaining handles per reconnect:

- Two `RawInputManager` objects originate in libvirtualhid touchscreen/pen
  creation. A standalone Windows 11 build 26200 reproduction also retains two
  handles per create/destroy pair, including when both calls share a thread.
  Keep exclusive touch/pen leases in the host input context, bounded by peak
  concurrent clients. Clear active contacts, pen buttons and tool state before
  reuse; discard a device if cancellation fails. This bounds the exercised OS
  lifetime issue rather than claiming to fix Windows itself.
- A QWave file handle originates in ENet's global `qosHandle`: enabling QoS
  overwrites the previous handle. Close it before replacement or disable, so its
  flows are also released. Apply the correction to both the vendored client and
  prepared Windows host; retain normal deinitialization cleanup.

Upstream review: Sunshine [PR 4340](https://github.com/LizardByte/Sunshine/pull/4340)
fixes DXGI adapter enumeration ownership and is already included in the pinned
host. The pinned/current ENet
[moonlight branch](https://github.com/cgutman/enet/blob/aca87840b57f045a1f7f9299e4b1b9b8e2a5e2f1/win32.c)
still overwrites the handle. No corresponding synthetic-pointer lifetime fix was
found in the reviewed libvirtualhid history; its
[Windows backend](https://github.com/LizardByte/libvirtualhid/blob/6fdb8bd4de3b68d96c30e5303ac2ebb333c09746/src/platform/windows/windows_backend.cpp) already calls
`DestroySyntheticPointerDevice`.

The executable regression tests run the patched upstream constructor/destructor
and both QoS switch arms against failure-injecting API doubles. They cover
concurrent lease exclusion, contact/button cancellation, replacement after failed
cleanup, failed creation, repeated QoS enable/disable, and final destruction.
A Windows 11 / AMD AMF run completed 30 capture/encoding reconnects and received
H.264 frames every round. After warm-up, total handles fluctuated between 381
and 384 rather than growing by three per round. The cooled sample had 379
handles, one Desktop, two RawInputManager objects and 25 File handles, with
19,087,360 private bytes. The full Linux Nix build and native regression tests
pass. These results do not establish visual or live pen/touch acceptance.

## Desktop memory display and production diagnostics (2026-09-26)

Show local resident memory after the traffic indicator and refresh every three
seconds while the window is visible. Detail rows separate the client, the managed
sharing host and immediate child helpers. No remote memory is queried. Use native
RSS/working-set queries on Linux/macOS/Windows; show unavailable or partial samples
instead of inventing zero readings for failed process queries. Child PID sets are
deduplicated, so the managed host is not counted twice. Shared pages can be counted
more than once and some GPU allocations are excluded. This is an operational
indicator, not a leak detector. Narrow windows omit the chip.

Production host builds compile lifetime diagnostics to no-ops. Dedicated Nix
`packages.<system>.diagnostics` builds explicitly define
`DESKPORT_ENABLE_MEMORY_DIAGNOSTICS=1`; the existing absolute-path environment
variable additionally enables recording at runtime. Diagnostic logs stop growing
at approximately 4 MiB and report accounting errors without unsigned underflow.
The Vulkan driver lifetime guard is a functional fix and remains enabled.

Native macOS and Linux tests cover self/child/exited-process sampling and
production versus diagnostic compilation; both full builds pass. The same native
process sampler also passes on Windows. The Qt page suite passes 24 checks,
including memory details and toolbar bounds at three widths. Seven language
catalogs and 5,071 compiled translations pass. Compilation is not physical
streaming acceptance.

The old dev/pcui, docs/release-060-preparation and
fix/windows-display-activation-057 branches were already ancestors of main. Their
branch references were retired without deleting checkout files or test artifacts.

## Vulkan driver lifetime across reconnects (2026-09-26)

The remaining repeatable Vulkan growth comes from Mesa CPU topology tables
retained when driver libraries unload. A matched 30-versus-3 session heap profile
attributes 3 KiB per additional session to those tables across encoder probes.
A standalone 100-cycle instance test reproduces 25,600 bytes in these stacks.

Keep one Vulkan instance alive for the Linux host process, initialized lazily
before the first Vulkan encoder probe and destroyed before unloading its loader.
It owns no logical device, queue, surface or encoder; normal device selection and
session teardown remain unchanged. The same 100-cycle driver test retains only
256 bytes in the topology stacks with this guard. This is a bounded-lifetime
workaround for the pinned driver, not a claim that Mesa frees its original table.
The guard is optional on initialization failure and serializes concurrent first
use. Mock-loader ASan/UBSan checks cover concurrency, failure, missing symbols and
instance-before-loader destruction.

Smaller EGL process-lifetime mappings and an intermittent 32 KiB PipeWire buffer
remain separate investigation items. Do not present them as fixed by this change
or interpret balanced host counters as whole-process zero-leak evidence. Private
delivery still requires native media regression and user activation for GUI tests.

## Vulkan encoder teardown backport (2026-09-26)

Real reconnect stress exposed roughly 30 MB of retained allocations per session
despite balanced host lifetime counters. An isolated Vulkan reproduction and
heaptrack traced the dominant allocations to FFmpeg CBS parameter-set clones
created during both encoder probing and streaming. The pinned FFmpeg `fb216b5`
predates the upstream cleanup fixes.

Backport upstream `569674ac`, `ddfa8420` and `b672ae39` to release CBS contexts,
access units, queued image views and session-parameter feedback buffers, and
propagate feedback API errors. Recompile only the four affected archive members
using the original configuration; fail on a version/header mismatch. See
`host/linux/FFMPEG_BACKPORT.md` for provenance and the dependency upgrade guard.

The original three-session heap profile retained 111.95 MB on exit; the CBS-only
control retained 276 KB, with no outstanding CBS-clone allocation stacks. The
complete FFmpeg backport received encoded frames in 30 isolated Vulkan sessions
on each of two Linux machines. All host counters balanced; allocated memory
increased by 0.47–0.59 MB across the subsequent 29 sessions, rather than roughly
30 MB per session. These receivers do not replace deployed GUI acceptance.

The longer profile also identified smaller capture-owned leaks. Store DMA-BUF
modifier lists in vectors, initialize the format count, and own the Wayland
registry, output and interface proxies. Detach monitor metadata from proxies
before returning it past the local connection's lifetime. An extracted-code
regression rejects the original leak and checks 1,000 success, missing-interface,
failed-connect and registry cycles, including metadata lifetime after disconnect.
It passes under Linux ASan/UBSan and macOS UBSan and runs in Linux CI.

The final overlay passes 10 further unprofiled and 30 profiled hardware sessions.
The latter retains 338 KB on exit, dominated by driver/runtime paths; filtered
profiles contain no CBS-clone, monitor-enumeration or modifier-copy leaks from
the identified host paths. This is not a whole-process zero-leak claim, and the
new candidate still requires private delivery and user activation for GUI checks.

## Session memory lifetime diagnostics (2026-09-26)

Add opt-in counters to the pinned Linux host's session, encoder, capture,
PipeWire stream, image, dummy-pixel, AVFrame and EGL-context owners. Build the
explicit `diagnostics` package first, then set
`DESKPORT_MEMORY_DIAGNOSTICS` to an absolute private JSONL path before launching
an isolated diagnostic host. Snapshots after thread joins and after full session
member destruction distinguish outstanding resources from glibc free arena
retention. Dummy pixel counts use owned allocation sizes, never borrowed capture
pixels. Default hosts perform no counter updates or diagnostic file writes.
This instrumentation is diagnostic evidence, not a memory-leak fix or live
acceptance claim. Test the actual extracted owner and allocation functions with
`scripts/test-host-pipewire-memory.py --diagnostics`.

## Public CI core pin repair (2026-09-26)

Publish the shared session admission commit before its desktop consumer. Replace
its private, machine-local Nix snapshot with a public immutable GitHub revision,
and synchronize the submodule and flake pins with the merged core main branch.
The shared C/C++ contracts, portable catalog and Qt adapter vectors pass locally.
GitHub CI remains the clean-checkout and Linux package-build acceptance gate.

## Stress regression fixes (2026-09-26)

Repeated session startup exposed retained PipeWire dummy image allocations in
our pinned Nix host. Own those buffers with RAII, including replacement and reset,
without freeing borrowed capture pixels. The exact vendored owner/allocator
regression rejects the original leak and exercises 5,000 patched lifetime cycles.

Separate bounded incoming TLS candidates from the interactive binding slot.
Keep an existing approval intact while endpoint and topology queries proceed;
release completed probes promptly. Tests cover 512 concurrent path requests,
a stalled original handshake plus a successful fresh endpoint query, competing
bindings, the 64-connection limit and recovery after disconnect.

Compile translations before resource generation on every platform and compare
shipped binary catalogs with source text through QTranslator. Seven primary
catalogs now include the cycle and unverifiable-path messages.

Checkpoint: isolated macOS/Linux regression and package builds, private test
release and mynix delivery. Physical reconnect memory stability and concurrent
three-machine streaming remain activation-dependent acceptance gates.

## Acyclic session admission — 2026-09-26

Reason: allow chained desktops without recursive control paths. Register outgoing
edges before admission, retain them through media restarts and teardown, and
walk authenticated pinned peer links before admission and confirmed takeover.
Reject reciprocal/longer cycles and unknown paths before touching the old lease.
Client-only mobile compatibility remains; desktop chains require updated peers.

Validation: 111 isolated production TLS/binding checks passed on macOS, including
chain, reciprocal/three-node cycle, concurrent pending edges, takeover recheck,
unreachable peers, old desktop/client-only compatibility and reservation cleanup.
Shared C/C++ path validation and core adapter checks passed. Private package
build/notarization and configuration preparation follow; activation and real
video/input acceptance remain the user's separate checkpoint.

## Non-disruptive binding — 2026-09-26

Reason: a new binding must save permission without automatically connecting or
interrupting an existing controller. Desktop navigation already returns to Devices;
add regression coverage for both idle and active outgoing sessions. Replace the
running host's stop/edit/restart grant path with authenticated live trust addition.
Unsupported helpers fail binding without a restart fallback. Update approval copy
in all seven translated catalogs to describe the separate Connect action.

Validation: 102 isolated binding checks and 24 UI checks passed on macOS, including
preserved host readiness and admission lease on grant success/failure. Catalog
coverage passed. Linux x86_64 Nix build and executable smoke passed; the macOS
arm64 host compiled. The real Linux host passed disposable loopback TLS grant,
existing-client/lease preservation, repeat grant, corrupt-state failure and API
authorization checks. Deployed active-stream acceptance remains separate.

Acyclic chains are enforced by the subsequent sessionTopology extension; see
[session policy](SESSION_TAKEOVER.md#chained-sessions-and-cycles).

## Connection feedback localization — 2026-09-26

Reason: desktop interoperation exposed English connection/cancel controls and
keyboard-routing titles inside localized interfaces. Cover StreamSegue and the
native Session, input and clipboard contexts in all seven translated catalogs.
Clipboard transport failures no longer claim that sharing was disabled without
evidence; the message identifies the affected feature and suggests reconnecting.

Validation: catalog coverage and compiled QTranslator lookups passed for all
seven locales; isolated SDL navigation passed on macOS; the Linux x86_64 Nix
package built and passed the isolated executable/desktop identity smoke check.
These changes improve feedback, not transport latency. Candidate deployment and
live verification of translated native overlays remain pending.

Follow-up: complete the directed three-machine streaming/input/reconnect matrix
after occupied sessions are available. Investigate intermittent clipboard
negotiation separately; do not infer its cause from the old generic message.

## Desktop 0.6.0 release — 2026-09-24

Reason: user-authorized macOS/Linux/Windows formal release. macOS notarization,
Linux distro/Flatpak checks, Windows final-installer three protected runtime
rounds and release CI passed. Public source/relink materials were scanned.
Final-artifact physical streaming acceptance and Windows clipboard parity are
not established by these package checks; see [release notes](RELEASE_0.6.0.md).
Mobile 2.0 is distributed separately; Apple build 19 is waiting for review.

## Deterministic host startup responsiveness check — 2026-09-21

The fake authentication process now waits for ten UI heartbeat callbacks before
completing. Normal and slower timer cadences exercise event-loop progress without
assuming a fixed callback count in a 400 ms wall-clock window. Authentication has
a bounded deadline and direct start/stop responsiveness assertions remain intact.

## 0.5.1 macOS and Linux release — 2026-09-22

Published the current desktop baseline with ENet startup failure handling and
preservation of configured connection entries during authenticated refresh.
The user-selected release scope is macOS arm64 and Linux x86_64, including all
supported desktop package formats. Windows remains on its development branch
and is excluded from this release. Native streaming acceptance and deployment
remain separate from package verification and publication. Distribution signing
retries transient Apple timestamp-service failures up to three attempts per
object; all signature, timestamp and notarization requirements remain enforced.
All 11 remote assets matched local SHA-256 after macOS notarization/Gatekeeper,
the Linux installation/API/Flatpak matrix and all final-commit GitHub checks passed.

## ENet host creation failure — 2026-09-21

Reason: a failed ENet host allocation was dereferenced while enabling QoS,
crashing the bundled host during session startup. Return the empty host to the
existing caller error path on all desktop host build routes. Runtime regression
coverage exercises both failure and success against both vendored revisions.

## 0.5.0 stable release — 2026-09-21

Reason: user-authorized desktop release of bounded local diagnostics, update
notifications and encoding-efficiency changes. Published 11 remotely hash-verified assets after macOS notarization/Gatekeeper,
the full Linux installer/API/Flatpak matrix and CI passed. Consumer activation and
real streaming/input acceptance remain manual follow-up checks.

## Default diagnostic recording — 2026-09-21

Reason: user requested logs to be available before a fault occurs. Enable bounded,
privacy-filtered local diagnostics by default; preserve saved off preferences.
Storage limits, retention and manual-only uploads remain unchanged.

## Encoding efficiency audit — 2026-09-21

## Bounded diagnostics and feedback — 2026-09-21

Reason: user-requested privacy-conscious log collection for real connection and
interaction reports. Desktop/embedded host/display logging now shares a default-on
sink with bounded, allowlisted event records. Settings generates a validated local
ZIP and opens a public GitHub issue draft; upload and submission remain manual.
See [DIAGNOSTICS.md](DIAGNOSTICS.md) for the exact audit, storage limits and tests.
Next checkpoint: review this development commit, then separately authorize a
candidate package and validate real user reports after manual activation.


Reason: small screen changes at high FPS still consumed the configured bitrate.
The bundled Vulkan H.264/HEVC encoder test confirmed CBR filler dominates the
simple-scene output. Add shared rate selection/fallback and encoded-byte telemetry;
smart AMD RADV Vulkan H.264/HEVC sessions try bounded VBR. Preserve other drivers.
Match asynchronous IDR diagnostics by output PTS and notify the device editor
after background endpoint refresh releases its busy state.
See [the audit and backend matrix](ENCODING_AUDIT.md). Isolated bidirectional streaming precedes private packaging; manual activation
and the user's own WAN experience remain separate checkpoints. WAN pacing, live bitrate adaptation and unified mobile/desktop budget
fixtures remain explicit follow-up work, not completed behavior.

## 0.4.5 stable release — 2026-09-21

Reason: the user confirmed successful 0.4.4 physical resize/resume, reconnect,
keyboard/mouse input and local display recovery, and requested a full Linux and
macOS stable release. Package validation and published asset verification remain
required. Published all 11 assets with matching remote SHA-256 digests.
Ubuntu/Debian/Fedora/Arch installation, authenticated host API, Flatpak isolated
startup/recovery, Nix smoke, macOS notarization/Gatekeeper and both CI checks
passed. Consuming Linux and macOS system configuration builds passed; activation remains manual.

## 0.4.4 prerelease — 2026-09-20

Reason: withdraw the faulty 0.4.3 release and provide a macOS arm64 / NixOS
x86_64 candidate for manual activation. Includes the managed-display resume
probe fix below. Managed macOS capture currently supports 8-bit H.264/HEVC;
10-bit/HDR is unavailable. Do not promote to stable until real resize/resume,
reconnect, input and local display recovery have been checked after activation.

## macOS adaptive resume deadline — 2026-09-20

Reason: a deployed 0.4.3 session rendered its first frame but timed out after a
window resize. Managed virtual displays must skip the generic one-second display
wake probe and reject unsupported AVFoundation-only formats immediately. Preserve
native ScreenCaptureKit 8-bit H.264/HEVC capture; managed-display 10-bit/HDR remains
unavailable until a native capture path passes resize and reconnect acceptance.
Checkpoint: targeted capture tests, packaged host build, and real resize/resume
with paired client/host evidence before promoting another stable release.

## Formal release 0.4.3 — 2026-09-20

Reason: user requested a new full-platform formal release. macOS app/DMG notarization, extracted-ZIP Gatekeeper and version checks passed. The Linux Nix build, Ubuntu/Debian/Fedora/Arch installers and isolated Flatpak startup/recovery checks passed. Windows remains gated on its separate candidate validation. Universal Apple 1.2 (11) was submitted and reread as Waiting for Review; Android app creation is blocked by Google Play account verification. Next checkpoint: public desktop asset verification, Apple approval/store availability, and resolution of Windows/Android release gates.

## Initial connection window ownership (2026-09-20)

Reason: returning from Devices during host startup could hide Qt before any SDL
window existed. Create the native loading window before launch, keep its owner
responsive during asynchronous connection, and gate Qt hiding on explicit window
readiness. Preserve Devices focus through handoff and cancel pending HTTP launch
without replay. Validate delayed startup navigation, cancellation, failures, and
loading-to-video ownership using isolated Qt/SDL/TLS tests plus native KWin checks.
Actual streaming and input remain separate acceptance checks.

## Cross-platform product catalog (2026-09-20)

Reason: reduce independently maintained rules while preserving public desktop/core
and the private mobile product boundary. Integrate generated port/tuning/policy
rules from the new MIT catalog, keep the legacy GPL code intact, and test native
adapters. Next: shared capability/behavior fixtures and incremental session-state
extraction. Do not bring mobile UI or commercial code into this repository.

## H.264 reference-picture recovery (2026-09-20)

Reason: prolonged desktop streaming exposed repeated reference-picture errors and
keyframe recovery requests. The inherited client SPS fixup reduced every H.264
stream to one reference picture, even when VideoToolbox encoded multiple references.
Preserve the encoder's reference count; reduce excess decoder buffering only when
the SPS explicitly disables frame reordering. Keep reordered streams unchanged.

Regression: `python3 scripts/test-h264-sps.py` compares decoded frame hashes before
and after the real fixup, and reproduces corruption with the old behavior. An
optional synthetic VideoToolbox Annex B fixture exercises the macOS encoder too.
Live validation must separately check decoder errors, recovery requests and network
loss; a clean build or synthetic decode does not establish long-session acceptance.

## Session usability and lifecycle (2026-09-20)

Reason: user approved the audited backlog and Mac/iPad/Android emulator checks.
Implement explicit device actions, authenticated desktop fullscreen release,
on-demand macOS/Linux displays and bounded network recovery, and finish host source
vendoring. Preserve physical topology, exclusive ownership and old-client
compatibility. Mobile auto-reconnect and
mobile vendoring are separate; simulator results do not establish real streams.

## Stable desktop 0.4.2 (2026-09-20)

Reason: user-requested consolidation of desktop development into main and a full
formal release. Includes confirmed takeover, desktop tuning, compact macOS sizing,
dedicated-display recovery and the vendored dependency work. Refresh README feature,
platform and comparison tables. See [release notes](RELEASE_0.4.2.md).

Portable Linux packaging must compile the patched host, rather than bundle an
unmodified upstream executable that lacks the session admission API. Installed
package checks exercise authentication and reject unpaired/browser requests. Native
packages include the KWin display-helper permission entry.

Checkpoint: all established desktop package formats, signing/notarization, isolated
package and regression checks, release source/checksums, and consuming mynix pins.
Activation remains manual; existing TLS and long-session acceptance gaps remain.

## Takeover reason and compact macOS workspaces (2026-09-19)

- Read authenticated takeover termination promptly and resolve it before generic
  video-disconnect errors. Report a terminal error once, with Chinese translations.
- Apply the shared conservative macOS workspace floor after desktop tuning,
  retaining aspect ratio and 1x/2x backing scale. Other host sizing is unchanged.
- Targeted binding tests include an actual displaced desktop control channel.
  Physical Android-to-Apple handoff and Android takeover notice were verified;
  deployed desktop-client acceptance follows manual activation of preview.2.

## Vendored third-party sources 0.4.1 release (2026-09-17)

Reason: `mynix` consumes this flake through a GitHub commit-archive tarball,
which carries no submodules, so the flake had to refetch
`moonlight-stream/moonlight-qt` with `fetchSubmodules` purely to recover gitlink
contents. That made every build depend on six third-party repositories staying
reachable. All of them are now vendored into this repository: the five code
dependencies via `git subtree add --squash`, and `libs/mac` as ordinary files.
`libs/windows` (252 MB) was dropped because only `win32:` project branches
reference it. Upstream URLs, commits and licences are recorded in
[VENDORED.md](VENDORED.md); `shared/deskport-core` stays a submodule because
`deskport-client` shares it.

Side effects: `git archive` now yields a build-complete tree, so
`scripts/git-archive-all.sh` and its GNU tar/Bash 5 requirement are gone, and the
GPL corresponding-source tarball no longer depends on chasing submodule
revisions. Version declarations bumped to 0.4.1 in `app/version.txt` and
`flake.nix`; see [release notes](RELEASE_0.4.1.md).

Released for macOS arm64 and Nix/NixOS only. Client and host behaviour is
unchanged from 0.4.0, so the 0.4.0 native Linux package matrix is not rebuilt.

## Stable desktop 0.4.0 release (2026-09-17)

Reason: consolidate the adaptive-display core, per-device address editing, macOS
mirror-mode/KDE lease recovery and the Linux TCP fallback prereleases (0.3.6–0.3.14)
into main for a formal desktop release. Version declarations were bumped to 0.4.0
in `app/version.txt` and `flake.nix`; see [release notes](RELEASE_0.4.0.md) for the
established macOS arm64 and Linux x86_64 package matrix.

Published as stable `v0.4.0` from commit `6133f7e9`. The macOS arm64 DMG and ZIP
are Developer ID signed, notarized and stapled, with the extracted application
accepted by Gatekeeper. Linux x86_64 DEB, RPM, Arch, AppImage and the client-only
Flatpak were built from the same commit and installed in clean Ubuntu 24.04,
Debian 13, Fedora 44 and Arch containers. Binding, shared workspace contract,
small-MSS TCP and package smoke checks passed on both platforms; per-asset hashes
and the acceptance boundaries this release does not cover are in the release
`VERIFICATION.txt`. Consuming mynix pins were updated for pk4 and mm4; activation
remains the user's manual `rebuild switch` followed by live device acceptance.

The open TLS-fallback pending-handshake issue below is carried forward unresolved.

### TODO: host lifecycle responsiveness assertion is unstable on hosted CI (2026-09-17)

- [ ] Make `HostLifecycle::startupAndStopStayResponsive()` tolerate a slow shared
  runner, or measure responsiveness in a way that does not depend on wall-clock
  tick throughput, so a genuine startup/stop stall is still detected.

Reason: the macOS display workflow failed once on `dev/tcp-path-recovery-0.3.14`
with `'ticks >= 10' returned FALSE` while the rest of the suite passed
(28 passed, 1 failed, 2 skipped). The same test passes locally on macOS and Linux
and on every subsequent main run, including the 0.4.0 release commit, so this is
a CI timing sensitivity rather than an observed product regression. Until it is
fixed, a single failure of this case on a hosted runner should be re-run before
being treated as a release blocker.

## 0.3.14 TCP path recovery (2026-09-17)

Reason: TLS control connections can stall on paths that silently drop larger
TCP segments even when plain HTTP and SSH remain reachable. Linux now retries
pre-application TLS handshakes once with a 900-byte MSS, and read-only HTTPS
requests through a fixed-destination local tunnel. See [TCP recovery](TCP_RECOVERY.md).
Checkpoint: kernel MSS and TLS fixtures, IPv4/IPv6, pin rejection, no launch
replay, host/binding/clipboard regressions, target builds and notarized prerelease.
Delivered through mynix; the user confirmed all three desktop instances online.
Log inspection found the open control-connection issue below; online status does
not establish that every background endpoint refresh succeeds.

### TLS fallback blocked by a pending inbound handshake (2026-09-17; fixed 2026-09-26)

- [x] Separate bounded, unauthenticated TLS handshake candidates from the global
  binding busy state, while retaining certificate checks, authorization and
  authenticated-operation concurrency limits.
- [x] Add an integration regression where the original connection remains open
  at the server while the client retries with smaller TCP segments. Verify the
  retry completes endpoint refresh without waiting for the original timeout.
- [ ] Log connection direction, handshake stage and admission rejection reason
  so failed refreshes can be distinguished from successful fallback cleanup.

Reason: the client retries after 1.5 seconds, but an unacknowledged original
handshake can keep the server's `m_Link` occupied until its 10-second timeout.
`createListener()` rejects the retry while `busy()` remains true. Live socket
observations and an isolated production `PeerManager` test reproduced retry
rejection followed by successful TLS after the original connection timed out.
Periodic endpoint refresh can therefore fail even while existing streams and
display heartbeats remain active. A remote-close warning followed by successful
TLS and an endpoint response is a separate, successful fallback sequence.

Next checkpoint: implement bounded handshake admission, run the integrated
fallback regression and binding suite, then verify both connection directions
after user activation. The network hop responsible for larger-segment loss is
not identified. Merging 0.3.14 to main retains this known issue; no new release
is requested for this merge.

## 0.3.13 connection recovery (2026-09-17)

Reason: prioritize verified remote workspace availability over failed local
monitor restoration. Fix macOS mirror-mode recovery and KDE stale lease state;
retain recovery evidence and record local layout deviations. Deliver macOS
arm64 and Linux x86_64 through mynix for user verification on mm4, pk4 and wmn,
including iPad handoff. See [session display recovery](SESSION_DISPLAY.md).
Checkpoint: helper fault regressions, host/binding suites, platform builds and
notarized prerelease. Activation and physical streaming remain user-owned.

## 0.3.12 macOS adaptive resolution delivery (2026-09-17)

Reason: client resolution requests reach the host but disconnected connector
placeholders prevent saving the original layout. Package the existing topology
fix for the macOS arm64 and NixOS x86_64 prerelease targets and update mynix.
Checkpoint: isolated topology sanitizer regression, both target builds, notarized
macOS assets and pinned mynix system builds. After user activation, verify client
window resizing and original display restoration on disconnect.

## macOS offline investigation (2026-09-17)

The display snapshot now skips disconnected, inactive WindowServer entries
without UUIDs, while retaining identifiable disabled displays and rejecting
unidentifiable online/active displays. Production-adapter sanitizer regressions
and a read-only live snapshot passed. This fix is not in the 0.3.11 package.

Open: a deployed host stopped its streaming child while its UI still reported
sharing enabled. Repeated samples found AppKit status-button right-mouse tracking.
Confirm the input/termination sequence and recovery behavior before claiming the
offline problem resolved. No deployed process was restarted during diagnosis.

## Stable desktop 0.3.5 (2026-09-15)

Reason: consolidate all desktop development branches into main for a formal
macOS and Linux release. Includes on-demand clipboard, opt-in unattended macOS
recovery, sidebar version display, session display mirroring and caret geometry,
and the isolated macOS VM validation tools. Package and automated checks do not
replace physical-device streaming, clipboard or reconnect acceptance.

## On-demand clipboard (0.3.2 prerelease, 2026-09-15)

Reason: retain immediate text sharing while avoiding speculative image/file
payload transfer. Native clipboard helpers support lazy images and file/folder
URLs over the existing authenticated desktop session. Includes Wayland
background data-control, AppKit item providers, chunked reads, temporary download
limits, cancellation on new copies/disconnect, and old-peer text fallback.

The previously deferred PNG and file-copy work is now implemented for desktop
prerelease testing. Download starts on data access, which history tools can also
trigger. Large-file native paste timeouts and real Finder/Dolphin/streaming UX
remain manual acceptance after the user's pk4/mm4 activation. See
[clipboard behavior, limits and checks](CLIPBOARD.md).

## 0.3.1 preview: unattended macOS recovery

- Added an opt-in Sharing setting backed by Apple's SMAppService and a signed,
  bundled recovery helper. Ordinary DMG installs can request approval in System
  Settings without Nix or a separate installer.
- The system job drops privileges to the console user before reading preferences
  or starting the existing GUI agent. It checks every 30 seconds, respects disabled
  login items, and leaves running/Finder-launched instances alone.
- Show pending approval, missing setup, delayed heartbeat and recovery errors.
  Explicit quit offers pause-and-quit; reopening resumes checks. Disabling the
  setting removes recovery registration while retaining ordinary login startup.
- Desktop login, FileVault unlock and automatic-login configuration remain macOS
  responsibilities. A running process is not proof of working capture or input.
- Release scope: macOS arm64 package and NixOS x86_64 client only. Administrator
  approval, post-update Setup Assistant and unattended reboot acceptance are manual.
## Sidebar version display (2026-09-15)

Restore the application version below Settings in the desktop sidebar so users
can identify the running build without opening Settings. Use the existing
runtime version and theme colors. Packaging and release are deferred.

## Device-first desktop UI (0.2.7, 2026-09-14)

Reason: make connecting the primary action and remove connection parameters from
application preferences. Devices use cards with bundled OS marks. Each device
owns its basic picture/audio/input controls and deeper streaming settings.
Application settings contain appearance, language and the optional usage display.
System color scheme and accent are followed independently, with manual light/dark
and blue/green/purple/orange overrides. Accent contrast is adjusted for legibility.

The sidebar keeps navigation, the current session, optional measured transfer
usage and local sharing/settings. Counters cover client media/control socket I/O
and clipboard payloads, not carrier billing or all host-process traffic. Adaptive
and manual continuations preserve the session baseline. OS metadata is advertised
by the bundled host so saved devices gain marks after upgrading and polling,
without pairing again; unsupported hosts retain a generic computer mark.

Checkpoint: isolated QML navigation/screenshots, theme and setting isolation,
loopback socket accounting, clipboard/binding regression, macOS distribution and
Linux x86_64 Nix build. Next action: user activation and real cross-platform theme,
connection and hotspot-usage acceptance. See RELEASE_0.2.7.md.

## Background Mac clipboard observation (0.2.6, 2026-09-14)

Reason: Qt Cocoa clipboard dataChanged only observes external copies on app
activation. A background host cached its previous snapshot, so client-to-host
worked while host-to-client copies were missed. Poll NSPasteboard changeCount
on authenticated clipboard requests and refresh Qt MIME data only when changed.
The counter does not fetch contents; idle text is not repeatedly encoded.

Checkpoint: native external-process writes to a unique named pasteboard without
activation; authenticated clipboard integration with Qt notifications suppressed,
including image/file-to-text recovery. Existing tests had only in-process Qt
copies and did not establish native background correctness.
Next action: manual two-sided activation and reverse copy from ordinary Mac apps
while DeskPort stays in the background. Media/HiDPI behavior is unchanged.

## Clipboard content skip recovery (0.2.5, 2026-09-14)

Reason: unsupported clipboard notices could remain indefinitely, while native
clipboard read failures returned before processing replies or polling the host.
Skip the affected copy without starving the authenticated channel. Expire content
notices after five seconds; later text copies continue in both directions.
Do not treat transport/authentication failures as successful sharing.

Checkpoint: isolated SDL/Qt clipboard recovery after non-text, image and file
offers, notice expiry, bidirectional text, plus existing Unicode/size/order tests.
Next action: user activation followed by native image/file-to-text copying on
both desktops. The visual workspace sizing from 0.2.4 is unchanged.

## Fractional-scale workspace preview (0.2.4, 2026-09-14)

Reason: a 150% client received a pixel-matched 2x Mac desktop, making remote UI
one third larger than the equivalent local logical geometry. Match logical size
by transmitting a supersampled 2x desktop on clients between 1x and 2x. Keep the
full capture raster through encoding; do not introduce host-side downscaling.
Recompute the negotiated size when restoring saved window geometry.

Checkpoint: fractional UI-size/detail invariants, isolated binding/resize and
window-state checks, macOS signing/notarization and NixOS x86_64 build. User
activation on both ends precedes live small-text, resize/reconnect and input
acceptance. Full pixel alignment at 1x/2x is preserved; fractional downsampling
is not pixel-identical and needs visual acceptance. Existing maximum dimensions
and minimum logical desktop still apply. Above 2x, preserve raster detail.

Next action: compare small text at 150% after manual activation. Deferred:
physical-size heuristics, user text-size preference, live output-metadata refresh
and host-side resampling/transport-size separation. See RELEASE_0.2.4.md.

## Installable desktop release (0.2.0, 2026-09-13)

Reason: distribute packages that other users can install without a development
checkout. Ship Developer ID signed and Apple-notarized macOS ZIP/DMG, plus Linux
x86_64 DEB, RPM, pacman, AppImage and client-only Flatpak bundles. Keep the Nix
package and independent upstream settings/services.

Packaging includes a private Qt/media runtime and separately bundled Sunshine.
AppImage login startup retains the original executable path. Flatpak uses its
session-bus name for single-instance activation so sandbox PID reuse after a crash
does not block reopening. Linux device permissions still require host setup.

Checkpoint: notarization/stapling/Gatekeeper, clean distribution installs, isolated
packaged QML/CLI/host checks, Flatpak activation/crash recovery and the Linux Nix
build. Hardware decoding, live desktop input and long-session acceptance remain
separate. See RELEASE_0.2.0.md and LINUX_PACKAGES.md for the tested matrix.

Deferred by request: source integration of Sunshine and related components,
mobile/store client implementation and official repository/Flathub submission.
Next action: validate fresh-user installation and a real streaming session on a
second Mac and the supported Linux desktops, recording permission/setup failures.

## Automatic connection ports (0.1.14, 2026-09-13)

Reason: a headless host selected a different streaming port group, while its
approved client kept polling the old port and displayed the device as offline.
Do not require local access, removal of trust, or closing an active remote desktop
to recover a remembered endpoint.

Implemented: production clients retry one saved peer every ten seconds (first
attempt after one second) over the existing binding endpoint. A five-second TLS
probe pins the binding certificate and verifies the saved streaming certificate
and host ID before atomically updating a changed streaming port. Existing device
polling then discovers the new endpoint. Local aliases, trust and active media /
clipboard channels are preserved; unresponsive peers do not replace UI status.
Concurrent local edits or revocation invalidate an in-flight reply.

Checkpoint: isolated changed-port / wrong-identity / revoked-peer tests and Linux
Nix build. Live two-sided upgrade, changed-port recovery while an opposite-direction
session is active, and headless restart acceptance remain separate deployment checks.
Settings exposes one connection port, default 48991, also used when adding a
name without a port. Changing it binds the new listener before persisting it,
retains up to eight previous listeners across restarts, and advertises the new
entry port to approved peers. An occupied port leaves the existing listener intact.
Video/audio port groups remain automatic; manual overrides are advanced controls.
Both peers must support endpoint refresh. Previous ports must remain reachable
through any firewall. Legacy Sunshine entries still require explicit updates.

## Native macOS idle capture (0.1.13, 2026-09-12)

Reason: static-frame suppression still scanned entire pixel buffers on the CPU.
BGRA/NV12 now uses ScreenCaptureKit frame status and retained-surface idle refresh.
The P010 compatibility path also stops comparing pixels. Preserve the hardware
encoder buffer path and provide lifecycle ticks independent of screen updates.

Checkpoint: synthetic callback/lifetime tests, full host and Linux Nix builds,
stable signed package, then live static/video CPU comparison and reconnect/resize.
Actual resource savings and native visual acceptance require a running new build.
See RELEASE_0.1.13.md for the 10-bit compatibility limitation.

## UI refresh — 2026-09-12

## Smart desktop streaming (2026-09-12, 0.1.12)

Reason: video playback exposed presentation drops and bursts of unrecoverable
network frames; static desktop capture also repeatedly encoded unchanged content.

Implemented exact Mac capture deduplication, a five-fps idle encode target,
FEC-driven output-cadence reduction with hysteresis and gradual recovery, a
resolution-aware client startup bandwidth ceiling, pacing controls/defaults,
and correctly named periodic presentation diagnostics. No resolution cycling,
encoder recreation, or live bitrate reconfiguration is introduced. Host savings
are independently switchable and Mac-specific.

Validation: Linux Nix build and Mac viewer/host compilation passed; both platforms
passed 15 isolated UI checks and seven service checks. Policy and native synthetic
CoreVideo tests passed. Signed ZIP/DMG packaging verifies all Mach-O signatures
and excludes Nix/Homebrew runtime dependencies. Live playback/traffic/power
acceptance remains pending; no running installation was replaced.

See RELEASE_0.1.12.md for behavior, limitations and acceptance steps.
Next action: compare static/editor/video sessions after both devices are updated.
Checkpoint: ten minutes each at fixed window dimensions; inspect intentional idle
FPS separately from packet loss and presentation drops, then repeat the existing
resize/reconnect and two-hour stability acceptance.


User-approved scope: retain Qt and implement the six UI review recommendations.
Version 0.1.11 adds identity-aware session recall, an inline session header,
persistent favorite ordering and compact/card views, separate host service /
permission / trust status, client quality presets and host-specific audio controls,
and a shared light/dark/system theme. Session navigation must retain the active
StreamSegue when switching among Devices, Sharing and Settings.

Release checks: isolated page/lifecycle tests, 50 simulated navigation cycles,
synthetic narrow-window screenshots, Linux Nix build and signed macOS package.
Native input, compositor focus and live hide/recall remain user acceptance checks.
Next backlog: per-device connection preferences, actionable network diagnostics,
and actual sustained-session resource measurements.

# DeskPort roadmap

## Nix-managed macOS build dependencies (2026-09-12)

Reason: prefer Nix-managed tools without compiling large third-party dependencies.

- Add an Apple Silicon devShell using cached Qt and Sunshine build dependencies,
  while keeping Apple's SDK/compiler/signing and the pinned media prebuilts.
- Separate Nix build/output directories; support split Qt tools/QML/plugins and
  writable staging of store files. Resolve miniupnpc through pkg-config's full
  library path instead of silently falling back to Homebrew.
- Validation: native viewer/host builds, 14 isolated UI checks, signed bundle and
  ZIP checks, cleared-environment CLI and packaged QML/TLS loading (both
  Secure Transport and OpenSSL); Linux `nix build` and mynix nix-darwin build. Published
  0.1.10 assets and installed applications remain unchanged; stream acceptance
  is still the user's next checkpoint.

## Recovery and diagnostic follow-up (2026-09-12, 0.1.10)

Reason: review of current client/host logs and the retained 0.1.6 crash report.

- Keep each executing Session under C++ ownership until both exec() and queued
  transport cleanup finish. Page destruction/garbage collection must not free a
  Session still referenced by audio cleanup. This closes a lifetime hazard;
  the historical heap corruption is not proven resolved without reproduction.
- Bound adaptive /resume requests to 15 seconds, retain normal launch timeouts,
  and make late QML teardown callbacks tolerate a destroyed page context.
- Answer headless --help/--version with Qt's offscreen platform on Linux.
- Keep VAAPI eligible on native Wayland instead of preferring unavailable VDPAU.
  Retain the conservative Gallium RFI workaround; do not claim a driver-version
  cutoff or a measured latency/power improvement.
- Repair macOS test linkage for the native status menu and translate the new
  startup-management and resize-failure messages in seven languages.
- Add submitted/returned PTS and encoder name to host IDR failures. Current
  logs correlate these with client loss/queue overflow; synthetic VideoToolbox
  requests succeeded, so the real-session cause remains open. The old message
  alone does not prove SPS/VPS injection was suppressed.

Validation: Linux Nix build, headless CLI, UI/service/state tests and macOS
UI/service/state tests passed during development. Lifetime tests cover page
collection and cleanup both before and after exec returns. Final release
verification is recorded in RELEASE_0.1.10.md.

Next action: native two-device acceptance after switching to 0.1.10.
Checkpoint: 30 alternating resizes, 20 disconnect/reconnect cycles, then a
2-hour session. Check frame recovery, no stuck input, actual decoder selection,
and whether any heap/IDR errors recur. Do not erase existing diagnostic logs.

## Shorter tray menu (2026-09-12)

The tray menu's restart and quit entries dropped the application name; inside
DeskPort's own menu it said nothing.

## Tray left click moves the window (2026-09-12)

Reason: the tray had two window entries ("Open device list" and "Return to
remote desktop") and no way to put the window away again, while the left click
duplicated one of them.

The left button now toggles: it shows the remote window during a session and the
device list otherwise, and hides whichever is up. A new `DeskPortToggleWindow`
event makes that decision on the session's own thread, where the SDL window's
visibility is known. The right button owns the menu, reduced to opening the
device list, disconnecting, restarting and quitting; stopping sharing lives in
the device list with the other sharing controls.

macOS needed its own path: a menu attached to a status item is opened by either
mouse button and suppresses the button action, so the menu is popped up as a
native `NSMenu` from the `Context` activation and no Qt menu is attached there.

Validation: a Nix build and the service and UI suites on pk4, and a tray toggle
added to the 30-cycle retained-window transition test. Live clicks on both
desktops remain a user acceptance check.

## Active-session control center (2026-09-11)

Follow-up: the user reported that the first tray action did nothing while viewer
recall worked. An added regression reproduced a null Qt window passed into
`Session::exec()` from the deferred StreamSegue Loader. Use the existing root
`window` context instead of the attached `Window.window` property; assert the
session receives the actual root window, not just that its page was loaded.

Deployment: the macOS development update was installed and restarted with the
original signing identity. Strict signatures, bundle dependency checks and the
running host HTTP endpoint passed. Aqua-session packaging with the full build
PATH resolved the automation signing failure; `docs/MACOS_PACKAGE.md` records
the reproducible route and old-process shutdown pitfall. Signing now has an
early probe, verified to stop before building in the failing session. Linux was
also deployed; live control-center/recall acceptance remains with the user.

Adaptive continuation fix: the stream page for an adaptive resize was created
in the context of the page it replaced. Destroying that page aborted the new
page's Loader ("Object or context destroyed during incubation") and left the
client on "Adjusting resolution". StackView now creates the continuation and
quit pages in its own context. A regression test emits readyForDeletion after
exec() returns, as real sessions do, and requires the continuation to receive
the root window.

Deployment correction: an interim macOS install was ad-hoc signed, and the
independent Sunshine agent was removed while diagnosing an offline peer. Both
were reverted. The package was rebuilt in the Aqua session with the stable
identity, strictly verified and installed. That interim backup was removed in the
2026-09-11 `dist` cleanup; the current rollback bundle is
`dist/backup-20260911-225113/DeskPort.app` (0.1.1). The tray recall item now uses the
translated "Return to remote desktop" label.

Reason: a retained remote window had no discoverable route back to the device
list. Closing it hid the viewer, while tray activation recalled the same viewer,
leaving sharing and device management inaccessible until disconnect.

The viewer close action now releases remote input, hides the SDL window and opens
the device list without ending the session. The control center shows an explicit
active-session banner; its action or a device-card click recalls the retained
remote window instead of starting a second connection. The tray separates
**Open device list** from **Return to remote desktop**, and a second application
launch opens the device list. An explicit disconnect remains separate.

Validation: 13 isolated Linux UI checks cover a control center stacked above an
active session and cleanup after disconnection; 30 retained-window transition
cycles cover close, device-list hide, recall and explicit disconnect. A Linux
Nix build passed. Native Wayland focus placement and live input release/recall
remain real-session acceptance checks.

## Input defaults corrected (2026-09-11)

Reason: real use on the KDE Wayland client showed that retained 0.1.1 settings
left system-key capture off and clipboard sharing unset, so Super+Space stayed
local and no clipboard channel was created. DeskPort now enables system shortcut
capture and plain-text clipboard sharing for new profiles and performs a one-time
version-3 settings migration for existing profiles. Users can still disable
either option after migration. Changes take effect on the next connection.

### Editable remote endpoints (2026-09-11)

Reason: incoming bindings could retain a proxy IP and overwrite repaired host
addresses at startup. Bindings now exchange a separate DNS hostname, prefer
entered or previously saved DNS names, and use numeric addresses only when no
DNS name is available. Certificate identity remains pinned.

Saved access now includes an editor for the local device alias, domain/IP,
host port and binding port. Changes persist atomically, update the host list,
and preserve trust credentials. Invalid input and failed writes leave the old
record intact. Explicit endpoints are polled before cached discovery addresses.
Validation: 13 isolated macOS binding checks and seven-language editor coverage
passed. Native editor acceptance remains pending.

### Reconnect and application recall (2026-09-11)

- Defer stream execution until asynchronous QML Loader incubation completes.
  Replacing a page from its nested session loop previously destroyed the active
  incubator and could strand the retained window on Adjusting resolution.
- Normal GUI and streaming launches share a per-configuration process lock and
  local activation socket. Repeated launches request recall instead of creating
  another GUI or loading host state. CLI list/pair/quit commands remain separate.
- Tray Open and tray activation use the same recall action. Active streams and
  resize transitions raise their existing SDL window on its owning thread;
  idle clients raise the existing QML root window. Linux keeps the Qt event loop
  responsive while the SDL worker runs.
- Validation: 12 Linux UI checks, including nested-loop continuation and repeated
  activation/lock release, passed; the Linux package built successfully.
  Installed on the target Wayland client with persistent package rooting and
  matching command, menu and autostart entries. A real adaptive resize resumed
  streaming and decoded video after the retained-window handoff. Two additional
  launches exited successfully while the original session process remained.
  Invoking the installed tray's Open action twice also retained that one process.
  Cross-workspace focus behavior and extended resize stability remain acceptance
  checks; this does not claim a long-duration streaming soak.

Status: v0.1.0, first release, 2026-09-10. Unfinished capabilities and native
acceptance limits are listed below. See [release notes](RELEASE_0.1.0.md).

## Product goal

Keep a remote desktop ready in the background. Recall it on the current local
workspace with one action, work normally, then tuck it away without reconnecting.
The first target is a KDE Wayland / AMD Linux client and a macOS Sunshine host.
Windows and macOS clients are intended follow-ups; they are not yet validated.

Product direction (2026-09-09): a single application with both viewer and optional
host roles, one explicit mutual-pairing flow and a shared device list. See
[the architecture](ARCHITECTURE.md) for component, permission and delivery boundaries.
The macOS preview bundles all three components and manages incoming PIN pairing.
The mutual-binding preview supports one recipient approval; reciprocal streaming
still requires manual acceptance.

## Binding delivery correction (2026-09-10)

Binding requests now distinguish connecting from recipient acknowledgment. The
actual approval QML component is covered by an isolated request/cancel test.
QML caches are keyed by resource content so reproducible builds cannot reuse
an older interface with identical resource timestamps. Discovery advertises the computer hostname instead of a fixed product name.
Validated on two computers: request delivery, recipient approval, saved bindings
in both directions, and host availability after restart. Reciprocal streaming
remains a manual acceptance gate.

## Implemented in the bootstrap

- Moonlight Qt v6.1.0 source and upstream Git ancestry.
- DeskPort executable, application/configuration identity and Linux desktop entry.
- Windowed, absolute-pointer defaults; mute on focus loss; background gamepad,
  game optimization and Discord presence disabled by default.
- Upstream update checks disabled for the independent development version.
- Pinned Nix build and development shell for Linux.

## macOS all-in-one preview (2026-09-09)

- Native Qt viewer plus original signed Sunshine bundle in one application.
- Native virtual-display helper; no BetterDisplay runtime dependency.
- Existing mirror membership is preserved; capture targets the active mirror source.
- Sharing page: start/stop, fixed HiDPI presets, permission shortcuts, incoming PIN.
- Private host state/certificates, separate ports, certificate-pinned local API.
- Tray recall while hosting and an optional user-login launcher.
- Monochrome tray artwork: native macOS template rendering and light/dark
  palette variants on other desktops (2026-09-10).
- Packaging script, signature checks, build-path dependency checks and host self-test.

Validated: native macOS GUI startup, bundled-component startup/cleanup, isolated
Linux CLI startup, and creation of all three fixed HiDPI presets. Intermediate
application bundles are excluded from Spotlight to avoid duplicate app entries.

Pending acceptance: complete OS consent, actual incoming stream and input,
reverse-platform hosting, reboot behavior and public distribution signing. A display
helper command acknowledgment is not proof of a changed physical display mode.
Live mode switching did not pass the first physical-mode check and remains pending. See MACOS_PACKAGE.md.

## Host lifecycle reliability (2026-09-10)

The macOS development preview now initializes host authentication asynchronously
and stops its child processes without blocking the UI. Unexpected host/display
exits clean up the other component and allow retry. Stop cancels pending startup;
late pairing replies cannot overwrite a newer session's status. The pairing button
is disabled during startup and cleanup. A running process is explicitly not a
claim that capture permissions or remote input have been verified.

Native regression harness: `python3 scripts/test-host-lifecycle.py` uses temporary
state and fake child processes, without display changes or input. Port coexistence
checks additionally use loopback-only socket reservations.
Existing separately installed Sunshine services are not modified or stopped.
Eight native lifecycle scenarios passed, including display failure while hosting
and authentication timeout. Simultaneous process error/exit notifications preserve
the original failure instead of replacing it with the cleanup process's exit.

Validation on 2026-09-10 also passed native macOS packaging/signature and linked
library checks, packaged CLI startup, and a Linux Nix build plus isolated CLI smoke
check. These checks do not establish real capture, input or permission readiness.

Next acceptance: validate the new package's permissions and incoming stream in a
user-approved test window. Keep the existing remote-access path available throughout.

## Coexistence with native services (2026-09-10)

User requirement: DeskPort and independently installed Sunshine or similar services
must not reconfigure or stop each other. Host startup now reserves the complete
TCP/UDP port family, selects an available DeskPort group, and remembers its base.
All pairing/UI/self-test endpoints use that base. A per-state-directory instance
lock prevents competing DeskPort launches from overwriting host state. Exhaustion
fails before creating a display; the native Sunshine port family is never selected.
Manual viewer entries default to DeskPort; explicit and saved ports are preserved.

Regression coverage includes each TCP/UDP family member being occupied, port
exhaustion/retry, a competing instance, chosen-port configuration, and explicit
native-Sunshine/manual IPv6 addresses. The instance-lock test also ages the lock file while its owner is still running.
All 21 scenarios passed (23 QtTest checks including setup/cleanup), followed by
native macOS packaging/signature/dependency checks, packaged CLI startup, and a
Linux Nix build with isolated CLI smoke checks. Full live streaming coexistence
still needs user-assisted acceptance while retaining the existing remote-access service.

## macOS privacy identity repair (2026-09-10)

Local TCC diagnostics attributed bundled-host capture to the outer DeskPort app,
but rejected the saved grant because an ad-hoc update changed its code hash.
Packaging now rejects accidental ad-hoc builds unless explicitly opted in. Local
installed updates use a stable Apple Development identity. The outer application
also declares the audio-input entitlement and microphone purpose string required
for the bundled host's audio request. Switching from the old ad-hoc identity needs
one user-assisted DeskPort reauthorization. Packaging also refreshes subproject
metadata and checks the final usage description and audio entitlement to prevent
stale incremental-build plists. The signed macOS package passed signature and
dependency verification; the Linux Nix build and isolated CLI smoke check passed.
Capture, audio and future-update grant
retention still need live acceptance; native Sunshine permissions remain untouched.

## TODO: first-run permissions and device identity (2026-09-10)

- [ ] Add a macOS onboarding guide covering Screen & System Audio Recording,
  Accessibility (keyboard/mouse control), and Microphone/audio input when required.
  Explain why each is needed and show actual readiness separately from pairing.
  Open the corresponding System Settings page and provide a draggable reference
  to the installed outer DeskPort.app, plus Reveal in Finder as a fallback. Verify
  which settings panes accept dropping an app; do not promise unsupported OS UI.
  Permission grants remain explicit user actions. Explain relaunch requirements
  and stale grants after migrating from ad-hoc signing; preserve native Sunshine.
- [ ] Keep the host identity stable before first pairing and across restarts/updates.
  Observed repeated missing host state before pairing and multiple saved UUIDs
  pointing to the same local endpoint. After pairing, the persisted UUID matches
  serverinfo. Verify ten restarts, including unpaired starts, without new entries.
- [x] Filter local loopback/interface addresses from discovery and the displayed
  saved-host list, preserving saved records and pairing data. Ignore local mDNS
  before querying or persisting new identities; do not match by name or subnet.
- [ ] Distinguish same-name devices by address and status; offer explicit removal of
  stale entries without deleting pairing data for another device. Never merge
  identities solely by display name or host address, since services can coexist.

Live checkpoint: user confirmed picture output; host logs show an active HEVC
session. Accessibility previously failed its saved code requirement; user reports
reauthorizing it, but pointer movement and click delivery still need acceptance.

## Mutual binding preview (2026-09-10)

The next preview implements IP/domain initiation plus one recipient confirmation,
TLS certificate exchange, both-direction local trust provisioning and automatic
remote device entries. Linux hosting is included with KWin/portal capture of the
existing desktop. See [the binding protocol and acceptance checks](PEER_BINDING.md).
PIN pairing remains available for legacy clients. Interrupted grants are visible;
local revocation is available, while coordinated remote revocation remains pending.
Live both-direction picture/input acceptance is still required.

## Next action: unified desktop experience (2026-09-10)

See [the desktop interaction guide](DESKTOP_UX.md) for the implemented flow and
acceptance checks.

User priority after the mutual-binding milestone: unify the Mac/Linux first-run
experience, permissions, binding, device list and settings. The new shared QML
shell groups Devices, Sharing and Settings, with a revisitable, optional setup
guide. Device cards show name, endpoint and connection state. Mutual binding is
the default add-device flow; legacy PIN pairing remains an explicit secondary path.
Common streaming settings are grouped separately from advanced compatibility
controls. Opening settings must preserve custom values without silently applying
presets. Permissions are checked separately from binding and host process state.

macOS reports screen, Accessibility and audio-input status, links to the matching
settings pane and provides an app-file drag plus Reveal in Finder. Linux explains
capture consent and checks input-device access without claiming that a process
start proves screen capture. Manual checks still required: OS pane drag acceptance,
keyboard navigation, both-platform layout, and reciprocal picture/input after the
refresh. Keep the independent remote-access service intact throughout.

## Following: persistent-session prototype

Language settings added on 2026-09-10: visible language selection with system
default, local persistence and live retranslation; primary desktop-page coverage
for English, Simplified/Traditional Chinese, Japanese, Korean, German, French and
Spanish. Existing additional catalogs remain selectable with English fallback.
See `docs/DESKTOP_UX.md` for translation maintenance and validation.

1. Diagnose input with the unchanged Moonlight/Sunshine path, including host
   permissions and interaction with a software KVM. Do not hide an input failure
   behind a new application shell.
2. Separate session lifetime from window visibility: add explicit show, hide,
   disconnect and quit operations. Do not treat a frozen frame as a live session.
3. Prototype one-action recall on the current KDE workspace; return held keys,
   buttons and input capture when the window is hidden or loses focus.
4. Measure recall time, fresh-frame time and input response separately; measure
   background bandwidth and CPU/GPU use before choosing an idle policy.

Acceptance: 50 hide/show cycles on the same healthy streaming session, no new
stream launch per recall, no stuck input, and a local escape that remains usable.
Window appearance under 200 ms is a target to measure, not a current result.
Initial connection, sleeping hosts and network recovery are separate cases.

Client sleep and termination (2026-09-11): a Linux client holds a logind delay
inhibitor and ends the stream before suspend/hibernate, without honoring "quit
app after streaming". SIGTERM/SIGINT now stop the session and host processes;
SDL's handlers previously swallowed them until systemd sent SIGKILL. Stream pages
connect session signals once, so a control-center round trip no longer repeats
termination handling. Verified: SIGTERM exit in an isolated profile. Pending:
real suspend/hibernate and resume on the KDE client; automatic reconnection after
resume is not implemented.

Host resume hang (2026-09-11): Sunshine's macOS `dummy_img()` waited forever for
a first frame. A resume racing the idle-mode restore of the virtual display left
the single HTTPS thread blocked and the host unreachable until Sunshine was
restarted. `host/macos/patches/sunshine-capture-timeout.patch` bounds the wait to
three 2-second capture restarts, then fails encoder validation so /resume returns
an error. The streaming `capture()` loop still waits without a timeout (upstream
FIXME). Verified: patched host builds and packages; live race not yet reproduced.

Adaptive resize latency (2026-09-11): a measured resize took about 7.5 s from
restart to the resume request. Adaptive continuations now skip the 3.5 s launch
warning toast wait and the 1.5 s segue delay before connecting. Remaining work:
reuse the chosen decoder instead of re-probing, show the scaled previous frame
during transition, and eventually change resolution in-band at an IDR without
restarting the stream (as RDP 8.1 dynamic resolution does). Rate-limit layout
updates and wait for each transition to finish before sending another.

Adaptive resize stage baseline (2026-10-13): 147 pk4 → mm4 resizes from client
diagnostics took a median 500 ms quiet interval plus 2970 ms (p90 6851 ms) from
stopping the old stream to the first new frame. Median stages: stop 268 ms,
resume-time re-settle 521 ms, display mode change 534 ms, decoder probe 103 ms,
input-handler setup 643 ms, /resume 460 ms, RTSP/decoder setup 215 ms and first
frame 262 ms. The re-settle repeated a quiet interval the size had already
passed; it now commits an unchanged size immediately. SDL joystick enumeration
took 430–490 ms on pk4 with no gamepads attached; the subsystems now stay
initialized across a retained-window continuation and present gamepads are
re-announced. A live pk4 → mm4 A/B run (30 resizes per build, KWin-scripted
window sizes) measured stop-to-first-frame at a median 2626 ms (p90 2774) for
0.7.1 and 1646 ms (p90 1738) after the change; re-settle fell from 523 to 8 ms
and input setup from 510 to 24 ms, with other stages unchanged.

Reconnect transitions now paint a small blurred picture of the previous stream
behind the progress indicator through both the stopping and connecting phases,
instead of a flat background. The renderer converts one frame every 3 s, and a
fresh one when a resize begins, on a worker thread joined before the decoder is
released. First connections, HDR streams and frames that cannot be transferred
keep the flat background. A 15-resize run showed no latency change (1671 ms
median). Screenshots confirmed the picture in both phases; the gap between the
loading surface and the first decoded frame was not captured.

Windows (2026-10-14): the first build captured the picture but never showed it.
The stream's Direct3D 11 flip-model swap chain kept its last frame over the
software renderer's GDI output, so the old picture stayed frozen until the new
stream started. The transition window now uses SDL's Direct3D 11 renderer on
Windows, with software rendering as the fallback. On the wmn test machine, a
normal tray session to mm1 went sharp → blurred → sharp in all 14 resizes
(six direct sizes, four 20-step drag emulations, two maximize/restore pairs),
sampled about every 20 ms, with no black or flat frame and with every stream
recreated. Native modal border dragging was not injected. Capture outcomes are
now recorded as `snapshot-*` resize stages in client diagnostics.

Remaining candidates: overlapping the mode change with stream teardown (needs
host capture-restart evidence), reusing the probed decoder, and in-band IDR
reconfiguration.

## Then: daily development

### Dedicated display, client-size resolution and physical mirroring (updated 2026-09-10)

The host is intended to stay at home as an always-on desktop-development server.
Add a dedicated remote display whose mode follows the client's content area,
similar to a VM's automatic guest-display resizing. The macOS development package
includes native virtual-display creation and client-window adaptation. The Sharing
page shows the actual virtual-display size; Picture settings enable adaptation by
default for mutually bound, capable Mac hosts. No BetterDisplay runtime dependency
is needed for this dedicated workspace. Physical-display mirroring, making the
virtual workspace the main display, and restoring full physical topology remain
planned; the current mode preserves the existing physical capture source.

The first implementation coalesces changes for 900 ms after dragging, negotiates
aligned 640×360–3840×2160 pixel sizes over the pinned binding TLS connection, and
resumes video after stopping the old stream. This is a brief reconnection, not
seamless encoder reconfiguration. Window position/size, remote applications and the
display-control lease survive the transition. Only one approved controller can
resize; it sends heartbeats, and loss of its connection releases ownership.
After a 10-second grace period the display returns to its configured idle size.
Minimizing preserves the stream and display. Stopping sharing removes the helper.

Client drawable pixels are distinct from logical window size. Since 2026-09-11 the
stream matches drawable pixels 1:1; clients at 150% or more use 2× host scaling,
others 1×. A 2× workspace keeps a 960×540 logical minimum (1920×1080 pixels)
because native testing found advertised small HiDPI modes that WindowServer
rejects. The 2026-09-10 1× policy divided by client scale and upscaled visibly
soft text on a 150% client. Unsupported or
unbound hosts fall back to the saved fixed streaming resolution. Linux hosting
continues to capture its existing desktop.

Validation: isolated binding tests cover certificate pinning, unapproved clients,
exclusive ownership and transfer; host tests cover rejected/timed-out resize without
stopping sharing; QML tests exercise actual continuation-page activation. Native
window-drag, image quality and pointer acceptance are tracked separately from build
and protocol checks. See [adaptive display](ADAPTIVE_DISPLAY.md).

The requested session mode makes the virtual display the main display and mirrors
it onto the other attached displays. A full disconnect restores the previous
physical main display and display layout. This supersedes the earlier assumption
that physical displays remain independently arranged during a remote session.

- First prove persistent virtual-display creation and capture on macOS, including
  host reboot and operation without a physical monitor. Bundle the implementation
  and its permission/setup flow; do not require a separate display utility.
- Treat window size, client device-pixel ratio, host HiDPI scale and encoded frame
  size separately. A larger window should expose more workspace without blurry text.
- Coalesce resize events after dragging stops, choose supported/aligned modes and
  retain the last working mode if the host rejects a requested size.
- Verify how capture and encoder/decoder reconfiguration behave during an active
  session. Initial session-resolution selection is not proof of seamless live resize.
- Keep the virtual display alive while the window is hidden so applications stay
  placed. Define ownership before allowing two clients to change one display.
- Snapshot physical main-display identity, modes, scaling, positions and mirror
  groups before changing the topology. Make the virtual display the mirror source;
  account for physical aspect-ratio/mode limits without silently changing the
  requested remote workspace size. Surface unsupported mirroring explicitly.
- Distinguish window hide/minimize and transient transport loss from full session
  termination. Preserve the display during hide/reconnect; define a bounded orphan
  timeout and provide an explicit local stop action that releases session ownership.
- On full disconnect, quit, or unrecoverable session failure, restore the original
  physical main display and layout before removing the virtual display. Persist a
  recovery record for host-process crashes; handle unplugged monitors by selecting
  an available physical fallback. With no physical display, retain a usable headless
  recovery path rather than assuming restoration is possible.
- Deliver macOS first, then assess the KDE Wayland implementation independently;
  share settings and lifecycle semantics across platforms without claiming equal
  display-control capabilities before native validation.

Checkpoint: 30 alternating window sizes, including fractional client scaling;
sharp text, correct pointer coordinates, no application relocation to physical
screens, and measured interruption time. Start with explicit size presets if live
resizing requires disruptive stream restarts.

Additional acceptance: physical displays mirror the virtual workspace while
connected; 20 full disconnect/reconnect cycles restore the original main display
and layout; hide/show and brief network loss do not switch displays; host-process
crash recovery, monitor hotplug, headless operation and competing clients preserve
local control. Validate alongside the independent remote-access service.

### Port allocation and migration (requested 2026-09-10)

- Move preview defaults and fallback port families into the IANA dynamic/private
  range (49152–65535). The current 489xx defaults are outside that range, and UDP
  49000 is reserved. Private ports are not exclusive: retain complete-family
  conflict detection and configurable endpoints.
- Coordinate host, viewer, binding-service defaults, discovery announcements,
  saved peer endpoints, packaging, documentation and mynix firewall rules in one
  migration. Existing trusted peers must remain usable without deleting bindings;
  define mixed-version compatibility and rollback before deploying both platforms.
- Advertise actual service ports. Keep firewall rules consistent with selected
  ports; a fallback must not silently produce an unreachable host. Keep the
  management UI local and avoid opening unused ports or broad fallback ranges.
- Preserve independent Sunshine/Moonlight services and their settings.

Checkpoint: fresh pairing and existing bindings work across the migration; exercise
occupied default ports, restart, rollback and mixed versions on macOS and Linux;
verify discovery, reciprocal connection and streaming through the enabled firewall.

References: [IANA port registry](https://www.iana.org/assignments/service-names-port-numbers/),
[UDP 49000 reservation](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=49000),
[RFC 7605](https://www.rfc-editor.org/rfc/rfc7605.html).

### Shared clipboard (requested 2026-09-10)

Design clarification (2026-09-11): the user requests Deskflow-style sharing.
The current `KeyComboPasteText` sends UTF-8 input events and does not synchronize
the remote clipboard. Study Deskflow's format abstraction and sequenced clipboard
messages, adapting the design to an authenticated DeskPort channel on both ends.
Do not assume its protocol can be plugged into Sunshine unchanged.
Reference: [Deskflow clipboard messages](https://deskflow.github.io/deskflow/group__protocol__clipboard.html).

- Add bidirectional clipboard sharing between the active client and host, starting
  with Unicode plain text, then PNG images. File transfer remains deferred.
- Provide a clear session setting to enable or disable sharing. Limit exchange to
  the authenticated active session; binding alone must not synchronize clipboards.
- Do not overwrite either clipboard on initial connection or replay stale contents
  after reconnect. Prevent echo loops and define ordering for simultaneous copies.
- Set payload limits and report unsupported or oversized content without silently
  truncating it. Do not log clipboard contents or retain clipboard history.
- Stop exchange on full disconnect. Validate platform clipboard permissions and
  KDE Wayland behavior independently; do not infer support from the shared UI.

Checkpoint: 100 bilingual/multiline text transfers in both directions, PNG transfers
after image support lands, simultaneous copies, disabled sharing, disconnect and
reconnect; no initial overwrite, stale replay or loops, and clear limit handling.

### Pointer-scoped keyboard capture (requested 2026-09-11)

Reason: the user wants VM-like input routing while the pointer is inside the
remote desktop, including system shortcuts rather than triggering local actions.

- [x] Extend existing SDL keyboard capture with remote-video-region entry/exit
  handling. Capture only for a visible, focused streaming window; restore local
  input on exit, focus loss, hide, disconnect and explicit release.
- [x] Preserve a documented emergency release shortcut and release held remote
  keys/buttons across transitions, including drags and modifier chords.
- [ ] Validate KDE Wayland focus behavior: pointer entry alone does not establish
  keyboard focus. If activation is unavailable, require a click and make capture
  state visible. Do not silently change the user's desktop focus policy.
- [ ] Verify Alt+Tab, Super, Super+Space and remote modifier mapping on the actual
  target. OS-reserved shortcuts are exceptions; do not promise every key can be
  intercepted. Existing SDL grab flags alone do not prove compositor acceptance.

Implementation update (2026-09-11): pointer routing and bidirectional Unicode
text exchange are implemented. PNG images remain the next clipboard phase.
Next action: native two-device acceptance. Checkpoint: 100 entry/exit cycles,
held modifiers, dragging across the edge, hide/recall and disconnect, with no stuck
remote keys or local shortcut activation while capture is effective.
Automated protocol checks do not establish compositor shortcut interception.
Reference: [SDL keyboard capture](https://wiki.libsdl.org/SDL2/SDL_SetWindowKeyboardGrab).

### Remaining daily-development work

- Explicit local/remote shortcut handling; native host IME first.
- Reconnection that preserves remote applications and returns local control.
- Fixed resolution/scale profiles, with text clarity checked on real hardware.
- Ten disconnect/reconnect trials; 100 bilingual/multiline clipboard samples;
  three two-hour development sessions before asking 3–5 developers to try it.

## Deferred

Individual remote application windows, multi-display roaming, file drag/drop,
USB redirection, local IME preedit forwarding, cloud accounts, mobile clients,
automatic updates and broad distribution packaging. Stable macOS signing is required
for the planned host deployment, rather than a cosmetic release task. Do not refactor the decoder or protocol
unless a measured blocker requires it.

### Fractional scaling and virtual-display input (2026-09-10)

- Compute the client logical workspace from drawable pixels / system scale. Use
  2× macOS backing for scaled clients, 1× otherwise, aligned to four pixels and
  bounded by 7680×4320. Keep a minimum 960×540 logical workspace for valid native
  HiDPI modes. Example: 2880×1620 at 150% becomes 3840×2160 backing / 1920×1080 logical.
- Match Qt logical output geometry to SDL native pixel modes on Wayland: SDL window
  ratios may hide fractional scaling while QScreen DPR may round 150% to 200%. Reconnect after changing
  system scale; live scale/hotplug refresh remains an acceptance follow-up.
- Build a pinned, patched macOS input backend that follows the verified virtual
  capture display. Do not change the physical main display to redirect input.
- Acceptance: verify mouse motion/clicks, fractional-scale text size, resize/resume,
  and coexistence with independent Sunshine on the installed endpoints.

### Continuous window during adaptive resize (2026-09-10)

- Retain the native client window, its placement and maximized/fullscreen state
  while the stream renegotiates. Show a translated animated loading view instead
  of destroying the window and exposing the desktop.
- Release input during the transition; discard waiting input rather than replaying
  it. Close/Escape cancels the continuation, and failed startup disposes the window.
- Wayland/Vulkan uses shared-memory loading buffers on the existing surface;
  SDL's software-renderer fallback can recreate Vulkan windows and is avoided.
- Native isolated tests exercise 30 handoffs, native-window identity, animated
  frames, compositor buffer recycling and close cleanup on macOS and KDE Wayland.
  Real stream/decoder resume and perceived continuity still require interactive
  acceptance after installation, including minimize and repeated resize.

### 1× host workspace by default (2026-09-10)

User preference: reduce the host framebuffer and transmission cost. This
supersedes the earlier automatic 2× backing policy. Adaptive clients now request
1× at their logical workspace size; sharing startup and idle restore also use 1×.
Saved idle pixel sizes and explicit legacy 2× protocol requests remain supported.
A 2880×1620 client at 150% requests 1920×1080 instead of 3840×2160, one quarter
as many pixels. Text sharpness and actual bandwidth remain native acceptance items.

Investigation of the cropped image reproduced the crop in host capture before
encoding: the mirrored virtual source exposed 1280×720 logical bounds while its
physical mirrors exposed 2560×1440. A screenshot captured the full desktop, but
both AVFoundation and a ScreenCaptureKit streaming experiment reproduced the crop.
The experiment was removed; do not claim changing capture APIs fixes this issue.
Validate the 1× mode in the user's existing mirror arrangement after installation.

Validation: 12 binding/workspace checks, 26 host lifecycle checks and the Linux
`nix build` passed.
The signed macOS package was installed with sharing enabled. CoreGraphics then
reported 2560×1440 physical and logical dimensions (1×) for the virtual source
and both physical mirrors. A fresh host-side capture at 1× showed the full
desktop. The isolated virtual-display test was stopped when WindowServer changed
mirror modes, so repeated mode switching is not recorded as passed. Repeated
client resize, input and end-to-end image acceptance remain pending.

### Remembered client workspace and desktop input (2026-09-10)

- Save each host's last stable client window, maximized/fullscreen state and 1×
  stream dimensions. Reconnect requests that size immediately. Display layout,
  scale or configured window-mode changes invalidate the saved state.
- New installations capture system shortcuts in windowed mode as well as
  fullscreen. Explicit saved choices remain unchanged; existing users with
  capture set to Never must choose Always to send Super/Windows + Space as
  Command + Space to a Mac host.
- Show a local pointer by default in desktop mode, with a setting to disable it
  when the host also draws a pointer. Focus loss releases keyboard capture and
  shows the pointer; focus return reapplies capture.
- Validation: Linux `nix build`, isolated desktop preference/state persistence
  tests, QML settings/stream-page tests, seven translated catalogs and isolated
  CLI smoke passed. Restart the installed client before native acceptance of
  shortcuts, pointer visibility and reconnect without an initial resize.

### Small video packets by default (2026-09-10)

- Default video packet size is 896 bytes (16-byte aligned), leaving headroom for
  transport and tunnel headers on constrained mobile/VPN paths. This is a video
  protocol size, not the final on-wire datagram length. Explicit custom packet
  sizes remain supported.
- Unknown routes use remote mode rather than AUTO, because upstream AUTO replaces
  the requested size with 1024/1184 on public routes. LAN and VPN detection retain
  the smaller size. Resolution, bitrate and image quality settings are unchanged.
- TCP MSS belongs to the operating-system network configuration; this video
  change alone does not repair a TLS handshake affected by a path-MTU black hole.
- Validation: Linux `nix build`, isolated CLI smoke, desktop-state and QML
  regression suites and translation checks passed. Live video packet capture
  and stability on the affected mobile link remain acceptance checks.

### Durable binding addresses and local host audio (2026-09-10)

Reason: resolved proxy addresses can become stale, and desktop audio should stay
on the computer producing it by default.

- Retain locally entered DNS names for outgoing bindings and restore names from
  older saved requests. Keep certificate pinning and separate service ports.
- Disable host audio streaming by default using Sunshine's `stream_audio` setting.
  Sound settings can opt in after restarting sharing. New viewer preferences keep
  host audio playing; explicit existing viewer choices remain intact.
- Validation: Linux package build, hostname binding/legacy recovery/key-change
  rejection, desktop preference persistence, generated host audio configuration,
  and seven translated catalogs passed. The binding suite ran 11 checks successfully;
  its macOS-only adaptive-display check failed on Linux because that feature is
  unavailable there. macOS build/native audio acceptance remain pending.
  Installed applications and the active session have not been restarted.

### Installed update verification (2026-09-10)

- Fixed the device-list importer to accept URL-valid DNS hosts as well as numeric
  addresses; otherwise restored hostname bindings were rejected at import.
- Built and installed both Linux and macOS clients from matching source changes.
  macOS keeps the existing signed host/display helpers and signing identity.
- macOS binding tests: 12 passed. Host lifecycle tests: 26 passed. Bundle signature
  and dependency validation passed. Linux `nix build` passed.
- Both running host configurations disable audio streaming. Native macOS settings
  show the new switch disabled, and the saved viewer address retains its hostname.
- Remaining network issue: TLS through proxy DNS can time out even when direct VPN
  TLS succeeds with the same pinned identity. Saving a hostname does not fix that
  routing problem. End-to-end sound playback is not recorded as verified.

### Scope automatic discovery to DeskPort ports (2026-09-10)

Reason: independent Sunshine and DeskPort services on one computer have the same
machine name and were both added automatically.

- Only probe mDNS advertisements using the DeskPort base-port family: 48989 plus
  increments of 100 through 50889. Reject unrelated service ports before resolving
  or querying their server information.
- Hide and stop polling older unpaired, automatically discovered entries outside
  this family. Preserve their stored data and explicitly added or paired services.
- Manual addition and approved bindings continue to support explicit custom ports.
- Validation: Linux `nix build`, 26 macOS host lifecycle checks, and macOS bundle
  signature/dependency validation passed. Installed and restarted both clients.
  Existing native-service records are retained on disk but excluded from discovery
  and the visible device list unless explicitly added or paired.

### Connect directly to the desktop (2026-09-10)

Reason: selecting Desktop or Steam after selecting a device adds an unnecessary
step to the normal remote-desktop workflow.

- Clicking an online paired device loads and launches its Desktop entry directly,
  or resumes that desktop session. A brief loading page handles uncached app lists.
- Replace the loading page with the stream so disconnect returns to Devices.
  Leaving the loading page cancels pending launch and does not reconnect later.
- Never launch Steam or terminate another running application implicitly. Missing
  Desktop entries and busy hosts show actionable messages. The device menu retains
  Applications as an explicit fallback for custom hosts and other applications.
- Validation: 10 isolated QML checks passed, covering immediate and delayed launch,
  missing Desktop, busy hosts, cancellation and return to Devices after disconnect.
  Seven-language coverage, Linux `nix build`, macOS compilation and signed-bundle
  validation passed. Installed and restarted both clients. Native end-to-end
  desktop/input acceptance remains a manual check.


### Clipboard, pointer routing and resident service (2026-09-11)

Reason: daily cross-device development needs shared text and reliable local input
release; closing a window must not remove an unattended machine's remote access.

- Text clipboard sharing is enabled by default on both bound devices and can be
  disabled in Keyboard & pointer.
  A separate pinned mutual-TLS session starts only after streaming starts, works
  without adaptive resolution, and expires on disconnect or a missed lease.
  Initial contents are not transferred. Host revisions order concurrent copies;
  a newer local copy during a request stays pending. UTF-8 text is limited to
  1 MiB; invalid text, files and images are rejected without truncation or logging
  contents. PNG sharing remains pending. Hidden connected sessions keep sharing.
- Desktop-mode keyboard routing requires capture enabled, focus, a visible window
  and a pointer inside the video region (including letterbox boundaries). Exit,
  focus loss, hide and disconnect release held physical keys and mouse buttons.
  Ctrl+Alt+Shift+Z releases input; click inside to resume. The title shows routing
  state. Capture preferences remain respected; compositor-reserved keys still
  need native validation.
- Closing the main, viewer or resolution-transition window hides it. The tray
  recalls the window, disconnects only the viewer, stops local sharing explicitly,
  or fully exits DeskPort. Ctrl+Alt+Shift+Q disconnects the viewer. Qt background
  events remain responsive during the non-threaded macOS streaming loop; Linux
  retains its existing main-thread event pump.
- Host/helper failures retry after cleanup at 5/10/20/40/60-second intervals;
  a stable minute resets the delay. Explicit stop cancels recovery and survives
  application restart. Credentials and independent services remain untouched.
- Login startup now supervises the actual process: macOS LaunchAgent with
  unsuccessful-exit recovery; Linux XDG login entry starts a systemd user service
  with on-failure recovery and process-group cleanup. Successful tray exit does
  not respawn. New setup enables login startup; saved opt-outs stay respected.
  Existing enabled startup files refresh on launch. Service registration applies
  at the next GUI login; this is not pre-login/FileVault-unlock access.
- Validation: Mac/Linux builds, 100 per-direction text exchanges, SDL clipboard
  integration, service recovery, existing binding/lifecycle and UI regressions
  passed. See `docs/INPUT_SERVICE_ACCEPTANCE.md`. Do not mark native keyboard,
  real clipboard permissions or unattended reboot acceptance as complete from
  protocol tests or builds. Both installed applications were subsequently updated
  and restarted; macOS 0.1.1 reports sharing enabled and the bound Linux peer online, and Linux
  remains active under its user service. See the installation follow-up in
  `docs/INPUT_SERVICE_ACCEPTANCE.md`.
# Developer ID distribution work (2026-09-13)

Deferred by explicit user instruction on 2026-09-13: evaluate deeper source
integration and unified builds for Sunshine and related components. Inventory
existing Moonlight-derived code, host/input helpers and media dependencies first;
preserve upstream licenses, pinned revisions and maintainable patch boundaries.
Validate one source-built component against the existing release before expanding.
This is a future backlog item, not part of the 0.2.0 packaging release.

The user requested straightforward macOS installation for downloaded packages,
including signatures for all bundled open-source components. The separate
`release-macos.sh` workflow prepares an isolated candidate from a verified build,
signs all embedded code with Developer ID plus secure timestamps/hardened runtime,
and provides resumable notarization, app/DMG stapling and extracted-ZIP Gatekeeper
checks. Existing local-update packaging and installed services remain independent.

Apple notarization completed on 2026-09-13 for the app and DMG. All 126 Mach-O
files and 35 code bundles passed distribution signing checks. Stapled-ticket
validation, Gatekeeper checks after ZIP extraction and macOS pre-distribution
assessment passed. New downloads use distinct `-notarized` assets; existing
development asset hashes and running installations are preserved.
Next checkpoint: fresh-machine browser-download installation and first-use
permissions. Future Apple store/Android client planning is in the separate private
`deskport-client` repository; no mobile implementation is part of this task.


## 0.2.1 prerelease: CPU and resize P0/P1 (2026-09-13)

Instrument stage timings and provide a read-only CPU sampler. Reduce settling and
mode-confirmation waits, coalesce to the retained window before resuming, reuse
successful probes within one initialization, and remove control polling/redundant
transition paints. Retain separate host/helper processes and stop/resume.
See [measurement and acceptance](PERFORMANCE.md). Controlled CPU improvements,
physical presentation latency and live input acceptance await both machines
switching; P2/P3/P4 remain deferred.

Validation update: both native builds, macOS 21 binding / 15 UI / 8 service /
26 lifecycle cases and 30 transition cycles, Linux CLI / 15 UI / 8 service and
30 transition cycles passed. Four Linux distribution install checks, AppImage
startup, Apple notarization/Gatekeeper, and GitHub CI passed. Both consumer
system configurations built. Deployment and controlled live acceptance remain
pending; the initial CPU observation had uncontrolled content/build contention.

### 2026-09-13 — 0.2.2 follow-up

- Raise the text clipboard bound to 128 MiB UTF-8 with legacy 1 MiB negotiation;
  invalidate native snapshots on clipboard changes and avoid repeated Base64 work
  or rescanning accumulated network frames. See `CLIPBOARD.md` for memory limits.
- Remove two throwaway controller discovery passes from adaptive continuation;
  retain real input initialization and hotplug, and correct first-submit dimensions.
  See `PERFORMANCE.md`; native post-upgrade timing remains an acceptance checkpoint.
- Deployment: keep rollback applications in `.noindex`, migrate indexed backups,
  and unregister only their LaunchServices entries. Preserve two rollback copies.
- This prerelease remains macOS arm64 plus the NixOS x86_64 flake only. Deferred
  queue/static-frame work, video-only reconfiguration and helper merging remain
  P2/P3/P4 and require further measurement.

### 2026-09-14 — one-way mobile approval

Add the explicit `clientBinding: 1` capability to the v1 binding hello. Incoming
`role: "client"` requests carry no host identity or endpoint; local approval grants
only the TLS client's access to this host. The `client-ready` / `bound` exchange
completes persistence without inventing a reverse host. Saved client access is
removable and is never imported into the desktop list or endpoint refresh loop.
The approval popup states the one-way permission and retains foreground activation.
The Apple client integration lives in the separate private client repository.
Android approval and custom mobile binding entry selection remain follow-ups.

Validation: 26 isolated binding cases (including client-only approve/reject,
pre-approval disconnect/ack, invalid host claims and revoke), 15 UI cases and
seven translated UI catalogs passed. Physical mobile video/input acceptance is
separate from these protocol and interface tests.


## Client-controlled device settings — 2026-09-14

Reason: user resumed the deferred settings item and requested an isolated development
branch. Device profiles now collect normal/advanced streaming preferences, take a
connection snapshot, and send authenticated audio/input choices to the host.
The tray has a separate Reconnect action, preserving remote applications and
loading the latest saved profile. Sharing no longer duplicates session switches.
See [SESSION_SETTINGS.md](SESSION_SETTINGS.md) for the inventory and compatibility.

Next action: validate sound on/off, view-only, per-device video settings and ten
reconnects on isolated test hosts after an explicitly chosen build is activated.
No release, mynix update or deployed-service change is included in this slice.

### Connection and resize flow protection — 2026-09-14

- Keep the device list responsive during transport setup; opening it does not
  transfer SDL event ownership or hide it when the connection completes.
- Reject duplicate session execution and retain exclusive SDL ownership until
  execution and asynchronous cleanup both finish. Delay deletion/continuation
  notification until that same boundary.
- Coalesce stale queued size events, wait 500 ms after the last size/scale or
  drag activity, and recheck settling on the retained window before changing
  the virtual display. Only one display request is in flight.
- Validate with isolated lifecycle/UI/resize checks and both target builds.
  Real tray-during-connect and continuous-resize acceptance follows manual activation.

## 2026-09-14 — 0.3.0 stable release

User requested merging the session-settings branch into main and publishing the full existing Mac/Linux package matrix. Release verification is recorded in docs/RELEASE_0.3.0.md and the release verification asset; physical streaming acceptance remains separate.

## 2026-09-15 — disposable macOS VM verification

Create one disk-budgeted macOS 26 guest for release package/GUI smoke checks.
See [MACOS_VM_TESTING.md](MACOS_VM_TESTING.md). Physical streaming acceptance
remains separate; no host installation or service changes are implied.

The 0.3.0 package passed the complete automated VM cycle with Gatekeeper enabled
and identified developers allowed after user approval. Guest cleanup is synced
before power-off. The first-run local-network modal still limits screenshot/UI
acceptance; streaming and physical-device checks remain outstanding.

### Session-primary mirroring implementation — 2026-09-15

Implemented the requested virtual-main session topology with original-layout
snapshot/restore, a crash recovery journal, and opt-in actual text-caret geometry
for the Apple client. The 0.3.2-based development package passed isolated VM
connection/disconnect/reconnect and three native portrait/landscape/idle cycles.
The physical monitor and crash/hotplug acceptance gates above remain open.
See ADAPTIVE_DISPLAY.md for the protocol and measured validation boundary.

## Shared core — 2026-09-17

- [x] Extract workspace arithmetic into pinned deskport-core; preserve native adapters.
- [x] Add shared vectors and production display-protocol contract tests.
- [x] Keep Git and Nix core pins checked together.
- [ ] Extend binding fixtures and old-binary interoperability before changing negotiation.
- [ ] Consider a per-session input state machine only when desktop needs it.

See [SHARED_CORE.md](SHARED_CORE.md) for ownership and update workflow.

## Session display modes and device addresses — 2026-09-17

Reason: the user requested three per-device screen policies, automatic restoration
and address editing across clients.

- [x] Add negotiated mirror-primary, primary-only and extension policies in core.
- [x] Add Qt settings, per-device persistence and the authenticated policy adapter.
- [x] Restore macOS enabled state and verify recovery before deleting its journal.
- [x] Exclude newly attached displays from session takeover.
- [x] Expose the existing identity-preserving address editor in Device settings.
- [ ] Physical multi-monitor mode/scale/disable/restore and abrupt-disconnect acceptance.
- [x] Complete x86_64 Linux bundled-host package and pk4/wmn configuration builds on pk4.
- [x] Integrate the Linux adaptive-display branch and KDE session policies for the three-host prerelease.

See [SESSION_DISPLAY.md](SESSION_DISPLAY.md) for behavior and platform boundaries.
## 2026-09-16 — Linux adaptive workspace

Reason: Linux hosting could capture a desktop but did not implement the virtual
display control advertised by capable macOS hosts. Implemented KWin 6.6+ and
GNOME/Mutter backends behind the existing exclusive TLS display-controller
protocol. Keep one output across resize/reconnect; verify pixels and scale before
acknowledgment; preserve other output settings; close on helper/compositor loss.
GNOME capture uses an owned PipeWire object serial instead of a separate portal
selection, and KWin refuses physical-output fallback when its virtual screen is
missing. See [Linux adaptive display](LINUX_ADAPTIVE_DISPLAY.md).

Next checkpoint: native Linux build and isolated compositor/capture regression,
then user activation through mynix and iPad portrait/landscape, HiDPI, input and
reconnect acceptance. Physical mirroring and unsupported compositor backends
remain separate backlog items.


## 2026-09-16 — KDE virtual-primary mirroring correction

Reason: real-client feedback clarified that an extended workspace did not meet
the intended workflow. KDE now makes the virtual output primary and mirrors the
other enabled outputs from it. An independent recovery process restores the
original order and replication sources after normal shutdown or helper death.
GNOME retains adaptive extended displays; Mutter physical mirroring is still a
backlog item. Next checkpoint: user activation and physical-screen/iPad mirror,
portrait, scaling, input-position and shutdown-restore acceptance.


## 2026-09-16 — Reconcile KWin's initial mode before capture

Reason: deployed feedback showed sharing repeatedly failing because the first
announced virtual mode differed from the creation request. Apply and verify the
requested mode instead of treating the initial mismatch as fatal. Journal helper
startup errors so remote diagnosis does not require opening the GUI. Added an
isolated initial-size/scale mismatch fixture. Next checkpoint: activation on a
physical GPU, successful host readiness and iPad mirror/input acceptance.

## 2026-09-16 — Linux connection-scoped displays (0.3.9)

User feedback confirmed adaptive client resolution works, but exposed disabled
physical panels becoming mirrors and missing disconnect restoration. Preserve
pre-connection enabled state and full physical layout; remove virtual outputs on
full disconnect and recreate them for the next connection. Disarm idle recovery
so later local edits survive app exit. GNOME capture follows the recreated node.

Checkpoint: isolated disabled-panel, disconnect/reconnect capture and crash
recovery coverage, followed by user activation and repeated physical-device
connections with the internal panel disabled. GNOME physical mirroring and
monitor hotplug during a remote session remain backlog items.

Follow-up input report: finger gestures and Pencil use separate mouse and pen
paths. The KDE mirror source retained a nonzero extended-layout origin, while
absolute mouse injection addresses the workspace bounds. Place the sole logical
mirrored workspace at (0, 0), verify that geometry, and restore physical positions
after disconnect. Actual iPad finger interaction still requires activation and
user acceptance; it is not inferred from Pencil input or capture tests.

## 2026-09-16 — Desktop device address entry points (0.3.10)

User requested IP/domain editing from the PC device view, matching the mobile
workflow. Expose the existing bound-device editor directly in the card action
menu and device settings, including offline devices; share the editor with Saved
access to keep validation and identity preservation consistent. Current sessions
must finish before editing. Legacy PIN host management is unchanged.

Checkpoint: UI save/reopen and invalid-input regression, binding persistence,
Linux build/release and user verification at a changed endpoint. Two-finger
mobile wheel gestures were already implemented; the shared gesture/input tests
passed on macOS. NixOS finger input still needs post-activation acceptance.

## 2026-09-19: Confirmed session takeover

- Implement capability-negotiated admission before display/video startup, explicit
  desktop confirmation, and fatal control-loss handling.
- Gate Sunshine launch/resume/RTSP through a certificate-bound local reservation;
  terminate and join old streams without restarting the host.
- Validate cancellation, stale/expired/replayed confirmation, concurrent contenders,
  legacy viewers, and management failure through isolated TLS tests.
- Pending acceptance: temporary signed host and physical desktop/Android/Apple
  takeover, including old-input release and local display restoration. No live
  service activation is authorized by these source changes.

- Desktop adjustment now uses the shared final multiplier, persists by device,
  and reconnects video from the tray while retaining admission. The macOS native
  tray preserves submenu and checked/disabled state.
- The explicit parallel-host acceptance entry uses isolated display identity and
  no shared layout journal; it only exercises extended-workspace behavior.
  See [SESSION_TAKEOVER.md](SESSION_TAKEOVER.md) for limits and acceptance steps.

## 2026-09-20: Device action menu cleanup

- Keep details and per-device settings on the dedicated card buttons. Remove
  their duplicate action-menu entries, application browsing and Wake-on-LAN.
- Keep session controls, pinning, address editing, legacy pairing, network testing,
  renaming and removal in the action menu. Card activation still opens Desktop.

- Device headers now allocate equal slots to online/offline/checking status,
  details, actions and settings, matching the mobile card layout.

- Keep picture presets, automatic resolution, remote audio/input and clipboard
  switches in Device settings only. Advanced streaming retains detailed tuning;
  editing frame rate or bandwidth selects manual streaming.


### 2026-09-21 input and startup candidate

- Candidate: remove informational startup sleeps, negotiate from mapped viewer
  geometry, input-driven cadence with one-FPS idle refresh and loss/user ceilings.
- Pending physical checkpoint: repeated cold connects without immediate resize,
  real first frame/input, static clock, sustained typing/scrolling, jitter and loss.
- Pending encoder work: supported live VideoToolbox bitrate reconfiguration and
  measured packet throughput; input cadence alone does not prove bitrate control.


### 2026-09-21: shared Linux smart cadence candidate

- Reuse host/common input activity and congestion policy in both Linux encoder paths.
- Preserve the final changed frame during pacing; poll PipeWire while idle and use
  explicit damage metadata without GPU readback. Keep video updates independent of input.
- Deliver a private Nix/macOS candidate; physical Linux idle/input/video validation
  follows the user's manual activation. This is not a formal release.


### Desktop stable update entry and remote macOS upgrade (2026-09-21)

Phase 1 implemented: the sidebar version opens stable release status and plain-text
release notes, with a GitHub download-page action on macOS and Linux. Checks run
at startup and every six hours; manual retry is available. Requests time out after
15 seconds, reject draft/prerelease or malformed metadata and unexpected release
URLs, and compare numeric versions to avoid downgrade prompts. Includes English,
Simplified Chinese and Traditional Chinese UI. Installation remains manual; the
UI directs Nix-managed users to their existing package-management workflow.

Validation: isolated stable-channel parser tests and macOS app compilation passed.
All 22 isolated Qt UI regressions passed, including opening the update dialog. Linux Nix build attempted
but unavailable: this Mac has no configured x86_64-linux builder.

TODO — macOS self-update (deferred by user, 2026-09-21):
- [ ] Evaluate Sparkle 2 with Qt UI and the signed/notarized release pipeline.
- [ ] Detect ordinary writable app installs versus Nix-managed installations.
- [ ] Download and authenticate the complete update before stopping sharing;
      resolve required installation authorization while still connected.
- [ ] Use an independent installer; coordinate login-agent restart and temporary
      recovery-helper suppression so replacement survives remote disconnection.
- [ ] Preserve pairing, app identity, settings and previously enabled sharing;
      validate capture/input permissions after replacement.
- [ ] Define bounded health checks, old-version recovery and configuration rollback.
- [ ] Verify a real remote old-to-new upgrade, reconnection, video and input,
      interrupted downloads and failed-start recovery before claiming unattended
      update support. This does not include rebooting macOS.

Next action: review phase 1 in a candidate build. No automatic installation,
release publication or deployed-service changes are included in this phase.

- 2026-09-21: Preserve the configured connection entry across authenticated stream endpoint refreshes; mobile adapters adopt the shared endpoint contract. Private device acceptance remains pending.

## 2026-09-20 — Windows x64 static client evaluation package

User-requested MinGW cross build retains the Windows client, D3D11VA/DXVA2 and
Vulkan renderer. Audit the offline NSIS payload and distinguish static libraries
from Windows/driver DLLs and installer plugins. Windows integrated hosting is
unavailable in the selected source revision; AntiHooking and Discord RPC are
not included by this build. Native installation, input release, background recall
and streaming acceptance remain pending user VM testing. No deployment or
release is part of this task.


## 2026-09-20 — Windows outbound client binding fix

Windows VM feedback exposed a binding initializer that required local hosting.
Use the existing client role and complete its approved/persisted
`hello/request/pending/accept/ready/client-ready/bound` flow without a listener,
local host metadata, trust provisioning or service startup. Preserve the default
macOS/Linux mutual flow and certificate pins. Add isolated outbound protocol,
persistence, endpoint-refresh and one-way UI regression coverage. Deliver a
separately named Windows evaluation installer; VM binding/streaming acceptance
remains the user's next checkpoint, with no automatic installation or networking
changes.

## 2026-09-20 — Full Windows host integration (in progress)

The requested full Windows package supersedes the client-only evaluation scope.
Work is restoring mutual binding and adding a privately managed, protocol-patched
Sunshine host, Windows display lifecycle, startup and installer integration.
Existing static Qt/client dependencies are reused. Windows VM capability probing
found Desktop Duplication initialization and the software H.264 encoder available;
this is not streaming, input, audio, clipboard or full-package acceptance.
Physical display mode changes require an advertised mode and a persisted recovery
record. The original signed MIT virtual driver started successfully in the VM under
a DeskPort-owned device instance. Live topology/restoration and adaptive-mode
behavior remain under implementation and test. Full-package readiness is not claimed until the autonomous
VM acceptance checklist is complete. Existing deployed desktop sessions are not
part of these tests.

### Display recovery incident follow-up

A test left the owned VDD enabled and a later VM reboot caused mouse-coordinate
problems. The user disabled the owned device and restored the internal display.
The resumed baseline verifies Code 22 and one physical primary output. A new
independent elevated display lease durably snapshots QueryDisplayConfig and
restores topology, primary, coordinates, mode and refresh while disabling only
the owned device. Native helper EOF, forced termination and invalid-mode tests
now restore the baseline; full application, installer and normal-protection
acceptance remain outstanding. The original detected binary and Defender evidence
are preserved. No false-positive determination or release readiness is claimed.

Windows full follow-up (2026-09-20): native clipboard pipe transport is implemented; actual Windows-to-Linux frames, software audio, keyboard/mouse, bidirectional text and Windows-to-Linux image transfer have private VM evidence. Window hiding released a held remote key before local key-up. These are partial acceptance, not a release. Installation now owns/restores the upstream VDD configuration pointer and checks privileged recovery executable path permissions. Windows advertises its enumerated SDR modes; new clients select the nearest supported backing size. These latest installer/mode changes await VM validation. Reverse media, file clipboard, full upgrade/uninstall and normal-protection detection verification remain open.

Windows full continuation (2026-09-20 evening): replaced Windows Qt delayed
clipboard publishing with an OLE IDataObject advertising formats without reading
file contents. Queued pipe dispatch permits data replies during OLE nested loops;
CF_BITMAP/CF_DIB/PNG support native receivers. Windows source-file reads verify
handle identity, size/time and reject reparse points. Native fixtures and actual
isolated Linux-to-Windows files/images passed; Windows-to-Linux files passed again.
A real Windows tray click hid/recalled the same streaming window, releasing a held
remote key before local key-up. Candidate2 running upgrade/pairing preservation
and finite-mode decoding have VM evidence. Fresh baseline recovery after VBox
physical-screen resizing also passed. Full uninstall/reinstall, abnormal
disconnection/reconnect, final display acceptance and normal-protection retest
remain pending. VM testing is paused pending permission to constrain the existing
VirtualBox process and the Windows network trust decision; no deployed host
services or host network rules were modified. Full package notices are maintained
separately from the historical client-only notices. No release is ready.


## 2026-09-20 — Windows 0.4.3 integration (acceptance pending)

The explicit Windows delivery request integrates desktop 7fccde75 and core
0badc8e3 with the full Windows host, recovery guardian, finite display modes,
OLE clipboard and installer. Keep the upstream connection-window recall and
session lifecycle fixes. Windows currently creates its private display for the
sharing lifetime and removes it when sharing stops; do not present the macOS/Linux
per-connection display lifecycle as verified Windows behavior. Empty OLE publish
now releases only this provider's offer, preserving another clipboard owner's
later data. Native ownership regression, new UI recall, exact final package
maintenance and normal-protection validation remain release gates.

Native Windows OLE ownership regression and full-sync1 running upgrade with
identity preservation passed. New Windows UI was captured privately. A physical
primary-mode regression during virtual-output attachment was reproduced and
fixed by preserving the pre-enable CCD modes; startup, supported resize and EOF
restoration passed with the diagnostic helper. Full-sync2 adds post-apply mode
verification and failure-triggered lease termination; exact-package acceptance
remains pending. Network Private was authorized and verified on the VM only.


## 2026-09-21 — Windows hardware parity candidate

User requested a full bidirectional Windows desktop product, an offline installer
with statically included libraries, and a native title bar matching the app theme.
The Windows branch now rebuilds verified host source with current shared overlays,
waits for host readiness during mutual binding, and supports noninteractive CLI
queries. See [Windows development](WINDOWS_DEVELOPMENT.md) for scope and evidence.
Final candidate packaging, native theme checks and bidirectional hardware session
acceptance are tracked separately; this checkpoint does not authorize a release.

Windows candidate checkpoint: the full offline installer and portable payload
passed static-dependency and payload-equivalence audits. Native version/help and
Light/Dark title-bar checks passed. Initial payload/installer scans returned no
Defender detection records, but subsequent native use quarantined the display
helper and blocked mutual binding. Installation and two-way hardware
streaming/recovery are not accepted. The Windows display lifecycle remains
sharing-scoped and is not claimed as per-session parity.

### Windows release blockers and next actions

- [ ] Resolve Microsoft Defender detection of `host/deskport-display.exe`
  (`Trojan:Win32/Bearfoos.A!ml`). The original sample was submitted to Microsoft
  for suspected-false-positive review on 2026-09-21; the review is pending, not
  a clearance. The submission portal also reports `Trojan:Script/Wacatac.C!ml`
  for the submitted archive. Keep the candidate blocked from release until
  the findings are resolved.
- [ ] After the review, update Defender normally and repeat exact-package
  installation and bidirectional binding/streaming/input/recovery checks with
  default protection enabled. Do not use exclusions or disable protection to
  satisfy acceptance. Preserve user settings and existing device identities.
- [ ] Establish trusted Windows code signing for the installer and embedded
  executables before public release; signing alone does not resolve detections.

Missing-host startup and binding now report the missing bundled component.
The installer checks required payload files after extraction and before
completion, returning a failure for incomplete installs. Binding regression
coverage passed (99 tests); installer syntax compiled with NSIS. These checks
improve failure handling but do not resolve the Defender release blocker.

## 2026-09-23 — Windows hardware recovery and client matrix

The Windows development branch now includes the 0.5.6 desktop baseline. A
closed-lid hardware test exposed a temporary driver-removal veto; the recovery
guardian now retries removal within a fixed deadline and verifies the owned
device is disabled before declaring restoration complete. The installed offline
candidate passed ten rounds of resize/release, forced exit and restart after
crash, including portrait/landscape switches in every scenario (30 scenarios).
Installer/portable payload equivalence, static dependencies, installed helper
hashes, silent failure behavior and credential retention were checked.

A physical Apple tablet received real Windows frames as a standard user and
passed twelve orientation changes plus disconnect/reconnect with the installed
candidate. A macOS viewer received Windows frames and correctly reported takeover
by the tablet. The Windows viewer also rendered the current macOS desktop;
its own window capture was inspected alongside the host and transport state.
Finite advertised modes are negotiated by the private Apple client; its source
remains separate. Full secure-desktop input, pre-login operation, reverse-stream
input and the remaining PC feature matrix are still acceptance
items. Testing under a pre-existing user exclusion does not close the Defender
release blocker above. No public release is authorized by these results.

Secure-desktop follow-up: a development-only LocalSystem service probe now
authenticates a fixed local console client and opens the Winlogon desktop in a
bounded worker. Thirteen native checks passed, including rejection of foreign
executables, anonymous/low-integrity clients and session-zero requests, plus pipe
collision, timeout and shutdown checks. The temporary service was removed after
testing. This is an IPC/permission checkpoint, not lock-screen streaming or input
acceptance; see [the service boundary](WINDOWS_SESSION_SERVICE.md).

### Windows virtual-display activation fix — 2026-09-23

- A Windows 11 target rejected initial indirect-display positioning through
  `ChangeDisplaySettingsExW` with `DISP_CHANGE_FAILED`, despite an active owned
  CCD path. The display helper now continues through the existing
  topology-preserving CCD update and verifies the resulting owned mode and
  position, instead of aborting on the GDI result alone.
- The final helper passed three native rounds of resize/release, forced exit,
  and restart after crash (nine scenarios), including 1080×1920 / 1920×1080
  switches and physical-layout restoration. Installed-package streaming
  acceptance is recorded separately; these native checks alone do not prove it.

- Installed Windows activation fix: real macOS first-frame/input/full-screen and
  Android fixed-size first-frame/input/reconnect passed. Keep Android finite-mode
  negotiation and the observed macOS mDNS reconnect crash open; do not claim
  complete cross-platform acceptance.


### Windows dynamic displays and policies — 2026-09-23

- Implement dynamic dimensions with the existing signed VDD: a bounded guardian
  request updates the owned mode configuration and recreates the owned adapter.
- Implement mirror, exclusive and extend with verified CCD topology changes and
  independent recovery. Follow the actual capture source across re-enumeration;
  never fall back to physical capture during an absent owned output.
- Native custom-size, portrait, repeated dynamic resize, cross-session reuse,
  release and crash-recovery checks passed for all three policies (nine scenarios).
  Installed streaming acceptance remains a separate gate for the private candidate.
- Prevent computer-polling reference-count underflow, which could replace an mDNS
  server while an orphaned browser's timer still referenced it. Real reconnect
  acceptance is required in addition to this lifecycle fix.

- Installed candidate: Android first frame, keyboard and click passed in all three
  policies; rotating the client changed the live stream from 824x1644 to
  1784x684. Extension disconnect/reconnect and restoration passed.
- Windows outbound launch exposed a local-name resolver mismatch after successful
  authenticated control requests. Resolve the name through Qt once and use the
  same numeric endpoint for launch and media; live acceptance remains required.
- Windows-to-macOS first frame passed with the resolved endpoint. One reconnect
  received capture-initialization 503, then succeeded on retry. Windows now
  retries only that specific pre-stream failure up to three times while keeping
  the authenticated lease, with cancellation and recovery-deadline checks.
## 2026-09-23 — Windows runtime detection reproduced; user notice added

Reason: repeated user-requested uninstall/reinstall and two-minute runtime tests
with Defender real-time/cloud protection enabled and no exclusions reproduced
`Trojan:Win32/Bearfoos.A!ml` quarantine of `deskport-display.exe` in 0.5.6 r4 on
round three; the first two rounds passed. Windows release clearance remains
blocked. The exact sample was submitted to Microsoft as
`e28ba5f4-9624-46ec-938a-1e6430c89f89` (Submitted / Pending at verification).
English and Simplified Chinese README platform sections now link to the
[Windows antivirus notice](WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).
Next checkpoint: obtain Microsoft's determination, then repeat exact-package
installation and runtime checks with protection enabled and no exclusions.


## 2026-09-24 — 0.6.0 source integration

- Integrate desktop development and Windows adaptive-display work into main; prepare desktop version 0.6.0 alongside mobile version 2.0.
- Keep one root README. Localized and upstream READMEs live under `docs/readme`; synthetic UI previews live under `docs/media`.
- Complete the missing Windows display-policy translations and fix stale UI-test delegate references after model resets. Core, host lifecycle, translation and UI checks pass.
- Formal packages, signing, exact-package Defender checks, store metadata and submission remain separate release gates. Do not infer release readiness from source integration or earlier private-package acceptance.

### Windows installer follow-up

- Repeated 0.6.0 installation exposed error 13 from the display class installer
  while DWM held the newly started virtual adapter. Use the recovery helper's
  Configuration Manager disable path with bounded retries and verify the owned
  device reaches the disabled state. Failed transitions still fail installation.
- Exact rebuilt-package installation, protected runtime and live streaming
  acceptance remain required before Windows publication.
## Windows 0.6.1 correction — 2026-09-24

Reason: the 0.6.0 Windows package contained a client reporting 0.5.7, and
the affected Windows machine rejected initial virtual-display positioning.
Recursively regenerate qmake subprojects and reject mismatched PE file/product
versions during build, packaging and extracted-payload audit. Native CLI checks
also require the requested version. Recompute the owned virtual display's CCD
desktop-image geometry and validate the configuration before applying it, then
verify that physical sources and the requested virtual position are preserved.

The affected machine passed startup, dynamic resizing, all three display
policies, normal restoration, forced-exit recovery and restart after a crash.
The final installer upgraded the affected machine to 0.6.1; the GUI and CLI
reported 0.6.1, another device discovered the Windows host, and the user confirmed
a successful iPad connection. Protected-runtime scanning is recorded separately.
Repeated unattended relaunch and locked-session acceptance remain unverified.

## Portable KDE sharing permissions — 2026-09-25

Reason: issue #2 reports that the 0.6.0 AppImage cannot start sharing because
KWin does not expose screencast to `deskport-display`. Automatically register a
missing user-scoped grant for the helper's canonical executable, refresh KDE's
cache and reconnect with bounded retries. Existing native grants take the fast
path. AppImage remounts need no repeated manual setup or stable extraction path.
Keep KWin permission checks enabled and grant only the helper's screencast API.
The isolated KWin regression adds empty permissions, repeat starts, remounts,
symlinked paths and stale generated-entry cleanup to the display lifecycle checks.
The AppImage-only `v0.6.1-rc.1` prerelease was published with package checks,
isolated KWin regressions and CI passing. On 2026-09-26 the reporter confirmed
that it fixes the issue on Bazzite; see [confirmation](https://github.com/keithxc/deskport/issues/2#issuecomment-5835796537).
This confirmation validates the reported sharing-startup problem, not a separate
full input, GPU or long-session acceptance matrix. Merge the fix into main and
restore its development version to 0.6.1; retain the tested prerelease and tag.

## Cross-platform text-caret delivery — 2026-09-26

Reason: mobile keyboard avoidance needs the remote insertion region; a local
click anchor alone cannot follow typing in a Linux or Windows editor.

- [x] Add bounded, isolated Windows UIA/Win32 and Linux AT-SPI geometry readers,
  reusing the existing opt-in lease-scoped `text-caret` protocol.
- [x] Invalidate readers on display transitions and shutdown; keep provider hangs
  independent of video and topology recovery.
- [x] Compile the Linux display helper and pass isolated AT-SPI/reader lifecycle
  checks using the locked Nix Linux builder. Existing peer binding suite: 112 pass.
- [ ] Complete full-package Nix build: Sunshine's `nvhttp.cpp` compilation was
  killed by the builder during validation. The focused helper build passed.
- [ ] Windows native compilation and live Windows/KDE/GNOME editor, terminal,
  browser, IME and mixed-DPI validation. User will package and activate separately.

See `docs/ADAPTIVE_DISPLAY.md` for provider limitations and fallback behavior.

### 2026-09-27: Nixpkgs upstream review

- Submitted https://github.com/NixOS/nixpkgs/pull/567358 and marked it ready after maintainer review. Upstream merge remains pending.
- Addressed new-package strict dependency/structured attribute checks and maintainer contact validation. The sandboxed package rebuild, three package tests and maintainer check passed.

### 2026-09-29: AppImage 0.6.4 compatibility preview

Reason: AppImageHub requires older Ubuntu LTS startup; the user authorized an
AppImage-only prerelease following the maintainer's private-runtime suggestion.
Bundle separate viewer/host glibc runtimes with checksum-pinned sharun, preserve
Qt/KDE executable identity, and include runtime notices and exact source links.
The final payload passes Ubuntu 22.04 CLI/QML/X11/API/state-isolation checks and
isolated KWin permission/remount/display lifecycle regressions. Keep 0.6.3 stable
assets and native-package requirements unchanged. Physical GPU/input/audio and
streaming acceptance, and AppImageHub catalog review, remain separate gates.
See [the verification record](APPIMAGE_064.md).

## Desktop concurrent viewers — 2026-09-28

The desktop shell now keeps one isolated media worker per connected server. The
existing media library has process-global transport, decoder and input state;
workers preserve its single-session contract while the shell owns the device
list, tray, session selection and lifecycle. Devices and tray actions do not wait
for a media worker to stop. Returning to Devices retains all connections.

- Existing device cards and the Connections tray menu select an existing desktop. Closing
  one session stops only its worker. A worker crash leaves other sessions alive.
- Presentation handoff releases the previous viewer's input and clipboard before
  showing the next; background viewers retain transport but mute audio and reject
  physical input. Native SDL windows are retained and hidden/shown. This is not
  multiple render surfaces embedded in one Qt window.
- Workers load only their target's saved host and cannot write the host registry
  or explicitly admit a removed host. The shell owns device removal.
- Admission probes check every outgoing branch using the existing core v1 path
  protocol; all branches must be safe and graph mutation fails closed.
- Worker diagnostics use separate bounded run logs, included through the same
  privacy allowlist when exporting diagnostics.
- Regression coverage includes two subprocesses, delayed input handoff, rapid
  selection, list return, independent crash/cancel, fanout/cycle admission, and
  real production worker startup with isolated settings. Physical simultaneous
  video/audio/input and native compositor switch latency remain acceptance work.

### Preserve the existing desktop UI — 2026-09-28

Removed the newly introduced session tab strip and restored the original page
spacing at the user's request. Session isolation is a backend concern; the
existing device cards remain the entry point. Future separation of presentation
and session services must preserve the current UI unless explicitly requested.

### Per-device session state and responsive handoff — 2026-09-28

The existing device cards consume backend session state keyed by host identity.
Every connected device shows Return to desktop, including background sessions.
Reconnect, fullscreen and disconnect commands address that card's session.
The UI does not wait for transport startup, shutdown or input handoff. A worker
that cannot acknowledge input release is terminated after a bounded deadline;
only its exit releases ownership to the next viewer. Repeated selections do not
extend that deadline. Fullscreen requests wait for the target viewer to appear.

Validation: the macOS and Linux Qt UI suites pass all 26 cases; the final
per-device navigation check also passes on Linux. Subprocess regressions cover
rapid selection, background disconnect, fullscreen handoff and failed workers;
heartbeat gaps during the stalled-handoff fixture were 11–20 ms. Two Linux
machines independently built the same final candidate with Nix and produced
matching executable SHA-256 hashes. Both candidates pass 20 switches between
two real worker processes, independent cancellation and IPC-loss exit with
isolated settings and no server connection. UI tests now use temporary settings
on macOS as well as Linux, preventing setup preferences leaking across runs.

Authorized live integration used a physical Linux viewer with macOS and Linux
servers. A disposable in-process driver invoked the real card actions; it is
not included in production source. Both streams submitted their first video
frames, and a private screenshot confirmed both original cards offered Return
to desktop with no session tabs.

The initial rapid-switch run exposed stale resize debounce state across hidden
windows: transient compositor geometry could trigger a transport restart on
recall. Hiding/minimizing and recalling a viewer now reset that interval. The
new desktop-state regression passes on macOS and Linux (10 cases), and the
production Linux candidate builds with the repair.

After repair, ten rapid switches and ten switches with one-second dwell both
passed while retaining both connections. Maximum card-to-viewer acknowledgement
was 58 ms and 52 ms respectively, measured with a 50 ms polling interval; these
are window acknowledgement times, not display-to-photon latency. The Qt heartbeat
maximum was 170–171 ms, including test screenshot capture. Each run independently
disconnected one device and recalled the other. The four viewer logs contain
first-render submissions and no adaptive transport restarts. The temporary
viewer service and workers exited, and its original deployed service was
restored. Physical keyboard/mouse routing and audio acceptance remain untested;
this does not install or publish the repaired candidate.

Scope confirmed by the user: implement presentation/input switching first;
retain background media connections and defer resource optimization. The
vendored RTSP client has no PAUSE command. Suspending remote video transmission
is separate protocol work; it must not turn a presentation switch into
disconnect/reconnect. Backend decode throttling also needs keyframe recovery
validation before enabling it.

### Optional video transmission pause candidate — 2026-09-28

The desktop candidate negotiates `videoPause: 1` on the existing admitted TLS
connection. Original UI/card actions remain unchanged. Background presentation
requests stop video UDP at the host after the initial key frame; audio, control
and the session lease remain alive. Legacy hosts continue streaming. The sender
fences pause acknowledgments against frame sends, drops pre-resume encoded
packets and requests a new IDR before forwarding video again. Capture/encoding
suspension is outside this version. Input release and focus-based audio muting
retain their existing implementation.

Only macOS and Linux adapters advertise the capability. Ordered commands,
completed duplicates, rapid desired-state coalescing, lease checks, timeout
failure and resumed-control state normalization are covered by isolated tests.
The wire contract is in core `protocol/VIDEO_PAUSE.md`; this local candidate uses
an immutable NAR snapshot matching its core Git commit. It is not a public
release pin and must be replaced with a reachable reviewed core pin before public
integration.

Validation so far: core tests, 117 existing binding cases, 11 additional protocol
cases (13 including setup/cleanup), 26 UI cases, 10 desktop-state cases, and two
real worker processes with 20 isolated switches pass on macOS. The signed macOS
candidate and Linux Nix candidate both build. The 13 protocol checks and isolated
worker checks also pass on Linux. The built Linux Sunshine passes the real
loopback management endpoint tests for authentication, lease isolation, malformed
state and repeated pause/resume without creating a video stream. The packaged worker test must run outside
the devShell's Qt plugin environment, which points at store plugins rather than
bundled frameworks.

Full multi-host video/network acceptance remains pending. Neither build success
nor protocol acknowledgments prove stopped network traffic, new displayed frames,
switching latency, physical input or audio routing. The separately authorized
temporary server runs below preserve installed packages and restore original
processes. No release or mobile source has been published.

The next live legacy-host run did **not** pass: both connections reached the
connected state and the first recall completed, then the controller stopped
advancing. Removing synchronous screenshots reproduced the stall. A disposable
parent-debugger run captured the controller's main thread in Mesa
`_mesa_glthread_finish -> eglSwapBuffers -> QSGGuiThreadRenderLoop::renderWindow`.
The root cause is not established; do not attribute it to the wire extension or
call it a resolved driver issue. Temporary Mesa threading and Qt software-renderer
settings did not complete a full run either. These settings were not committed or
applied to the deployed service. The original viewer service was restored and all
test workers exited. This blocks full live/UI acceptance of the candidate; the narrower macOS host
results below do not resolve the controller stall.


### Temporary macOS host validation — 2026-09-29

The temporary main app and embedded host preserve the installed designated code
requirements and pass deep strict signature verification. The private test host
suppresses automatic Screen Recording and microphone permission requests; no TCC
reset, installed bundle replacement or permission grant was performed. Existing
screen capture produced decoded frames. Original application instances were
restored after testing.

Read-only authenticated host telemetry now reports completed video sends,
suppressed frames, keyframe sends and the last frame index. During a 65-second
pause, sampled completed sends stayed at 23 while suppressed frames increased
from 1530 to 4600; the session remained connected. Resume decoded a new keyframe
in approximately 207 ms. Ten rapid hide/recall cycles and one longer-interval
cycle completed; the following window-response wait timed out. The hidden
controller heartbeat's maximum observed gap was 30 ms. These are worker/decode
measurements, not physical display latency or interactive card-UI acceptance.
No independent packet capture, physical input or audio acceptance is claimed.

The Linux server candidate did not start its display helper successfully.
Reusing the original launch environment did not resolve it. Multi-host switching,
independent failure recovery and the complete dwell sequence remain blocked.
The original Linux server/viewer instances were restored. The updated real
loopback API tests pass on the built Linux host, including telemetry and video
command lease/type/idempotence checks without a media stream.

### Responsive shell and direct desktop switching — 2026-09-29

Reason: review of the concurrent-viewer UI work. The shell still ran the Qt
Quick scene graph on its GUI thread, every switch waited for all background
viewers, and switching required a detour through Devices.

- The device shell no longer forces the upstream non-threaded Qt Quick render
  loop, which only the in-process stream needed; Qt's platform default applies.
  Command-line stream mode and workers keep the previous setting, and an
  explicit `QSG_RENDER_LOOP` is still honoured. This targets the recorded
  controller stall in `eglSwapBuffers` on the GUI thread; that root cause is
  still unconfirmed and live compositor acceptance remains pending.
- A switch hides only a viewer that may be presenting. Hidden background viewers
  are not asked again, so they add no latency and are no longer terminated when
  slow to acknowledge a redundant hide. Repeated Devices requests are idempotent.
  Input handoff ordering for the presenting viewer is unchanged.
- Ctrl+Alt+Shift+N in the presented viewer asks the shell for the next
  connected desktop (connection order). Background viewers cannot switch.
- The tray Connections menu is rebuilt only when a row's name or state changes,
  not on every selection or visibility acknowledgement. The legacy control-center
  grid used only by command-line streaming is created on first use.

Validation: macOS multi-session (6 cases, including a new case that fails on
the previous hide policy with a 2.1 s handoff), UI (26) and host lifecycle
(33, 2 platform skips) suites pass. Physical switch latency, focus/activation on
KDE Wayland, keyboard/mouse routing and audio remain live acceptance checks.

### Tray menu for concurrent desktops — 2026-09-29

Reason: switching desktops from the tray took three menu levels, the menu did
not show which desktop was presented, and the top-level reconnect/disconnect
items silently addressed an invisible selection.

- Live connections are listed at the top of the tray menu in the device list's
  saved order (groups flattened in place); the presented desktop is checked and
  connecting ones are labelled. One click presents that desktop. At most seven
  are listed; "All connections (N)…" opens the device list.
- Disconnect and More → Reconnect name their desktop: the presented one, else
  the last one shown. They are hidden when there is none. Restart moved to More.
- Menu items are fixed slots updated in place, refreshed before the menu opens,
  and never destroyed while a native menu is tracking.
- The tooltip reports connected desktops and the presented one.
- Left click keeps alternating between the device list and the remote desktop.
  If the last viewed desktop has ended, it presents the first connected one in
  device-list order; with none left it hides the device list instead of doing
  nothing. Ctrl+Alt+Shift+N follows the same order.

Validation (macOS, isolated): tray actions, the seven-item cap and targets;
session order, wrap-around and recall; UI (26), host lifecycle (33, 2 platform
skips) and multi-session (6) suites. KDE StatusNotifier and macOS status-menu
rendering remain live acceptance checks. Linux builds run on pk4 or wmn.

### Reachable development inputs — 2026-09-29

Reason: every test host needed private source and core snapshots preloaded
before a configuration could build. The `dev/pcui` branch is published, and
the video pause core change is published on the core `dev` branch
(`2ebf5c9`, identical tree and NAR to the local snapshot). Both Nix and Git
pin that revision, so a consumer can pin a public DeskPort commit and build
from source. `TEST_BUILD_ID` now names the development line instead of an old
private build. Core `main` and the public release pins are unchanged.

### Desktop tuning feedback and tray recall — 2026-10-01

Reason: desktop fine tuning enlarged the wrong way and, from a small client
window, larger values silently hit the macOS 800×600 logical floor. Changing it
during a connection no longer applied after desktops moved to worker processes,
and the tray's left click showed the device list before the desktop.

- Larger tuning values enlarge remote content (core `a107606`).
- A worker reports, with its traffic sample, when the host's minimum desktop
  overrides part of a requested enlargement. Device settings then explain the
  limit instead of the usual note.
- Choosing a tuning value while that device is connected sends the saved value
  to its running stream. The adaptive check then compares the resulting desktop
  with the current one: an unchanged size (for example any value above 1.0 once
  the macOS floor applies) keeps the stream; a new size takes the window-keeping
  resolution change, not a full reconnect. A fixed-resolution stream reconnects
  only when its size changes. A value arriving between sessions is kept for the
  next one.
- Left click presents the last viewed connected desktop (else the first in
  device-list order) without passing through the device list. With no desktop
  connected it toggles the device list. The list stays in the tray menu. This
  supersedes the alternating left click described for 2026-09-29.

- Automatic network recovery is silent. The viewer window is retained across
  retries (showing "Connection interrupted. Reconnecting…") instead of being
  destroyed and recreated, which made the compositor raise and focus a new
  window. The shell raises a viewer only for a user action; a worker's restart
  (unsolicited hide, then ready) restores presentation state without showing,
  raising or focusing the window. Final failure still opens the device list.
- A hidden desktop whose connection dropped could reconnect every ~29 s for
  minutes: the display controller retained across network retries still held
  the host's video paused, so the new stream never received a first frame and
  ended with "no video traffic" (-100). Each new stream now asks the host to
  resume video first; the hidden viewer pauses it again after the first key
  frame. Seen on wmn → mm4 on 2026-10-02 (15 cycles from 23:13 to 23:20).
- Theme switching blocked the shell for 3–5 s on wmn (NixOS/KDE). A perf
  profile put 85% of the stall in `QQuickIconImage::load` →
  `QIconLoader::iconEngine`: every icon load or recolor searched the desktop
  icon theme across ~90 XDG data directories, although every DeskPort icon is
  a bundled resource. Linux builds now limit icon theme lookup to `:/icons`.
  With the real QML and wmn's icon paths a switch took ~960 ms in the UI
  harness, and ~10 ms without those paths. Opening pages pays the same cost.

Validation (macOS, isolated): multi-session (7, including the limit report and
its removal on disconnect, and quiet recovery versus explicit raising), UI
(26), transition window (30 transitions), session navigation, core workspace
and Qt adapter vectors, and an incremental full app compile. Live tray, KDE,
limit-hint rendering and real network-loss recovery remain acceptance checks.

### AppImage VA-API and driver-cohort follow-up — 2026-10-09

Reason: issue #5 also reported that an older AppImage VA-API interface could not
load Fedora's Mesa `__vaDriverInit_1_23`. Reproduction on a current AMD host found
a second blocker: private glibc could not satisfy the driver's newer TLS ABI.

AppImage viewer and host now use checksum-pinned libva 2.23.0. Modern-host GPU
drivers are selected with their dependency closure and matching libc/interpreter,
without replacing the private Qt/media/PipeWire stack. Old-system fallback and
canonical public executable identity remain required. Native packages, deployed
services, the shared core and stable main are unchanged by this candidate.

Validation: offline selection/fallback tests and synthetic AMD hardware encode /
decode probes passed during development. Final AppImage, isolated KWin lifecycle,
old-system runtime and affected-user Bazzite acceptance are separate release gates.
### Tray left click raises on the first click — 2026-10-11

Reason: on KDE Wayland a tray click often only flashed the taskbar entry, and on
macOS a window behind other applications took two clicks to show. Plasma hands
its xdg-activation token to the clicked application, but the remote window lives
on the SDL thread or in a session worker that never received it. The toggle also
read a covered window as shown and hid it on the first click.

The main process hands the shell's activation token to the SDL viewer (with the
session worker's `show` request where applicable), which activates its surface
through `xdg-activation-v1`. The left button still toggles, but hides the window
only when it is in front; a covered window is brought forward. Plasma takes focus
from the window just before the click arrives, so a window that lost focus within
the last 600 ms still counts as in front. A session worker reports its viewer's
focus to the main process, which hides that viewer the same way. Hiding keeps
sessions connected.

Validation: Linux build on pk4 and a live KDE check of raising and hiding from
the tray. macOS behavior follows from the shared logic and needs a packaged check.

### Browser stream at up to 60 fps — 2026-10-11

Reason: the browser page, gateway and host each capped the stream at 30 fps,
which made the web client visibly less smooth than the native viewer.

The page now asks for 60 fps. The host keeps H.264 within Level 5.2: the largest
frames (near 4096 × 2304) run slightly below 60 fps, and the bitrate grows with
the frame rate up to 40 Mbit/s. The size limit of about 4K is unchanged; going
beyond it would need HEVC for browsers.

### Delayed video pause for hidden viewers — 2026-10-11

Reason: hiding the viewer paused the host's video at once, so showing it again
waited for the encoder to resume and send a key frame. Repeated tray clicks
paused and resumed the stream each time.

The session now pauses the host's video only after the viewer has stayed hidden
for five seconds; showing it again within that time keeps the stream running.
Input is still released as soon as the viewer hides.

### Tray toggle while connecting — 2026-10-11

Reason: a session worker reported its viewer's focus only once streaming had
started, so while the connecting window was up the tray could only raise it,
never hide it.

The session now also reports focus while it pumps the connecting window, once
per change, so the tray hides and shows that window like the streaming viewer.

### Native clients take over browser sessions — 2026-10-11

Reason: a browser session must not evict a native client, but a native client
confirming a takeover was refused while a browser session held the host, since
the host admits no native stream during a browser reservation.

On a confirmed takeover the binding service now asks the browser supervisor to
end its session, display restoration included, and then takes over with a fresh
admission snapshot. The page's next request reports `taken-over` and the page
ends at once with that explanation.

### Takeover question inside the connecting window — 2026-10-11

Reason: the "Host already in use" takeover question was a native message box,
which on Windows looked unlike the rest of DeskPort.

While a connecting window is up, the question is now drawn in it with the app's
dark palette and two buttons; Escape and Return choose Cancel, as before. The
native box remains only as a fallback without a connecting window. On Windows,
the main process also lets a session worker take the foreground when the tray
shows its viewer, which otherwise opened behind other windows.

### Tray artwork derived from the application icon — 2026-10-12

Reason: the tray used an independently drawn monitor and kite, which no longer
matched the application icon's tilted display and terminal geometry.

The icon generator now derives black and white tray templates from the canonical
SVG, removing the tile and materials and strengthening strokes for status-bar
sizes. The lift line ends at the display's outer silhouette, and the tiny terminal
prompt is omitted to keep the screen interior clear. macOS retains native template
coloring; other platforms retain their foreground-contrast selection.

Validation: Qt SVG rendering at 16, 18, 22 and 24 points confirms matching black
and white silhouettes, and an isolated native preview verifies both backgrounds.
The Linux x86_64 Nix build passes. Deployed menu-bar and KDE panel appearance
remain separate acceptance checks.

### Tray toggle while connecting, completed — 2026-10-13

Reason: the tray still could not hide a connecting window. The longest
connecting phase, waiting for the stream to start, pumped the window directly
and never reported its focus or presentation; and while several desktops
connected at once, the wanted viewer was never recorded as visible because the
others were still hiding.

The connection wait now pumps through the session, which reports focus and
presentation, and the tray treats the wanted viewer as in front when its own
focus report says so.

### Experimental Seamless supervisor hardening — 2026-10-11

Reason: a helper could wait forever for readiness, accept late readiness during
shutdown, lose its original failure diagnostic, or forward unchecked window and
frame events. These gaps need fixing before remote admission or media is added.

The local supervisor now has a bounded startup deadline, restart-safe terminal
notifications, strict window/frame lifecycle validation, and bounded record
parsing. Shutdown withdraws readiness while preserving validated cleanup errors;
destroyed window IDs and stale frame sequences cannot re-enter the session.
Fake-sidecar regressions cover fragmented input, invalid events, failed exec,
timeout, cancellation, reentrant restart, and exact-child termination.

This changes only the local experimental process boundary. Desktop transport,
remote Seamless admission, native client windows, pixel transport, and input are
unchanged. Qt unit/core checks and sidecar compilation have been exercised;
locked Nix and native compositor/client acceptance remain required. See
[Seamless Mode](SEAMLESS_MODE.md) for verification limits and remaining milestones.

### Seamless local window diagnostics — 2026-10-12

Reason: the experimental host status exposed only a window count, leaving SSH
operators unable to inspect a window's metadata or distinguish a fresh capture
failure from an older successful frame.

The existing local `deskport status --json` route now includes bounded, sorted
`windowStates`: title/app ID, initial configure size, capture sequences, last
successful frame dimensions/hash and capture/save errors. Failed capture retains
the older frame with its original sequence; destruction, teardown and restart
remove stale state. No pixels, files or remote wire messages are added.

Validation: isolated macOS supervisor and local management-socket regressions.
This remains M1/M2 server work. Native Linux daemon/Wayland integration, external
terminal/no-GPU VPS compatibility, exclusive Seamless admission, remote transport,
client rendering, keyboard/pointer, resize and close remain acceptance work.
