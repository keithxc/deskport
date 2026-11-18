# macOS display recovery and installation acceptance

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

This checklist qualifies the existing macOS implementation. It does not add a
new display policy, recovery daemon or transport. The disconnected-connector
fix `3b57f9a4` remains the production fix; no duplicate implementation is needed.

## Reproducible checks

Run from a dedicated checkout with the pinned core initialized. Keep output in
the checkout's resolved build directory and retain evidence separately from
scratch. No test below connects to a personal host. On a shared native Nix
builder, verify the actual builder/compiler CPU affinity, niceness and parallelism;
the daemon need not inherit scheduler settings applied to the Nix client. An
acceptance-only resource hook may constrain the builder without changing source
or package behavior.

```sh
run="$(bash scripts/build-paths.sh releases)/macos-lifecycle-TEST_ID"
mkdir -p "$run/evidence"
export TMPDIR="$run/evidence"
nix develop -c python3 tests/test-session-topology.py
nix develop -c python3 tests/test-core.py
nix develop -c python3 tests/test-host-lifecycle.py
nix develop -c python3 tests/test-host-lifecycle.py --binding
```

`tests/fixtures/macos-topologies.json` contains only invented displays: mixed
HiDPI with negative coordinates, an existing physical mirror with a disabled
panel, and three displays including a portrait panel. Each fixture runs through
all three production policies. The adapter must preserve the original journal
across repeated acquisition, resolve reconnected screens by stable UUID after
transient IDs change, leave a newly attached screen untouched, retain the journal
on rejected transactions, and restore from its persisted journal after a
simulated crash. Assertions check native pixel size, logical size, mirror source,
main display, origins and enabled state. The earlier placeholder, missing-mode
and queued-takeover regressions remain active. These are simulated WindowServer
operations under ASan/UBSan; they do not establish physical multi-screen support.

The production recovery-policy test additionally follows one persistent opt-in
through crash, pause, reopen, replacement/rollback process overlap, opt-out and
login-item removal. The display-contract CI workflow compiles and runs it under
ASan/UBSan alongside the topology regression. Launchd is injected, so this
establishes decision behavior,
not SMAppService registration, approval or recovery on a real installed Mac.

## Read-only topology and native extended workspace

First compile `tests/macos-display-probe.m` against Foundation,
ApplicationServices and AppKit. It uses the production snapshot adapter and
prints configuration only. Keep its UUID-bearing output private. Never capture
work applications or copy their contents into test evidence.

After confirming the GUI resource is idle and no other chat owns it, run the
independently signed candidate helper, never a replacement of the installed app:

```sh
python3 tests/test-macos-display-lifecycle.py \
  "$run/helper.noindex/deskport-display" --native \
  --thread-id CURRENT_THREAD_ID --output "$run/native-extended"
```

The harness atomically reserves `locks/local-ui.lock`, records thread/PID/time,
refuses an active deployed DeskPort stream, and clears only its own lock. It
forces isolated extended policy even if the caller inherited the real-topology
override. Four owned child processes exercise explicit lease release, EOF,
SIGTERM and SIGKILL. Each resizes in landscape/portrait and 1x/2x, then admits a
replacement lease while the old idle callback is queued. Physical topology must
remain byte-equivalent after sorting by UUID, and each child exit must remove
its workspace and return the complete topology to baseline. A SIGKILL case
checks WindowServer removal of the process-owned display; it does not prove a
physical-layout guardian restored a mirror/primary-only session. No capture,
input injection, installed service signal or physical-layout write is requested.

The harness records JSON summaries, responses and private helper logs. On a
failure, inspect evidence and physical topology before retrying. It terminates
only its own children; it never restores over a user's subsequent layout edit.

## Physical display window

Only begin with an explicitly available GUI, an agreed short recovery window,
local control and a known working rollback route. Save the real topology and
installed identity first. Use synthetic windows; close or move work applications
by user action. Do not unplug, sleep, power-cycle, change TCC or manipulate an
active personal session as an implicit part of testing.

For each policy, record the original physical main display, mirror groups,
enabled/disabled panels, modes, scale and positions. Check first frame and local
control, resize, disconnect and exact restoration. Separately exercise an
agreed hot-unplug/replug, reconnect with changed display IDs, host/helper crash,
and authenticated client takeover. Record the old client's end reason, new
client's actual mode/first frame/input release, journal retirement and restored
physical layout. Recovery pending is a distinct outcome from a successful new
workspace. Do not turn an isolated helper acknowledgment into streaming or
installed-build takeover acceptance.

## Signed package, upgrade and rollback window

Read [MACOS_PACKAGE.md](MACOS_PACKAGE.md) before packaging. Build through the
locked devShell and run signing preflight in that same execution session. Use
a fresh `releases.noindex/TEST_ID` candidate and the installed Developer ID team
and application identifiers. `release-macos.sh prepare` and `notarize` qualify
the complete bundle, ZIP and DMG; signed standalone helpers are not a completed
notarized application candidate.

Before a separately authorized activation:

1. Retain the exact old package, hashes, source identity, designated requirements
   of the outer app and nested executables, and current managed generation. Verify
   the old package is obtainable before entering the test window.
2. Retain a private, access-controlled backup of DeskPort's own state and the
   display journal if present. Do not read real clipboard data, migrate Moonlight
   state or upload pairing keys. Compare state hashes locally after each step.
3. Qualify candidate signatures, notarization/staples, extracted-ZIP Gatekeeper,
   bundle checks and isolated tests. Compare team/identifiers/requirements with
   the prior installed app. Matching identity is necessary but does not prove
   Screen Recording or Accessibility continuity.
4. The user activates the candidate through the agreed managed deployment route.
   Verify the actual running executable/source, then the existing capture/input
   grants, saved binding, sharing preference and recovery opt-in. Test permissions
   with synthetic content/input; never reset TCC or silently grant access.
5. Test recovery disabled, pending approval, approved, Pause and quit, next manual
   reopen, app crash, disabled login item and persistent opt-out. Verify only the
   console user's missing app restarts, with no duplicate live process. Approval
   and recent heartbeat are separate observations. SMAppService approval remains
   a user action. The helper must refuse execution outside its installed path.
6. The user restores the recorded prior managed generation/package, then checks
   the actual old executable, saved state, capture/input permission continuity,
   sharing and opt-in state again. Keep both artifacts until this passes.

Never directly replace `/Applications/DeskPort.app`, run `rebuild switch`,
register/unregister the deployed recovery service or stop/restart the installed
host while preparing this evidence. Packaging authorizes none of those actions.

## Evidence recorded on 2026-10-17

| Check | Result and boundary |
| --- | --- |
| Production topology, nine fixture/policy combinations | Passed ASan/UBSan, plus existing recovery and isolated-policy regressions |
| Shared core and Qt workspace adapter | Passed; pins unchanged |
| Host lifecycle | 34 passed, 5 Linux-only cases skipped; fake children and isolated loopback state |
| Production binding/session adapter | 172 passed; temporary identities and loopback fixtures |
| Recovery policy | 13 scenarios plus persistent lifecycle sequence passed ASan/UBSan |
| Independent native helpers | Built through locked devShell; same-session timestamped Developer ID preflight, signatures and strict verification passed |
| Native API availability / recovery executable path | Display API probe passed; recovery helper refused non-installed path with exit 78 |
| Physical topology | Read-only snapshot retained privately; no topology changes performed |
| Native four-ending harness | Not run: live deployed stream was busy, then another task owned the GUI lock; lock refusal observed |
| Full native Nix package | Passed regular `nix build --no-link` and an acceptance-only builder resource hook; actual compilers verified on CPUs 0–3, nice 15, at most four jobs |
| Full macOS application packaging/notarization | Pending: local free space below the workspace's 80 GiB minimum reserve |
| Physical mirror/hotplug/crash/takeover and install/upgrade/rollback/TCC/SMAppService | Pending an available and authorized activation window |

The first Nix build exposed that client-side taskset/nice did not constrain
daemon-owned compilers. The second build applied those limits inside the builder
before unpack/build and passed; its scheduling-only override and process
observations are retained with the private evidence. No daemon settings changed.

The signed helpers and private evidence are retained under this task's resolved
release directory. No installation, mynix update, deployed service restart,
permission change, VM launch or publication occurred. This item remains open.
