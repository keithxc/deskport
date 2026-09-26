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
compatibility. See [the active checklist](../todo.md). Mobile auto-reconnect and
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
