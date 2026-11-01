# Installed Windows display service

Current source and evidence scope is indexed in the
[PC implementation and acceptance matrix](PC_SUPPORT_MATRIX.md). Automatic
service registration and signed-in sharing without repeated UAC are implemented;
current pre-login/lock/secure-desktop acceptance is incomplete.

## Scope

This candidate removes per-sharing UAC for an installed, signed-in physical
console session. The administrator-authorized full installer registers the
independent LocalSystem `DeskPortDisplayService` as an automatic-start SCM service.
It has no network listener, Sunshine configuration parser, pairing database,
application launcher, clipboard or input API. An idle service does not start the
GUI, create a user login or make the machine remotely accessible before login.

`HostManager`, `PeerManager`, identity and Sunshine still run as the interactive
user. Login startup remains the user's `HKCU` Run entry. Stop sharing and saved
startup opt-outs retain their existing meanings. Shared-core wire/display policy
is unchanged. The portable ZIP remains usable as a viewer; sharing requires the
full installation at the SCM-registered path. Missing/denied services produce one
repair error, without `runas` fallback or an elevation loop. Normal installer and
driver administrator authorization remains necessary.

## Trust boundary and lifetime

The broker accepts one versioned 88-byte acquisition request on a first-instance,
local-only, message-mode pipe. The sole operation contains a bounded UUID nonce,
not a PID, session ID, executable, command, path, environment or PnP identity.
Malformed/oversized/partial requests and unsupported operations fail closed. Pipe
I/O is cancellable/bounded; an acknowledgement avoids unbounded flushing. Only
one guardian is active at a time.

The service obtains the PID from the kernel and impersonates the caller. It
compares process/caller/console user and logon identities, requires medium or
higher integrity, the active console session and a live installed
`deskport-display.exe`. Installed executables and every parent path must be
administrator-owned, non-writable by ordinary users and free of reparse points.
SSH/session zero, other sessions/logons, foreign executables, anonymous and
low-integrity requests are rejected. The client grants only identification-level
SQOS, so even a pre-created rogue endpoint cannot borrow its authority. It
authenticates the server PID,
LocalSystem account and exact SCM ImagePath before requesting work. Users may
query SCM configuration/status but cannot start, stop or reconfigure the service.
Executable checks supplement restricted operations; they do not prove human
intent or defeat malware already executing inside the same user's process.

The service starts only the fixed sibling recovery guardian as SYSTEM in the
authorized console session, with a fixed environment and no cross-session handle
inheritance. The console logon receives only process query/synchronize access,
never a SYSTEM token or termination/duplication rights. Process creation times
prevent parent/broker PID-reuse substitution. The guardian validates its SYSTEM
context, SCM broker and installed helper. Local lease objects allow only SYSTEM
and that console logon. The broker stop event allows SYSTEM/administrators only
and rejects pre-existing names.

The environment contains canonical Windows/system-drive and common-data paths
resolved inside the broker, including `SystemDrive`. A fresh SYSTEM process
needs that variable to resolve its ProgramData known folder. Client environment,
profile and PATH values are never inherited. Native testing caught an initial
minimal-environment failure before any device mutation; the environment regression
covers relocated Windows and common-data directories.

Repeated native adapter recreation also exposed a successful GDI query returning
a detached 0x0 mode. The helper now uses the same enumerated-mode fallback on
recreation as at initial startup, preferring the negotiated dimensions before
positioning the owned output. It still verifies and restores the physical layout.

Existing bounded dimensions, exact owned-device identity, protected journals and
topology verification remain authoritative. Helper death/EOF, broker stop/death,
console-session change and session end release the lease and attempt restoration.
The guardian is outside a kill-on-close service job so broker death cannot kill
recovery. Service stop waits up to 60 seconds for it; installation refuses to
replace files while owned installed processes remain. Recovery failure retains
its protected journal. The boot-disable task still handles stale owned devices.

## Installation and upgrade

The full installer checks the service binary with other required payloads. Its
maintenance helper closes the GUI first, stops only the exact owned service,
waits for guardian cleanup and verifies executables are unused before replacement.
Registration installs/updates automatic startup, restricts the service DACL,
configures two bounded crash-restart delays, records ownership and waits for
readiness. Foreign same-name services are rejected. Errors retain the uninstaller
for repair. Uninstall removes only the exact owned service before existing driver
cleanup. Ordinary maintenance `stop` remains app-only; `stop-installation` is the
installer barrier. User identity/peers are not moved or promoted to SYSTEM.

## Owned virtual-output DPI

Independent virtual sessions (exclusive and extended desktop) apply the existing
shared scale of 1 or 2 as Windows system DPI of 100% or 200%. The helper does not
pretend a process DPI-awareness declaration changes system scaling. The guardian
resolves the installer-recorded PnP instance through the CCD adapter interface,
checks its hardware identity, and rejects foreign or shared physical sources.
Every write re-resolves the current source because adapter recreation retires
source IDs. A mirror retains physical scaling and its existing 1x downgrade.

A protected, flushed journal records the original virtual percentage, relative
DPI step, recommendation and user identity before the first change. Only bounded DPI operations and 1x/2x values
cross the lease mapping; no source ID, registry path or arbitrary percentage is
accepted from the helper. DPI calls impersonate the authenticated helper's user,
whose token remains open for recovery after helper/broker death. Protected file
I/O runs as the guardian. Normal disconnect restores the original percentage
after restoring the idle display mode. Windows persists a relative step and its
recommended percentage changes with resolution: restoring an absolute 125% at a
125%-recommended mode would otherwise become 100% at a 100%-recommended baseline.
Before retiring/recreating a source, recovery restores the original relative
step and retains the baseline for its replacement. Final cleanup restores that
step before disabling the VDD and restoring the physical topology. Failed
recovery retains its journal.

Windows' per-source DPI packets use private CCD request types -3/-4, outside the
documented SDK enum. Their layouts/ranges are checked and unsupported requests
fail with an error; there is no global/custom-DPI or registry-writing fallback.
The shared scale contract remains unchanged. New clients use the shared host
800x600 desktop floor after tuning; it swaps its axes in portrait (since
2026-11-01), so portrait workspaces may fall below Windows' unswapped 200%
threshold and receive the verified 1x downgrade below. Older clients can request a workspace too small for 200% Windows
UI DPI. The native adapter then applies and acknowledges 100% with the existing
optional warning field, rather than breaking the session or claiming 200%.
Windows can report the requested CCD step while clamping effective UI DPI; every
successful setter also checks a newly created per-monitor-aware window. A
read-only native probe independently checks both values.
Cross-compilation and arithmetic tests do not establish native DPI acceptance.

References: [Windows effective desktop minimum](https://learn.microsoft.com/en-us/windows/win32/uxguide/vis-layout),
[GetDpiForWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow).

## Isolated validation

Pass new output directories under the checkout build root:

```sh
python3 tests/test-windows-display-service.py --output-directory "$POLICY_TEST_OUTPUT"
# After Windows cross-building the actual helpers:
python3 tests/test-windows-display-service-build.py \
  --helpers "$WINDOWS_HELPERS" --display-helper "$DISPLAY_HELPER_EXE" \
  --output-directory "$NEW_FIXTURE_OUTPUT"
```

The policy suite executes production framing/authorization decisions, including
all 64 fact combinations, malformed nonces, unsupported operations, session zero,
disconnected/foreign consoles and invalid responses. It executes the guardian's
actual cancellation predicate/resize-enable branch with API doubles so stop/death
wins over pending resize. It checks installer ordering
and build/package/export wiring. Cross-build the real service, maintenance,
guardian and Qt helper; the second suite audits x64/static runtime imports and
compiles/extracts a synthetic NSIS fixture. That fixture contains invalid driver
and placeholder client/host data, is never executed and must never be installed or
distributed. A normal `nix build --no-link` checks the Linux consumer. These checks
do not emulate SCM, kernel token enforcement or native display/capture/input.

## Native checkpoint — 2026-10-17

The full Windows 11 x64 installation built from `d659dac9` passed the following
checks in an active physical console. The GUI, host and display helper ran at
medium integrity; only the installed broker and recovery guardian ran as SYSTEM.

- Twenty sharing start/stop cycles restored the original physical layout each
  time. Seven rounds of resize/release, forced helper exit and restart after
  crash passed all 21 scenarios, including portrait/dynamic resizing and all
  display reuse policies. No `consent.exe` start was observed during these tests.
- Forced broker death restored the layout while the helper input remained open;
  SCM restarted the automatic service independently.
- Foreign executables, unsupported operations, malformed nonces, partial and
  oversized requests, and medium-integrity service stop were rejected without
  device mutation. This is a subset of the complete denial matrix below.
- An existing macOS client displayed the synthetic animated Windows fixture and
  delivered pointer clicks. A second, direct macOS client delivered a synthetic
  keyboard string; focus loss released a held remote key before local key-up and
  stopped repetition. Keyboard acceptance through the nested control route was
  inconclusive. Audio, clipboard and latency acceptance were not performed.
- Installed executable hashes matched the full portable package. Existing peer
  records, certificate and private-key hashes survived upgrade unchanged.
  Defender custom scans passed with real-time protection enabled and no path or
  process exclusions. Native CLI help/version checks, payload/PE audits, targeted
  policy regressions and the native Linux Nix build passed.

These results cover the signed-in display broker. They do not establish cold-boot,
locked-session, secure-desktop or pre-login hosting. Private evidence retains
package identities and synthetic test results; personal machine details and raw
screenshots are excluded from public documentation.

## Native DPI checkpoint — 2026-10-17

The clean full package from `6a33b4ee` passed 24 scale cases over two rounds of
exclusive/extended desktops: same-pixel 100%/200% changes, 2136x1200 and portrait
1600x2848 at 200%, and the verified 100% fallback for a small legacy-client
request. Independent CCD and real-window DPI agreed. Four direct disconnects
from a large 200% workspace restored the deliberately non-default 125% baseline.
Mirroring preserved physical DPI and the virtual preference after detach.

Helper death and broker death restored that 125% preference across adapter
re-enablement and restored the physical layout. SCM preserved its failure count
across upgrades: only two automatic restart actions are configured. After the
fault-injection series exhausted them, an authorized admin coordinator restarted
the stopped broker after the guardian had independently recovered the desktop.
This is not evidence of unlimited automatic restart.

An existing macOS client showed animated synthetic video at 2044x1200 with actual
192 DPI (200%), delivered a pointer click and a synthetic keyboard string, and
reconnected. Viewer termination restored the original 100% after the existing
session cleanup grace. A smaller window from that older client used the verified
100% fallback. Physical windows remained at 96 DPI. Nested UI disconnect was not
accepted through that focus-control route; normal helper disconnects were tested
separately. This is not an IME, rapid typing, audio, clipboard or latency matrix.

Four additional ordinary-user sharing cycles, all seven installed executable
hashes, retained peer/identity hashes, four CLI help/version switches and Defender
custom scans passed. GUI/host/helper remained at medium integrity, with zero UAC
process starts observed. The source tree was clean after full packaging; PE and
Setup/ZIP-equivalence audits, production offset/window-DPI regressions, shared
core/translation checks and native Linux Nix compilation passed. Test viewers,
fixtures and temporary tasks were removed and temporary client preferences were
restored. The installed automatic broker and ordinary-user resident remain.

## Remaining native acceptance

Reserve a Windows 11 x64 target exclusively, with a local recovery operator and
dedicated test account. Record exact source/package hashes, original SCM/task/
device state, test owner, process/session IDs and verified cleanup. Keep Windows
credentials, private clipboard data and work applications out of evidence.

| Gate | Required observation |
| --- | --- |
| Install | One installation authorization; correct LocalSystem/automatic/ImagePath/DACL; identity and peers retained. |
| Cold boot | Broker idle/running without user login, GUI or display lease. This is service startup, not pre-login hosting. |
| Signed-in sharing | Non-elevated console user; 20 start/stop/resize cycles without UAC; real video/input and verified restoration. |
| Denials | Foreign executable/logon/session, anonymous, low IL, malformed/oversized requests, pipe precreation and unauthorized service control cause no device mutation. |
| Recovery | Owned helper/broker crash, SCM stop during preparation/resize, disconnect and console change leave no stuck input, orphan guardian or wrong-display fallback. |
| Lifecycle | Upgrade/rollback/uninstall/reinstall retain identity, preserve foreign services/tasks and wait for recovery before payload replacement. |
| Missing service | A sharing request fails once with a repair message, without UAC or a restart loop. Restore only test-owned state. |
| Later desktop work | Establish current existing-session lock-screen transitions, credential/UAC-desktop video and input, and fresh pre-login hosting against the exact installed package; identify missing behavior from those results. Historical lock-screen video is partial evidence only. |

The final row is outside this signed-in display-broker phase and is not accepted
as a supported end-to-end capability. This does not erase old partial lock-screen
video observations or classify every unrun case as missing code. The old probe only opened/closed a
desktop handle. Neither that, WGC window permission, an elevated process, a running
boot service nor a screenshot proves secure-desktop video, credential input or
pre-login hosting. BitLocker pre-boot authentication, automatic login, weakened
UAC/desktop permissions and software-SAS policy changes are excluded.

References: [interactive services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services),
[CreateProcessAsUser](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw),
[console-user tokens](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken),
[desktop duplication](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput).
