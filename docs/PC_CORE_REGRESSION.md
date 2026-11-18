# PC core regression

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

## Current checkpoint (2026-10-17)

Baseline: desktop `b0c28182b8048497e32eadbae3561147d2c1969f`, shared core
`e1850b07c072f283669a64ec8f312ffc73ba1219`. No shared policy or wire change.

The native macOS acceptance fixture exposed a real adapter defect: the internal
MIME marker `application/x-deskport-clipboard` was registered directly as an
AppKit provider UTI. AppKit rejected it. The adapter now maps that marker to
`io.github.keithxc.deskport.clipboard-marker` and maps it back on enumeration.
The generic agent and peer protocol retain their existing format name.

The regression fails before the fix because the marker is missing. After the fix,
two unique named pasteboards run the production agents in both directions, with
external reader processes, Unicode text, decoded PNG pixels, chunked files,
Chinese/spaced names, hidden files, empty files/directories, repeat-read caching,
stable offers over two observation periods and stop-time marker revocation.
This uses AppKit and production clipboard code, with a queued local message relay;
it establishes neither cross-machine transport nor Finder/Dolphin compatibility.

The new synthetic fixture/tool checks and existing macOS named-board,
SDL dummy transition and navigation checks pass. The helper has no access to the
general clipboard in the named-board pass. Tests never connect to a personal host.

Validation of implementation commit `ac9ad388`:

| Check | Result and scope |
| --- | --- |
| macOS named-board production agents | Pass, both directions, actual PNG pixels and file content; local message relay |
| macOS clipboard TLS/synchronizer | Pass, five QtTest results, loopback certificates and synthetic text |
| macOS native change counter / SDL dummy transition / navigation | Pass; 30 transition cycles; no physical host/input |
| Native Linux `nix build --no-link` | Pass; resource-only derivation wrapper; four observed compilers at CPU 0–3 and nice 15 |
| Linux offscreen demand / transition / fixture tools | Pass; 30 transitions and 20 tool checks; native compositor cases skipped |
| Linux packaged clipboard helper | Pass, ordered acknowledgement, EOF and malformed-frame cleanup |
| Linux packaged session workers | Pass, two workers, 20 switches, independent cancellation and IPC-loss exit |

The first worker invocation failed before starting any worker because the long
`TMPDIR` plus Nix shell directory exceeded the AF_UNIX socket path limit. The
failure log is retained. Rerunning this standard-library-only check directly with
a short temporary path under the same project build root passed. No product
failure is inferred from that harness setup error. Package NAR/binary hashes,
source/core identities, compiler budget evidence and logs are retained privately.

Physical acceptance of the current candidate remains open:

| Client → host | Cases | Current-candidate hardware passes | Gate |
| --- | ---: | ---: | --- |
| KDE Wayland / AMD → macOS | 19 | 0 | Installed Linux build is an older candidate; activation and exclusive session needed |
| macOS → KDE Wayland / AMD | 19 | 0 | Same installed-source and session gate |
| Windows → macOS | 19 | 0 | Windows executable matches the prior private candidate; updated macOS candidate needed |
| macOS → Windows | 19 | 0 | Same macOS candidate gate; old DPI/input observations do not cover audio/clipboard |

Read-only installed identity probes confirmed that the Windows GUI binary matches
the recorded `test-20261017-184047-6a33b4ee` payload. The Linux process still
reports `issue5-kwin-20261009`; the installed macOS binary's hash was measured,
but its source/package relationship is not established. These are local inventory
observations, not release provenance or physical acceptance of this change.
Private device aliases, binary hashes and raw evidence remain under the build root.

## Reproducible fixture

Use the locked development shell. Resolve an output path and select a fresh run;
the generator refuses to overwrite an existing fixture directory.

```sh
run="$(bash scripts/build-paths.sh checkout)/pc-regression-YYYYMMDD/evidence"
mkdir -p "$run"
nix develop -c python3 scripts/pc-regression.py fixture "$run/fixture"
nix develop -c python3 scripts/pc-regression.py matrix-init \
  "$run/matrix.json" "$run/fixture/fixture.json"
```

Open `fixture/index.html` in an isolated synthetic window on the host test display.
It contains color bars, a moving counter, an input event log and an explicit audio
play button. No network request, clipboard read or input injection is made by the
page. Keyboard logs retain key codes/modifiers, not typed text. Save events before
closing. The counter demonstrates motion; browser animation timing is not a stream
frame-rate or end-to-end latency measurement. Browser-reserved shortcuts and held
key release require a separate native key-state probe.

`stereo.wav` is 48 kHz stereo PCM16: silence, left 440 Hz, right 880 Hz, both,
silence, left, right, both, one second per segment. Record the receiving audio
route into stereo PCM16 at the same rate, starting before playback. Trim unrelated
leading audio so the fixture starts within two seconds; retain the complete eight
seconds. Never record a personal application or microphone conversation.

```sh
python3 scripts/pc-regression.py audio "$run/fixture/stereo.wav" "$run/received.wav"
python3 scripts/pc-regression.py verify-copy \
  "$run/fixture/fixture.json" "$run/pasted-payload"
```

Audio analysis checks envelope alignment, active volume, silent-window leakage,
channel gain balance and expected tone energy. It returns nonzero for failure.
This is a short synthetic routing/integrity check. Listening quality, the selected
OS output route, mute behavior, audio/video sync and latency need their own evidence.
The negative controls reject silence, swapped/duplicated channels, imbalance,
wrong tones with matching envelopes and truncated recordings.

Copy the contents of `fixture/payload` through the session under test into a fresh
destination. `verify-copy` compares SHA-256, byte counts, the complete filename
set and directory set, including hidden/empty entries, and rejects links or extra
files. A local copy verifies the checker only. For clipboard image acceptance,
compare decoded pixels; an application may legitimately re-encode PNG bytes.

## Evidence ledger

Four directions each contain 19 separate cases: motion; stereo and playback route;
keys/shortcuts; vertical/horizontal scroll and trackpad; hide/focus release;
disconnect/reconnect/network recovery; text/image/files/directory/cancellation;
display and DPI restoration. Keep network fault injection scoped to the isolated
test session. Do not disable the host network or disturb another remote entrance.
Sleep policy and macOS multi-screen/install lifecycle are owned by separate tasks.
Seamless is excluded.

An observation JSON has `direction`, `case`, `layer` (`isolated`, `vm`, `hardware`),
`status` (`pass`, `fail`, `blocked`), `run_id`, `observed_at` (UTC ISO timestamp),
`fixture_sha256`, `method`, `expected`, `actual`, `identity` and `evidence`.
`identity.client` and `.host` each require `alias`, `os`, `device`,
`installed_version`, `source_sha` (40 hex), `core_sha` (40 hex), `package_sha256`
and `binary_sha256` (64 hex). Record OS version, compositor/GPU, input hardware,
output/capture route and display baseline in `device` or the evidence attachment.
Establish package → expected payload → installed binary correspondence before
using a source SHA; a displayed version alone is insufficient.

`evidence` is a nonempty list of `{ "path": "relative-or-absolute-file", "sha256":
"64-hex-digest" }`. Paths are relative to the observation JSON. Files must exist
and match at import; reports recheck the selected hardware observations' hashes.
Keep evidence on private storage and avoid personal names, addresses, credentials,
clipboard contents or screenshots of unrelated applications.

```sh
python3 scripts/pc-regression.py matrix-record "$run/matrix.json" "$run/observation.json"
python3 scripts/pc-regression.py matrix-report "$run/matrix.json" --run-id candidate-01
```

Reports require an explicit run ID, never inherit older passes, and count hardware
observations separately from VM/isolated checks. The same run/direction cannot
change installed identities silently. `complete` becomes true only for all 76
hardware passes under that run. The ledger enforces provenance bookkeeping;
operators still need to measure and truthfully describe each observation.

## Validation and resource ownership

Run `tests/test-pc-regression.py`, `tests/test-clipboard-demand.py`,
`tests/test-mac-clipboard.py`, `tests/test-transition-window.py` and
`tests/test-session-navigation.py` in the locked shell. Set `TMPDIR` and
`PYTHONPYCACHEPREFIX` below the checkout build path. The demand runner explicitly
selects the new native macOS case after its offscreen pass.

Linux validation requires native `nix build --no-link` on the exact source
snapshot, packaged `test-clipboard-helper.py` and `test-session-workers.py`, plus
the offscreen demand/transition checks. Reserve `linux-heavy-build.lock` by atomic
mkdir; use CPU 0–3, nice 15 and at most four build cores/one Nix job. Native GUI
checks similarly reserve the common device lock after verifying old-chat release.
Record thread ID, PID and time; clean and verify only owned resources on exit.
No application replacement, deployed-service restart, VM boot or managed activation
is needed for these isolated tests. A later current-candidate physical run needs
explicit activation and an exclusive device window.

Clamp the actual Nix builder as well as the CLI: a local resource-only
`overrideAttrs` can prepend `taskset -pc 0-3 $$` and `renice -n 15 -p $$` in
`preUnpack`, using absolute pinned util-linux paths. Retain that expression and
check the compiler processes' affinity/priority. CLI affinity alone is not evidence
about a daemon-launched builder. Keep source and product configuration unchanged.
For the packaged worker check, use an explicitly short project-build `TMPDIR`
directly with Python instead of inheriting Nix shell's longer temporary path.
