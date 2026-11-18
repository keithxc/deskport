# Windows display broker installation lifecycle

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

This validates the installed signed-in display broker described in
[WINDOWS_DISPLAY_SERVICE.md](WINDOWS_DISPLAY_SERVICE.md). It does not add a
pre-login host, secure-desktop input or a new unattended service. Seamless is
outside this work.

## Repair barriers

The full NSIS installer uses `checked-exec.nsh` for every maintenance/driver
operation. `ExecWait` can fail to create the child without producing a defined
exit-code variable ([NSIS contract](https://nsis.sourceforge.io/Reference/ExecWait)).
A stale zero must not let uninstall delete binaries while
the service still refers to them. The macro clears the error flag, waits for
the child and maps a launch failure to a nonzero result. Existing operation
gates retain the files and stop. Driver reboot-required status 3010 retains its
existing meaning. Payload-copy errors and an unwritable uninstaller abort before
recovery/driver/service registration.

These are failure barriers, not transactional rollback of partially copied
binaries. Keep the exact previous installer and its hash before maintenance.
Repair by rerunning a complete compatible installer in the authorized window;
do not delete an ownership marker, identity store or journal to force success.
If the maintenance binary is quarantined, review security history and repair;
the retained uninstaller deliberately refuses to continue without that binary.

## Reproducible isolated checks

Resolve outputs using `scripts/build-paths.sh` or the shared build-path resolver.
Do not put outputs into the checkout. On the appropriate build host:

```sh
python3 tests/test-windows-install-lifecycle.py --output-directory "$SCM_TEST_OUTPUT"
python3 tests/test-windows-display-service.py --output-directory "$POLICY_TEST_OUTPUT"
# In the locked Windows cross-build shell:
python3 tests/test-windows-install-lifecycle.py \
  --output-directory "$NATIVE_FIXTURE_OUTPUT" --compile-nsis-fixture
# Real cross-built helpers, synthetic driver/client payload, compile/extract only:
python3 tests/test-windows-display-service-build.py \
  --helpers "$HELPERS" --display-helper "$DISPLAY_HELPER" \
  --output-directory "$NEW_PACKAGE_FIXTURE_OUTPUT"
nix build --no-link --max-jobs 1 --cores 4
```

The SCM test compiles the production `display-service-install.h` functions,
removing only platform includes, against deterministic Windows API doubles.
It covers register/update/stop/remove/reinstall, protected-path and foreign-service
rejection, startup/stop deadlines, retained repair ownership, registration/API
failures, exact DACL/restart configuration and release of SCM handles. It cannot
prove SCM scheduling, kernel permissions, driver behavior or actual preservation
of an installed identity. Existing policy tests execute guardian cancellation
priority and production DPI-journal retirement/error behavior.

`checked-exec-fixture.exe` is a separate ordinary-user native regression. Run it
with `/OUTPUT=<new result file under the resolved evidence directory>` from a
non-elevated shell in a reserved test account. It tests a
missing child with a stale zero, clearing a stale error flag, successful launch
and a child returning 17. Require exit code zero **and** the new PASS result file.
It touches no SCM, driver, DeskPort installation, registry or network endpoint.
Compilation alone is not a native test pass. Never execute or distribute the
separate synthetic full Setup fixture: its driver is intentionally invalid.

## Read-only preservation evidence

`tests/windows-install-audit.ps1` reads exact installed binary hashes, SCM
identity/startup/DACL/failure actions and hashes of viewer identity, certificate,
private key, host credentials/state and saved peers. It never emits their bytes,
addresses, command lines, clipboard data, user or machine name. Keep even hash
evidence private. The default locations follow the production Qt organization
and application name; explicit paths are available for dedicated test profiles.

```powershell
# $Evidence is a new task directory under ~/mygit/build/deskport.
# Quiesce the app first in the already-authorized maintenance window.
& .\tests\windows-install-audit.ps1 -OutputFile (Join-Path $Evidence 'before.json')
# After a maintenance operation, before relaunching the user app:
& .\tests\windows-install-audit.ps1 -Baseline (Join-Path $Evidence 'before.json') `
    -OutputFile (Join-Path $Evidence 'after.json')
```

Outputs must be new files. Comparison requires quiescent snapshots and existing
identity/credentials/peers in the baseline, preventing an empty test from passing
preservation. Initialize those with a synthetic test identity and test pairing.
The viewer's legacy `uniqueid` is generated lazily and can be absent in a valid
host-only profile. Its missing/present state is still compared; viewer credentials,
host credentials/state and saved peers must exist before comparison.
No script creates, resets, exports or removes a real identity. `-AllowRunning`
permits a read-only inventory but such a snapshot cannot be used for preservation
acceptance. Hash stability proves saved state retention, not successful mutual
authentication; reconnect the synthetic paired client separately.

The audit itself has portable PowerShell tests:

```powershell
pwsh -NoProfile -File tests/windows-install-audit-test.ps1 -OutputDirectory $NewAuditTestOutput
```

They parse and execute the real audit against synthetic files and CIM/registry
doubles: unchanged identity passes, changed pairing and empty baselines fail,
and an unreadable SYSTEM guardian still blocks a quiescent comparison. A running
inventory cannot become preservation acceptance. These checks do not validate
native Windows registry/provider behavior.

## Native launch and inventory checkpoint — 2026-10-17

The exact `45d42e29` checked-launch fixture passed on Windows 11 in the interactive
console at medium integrity (RID 8192): missing child with stale success, stale
error flag, successful child and exit 17. Both bounded runs returned zero and
wrote the expected result. A process-start watcher observed no UAC starts.

The unchanged audit source also ran at medium integrity and read all seven
installed executable hashes and the automatic LocalSystem/exact-ImagePath service.
This was a running inventory with six existing identity/peer hashes, not a
quiescent preservation comparison. It exposed the legitimate missing lazy viewer
ID; the comparator now accepts that state without creating an identity field.
Portable regressions require that an ID appearing/disappearing still fails strict
comparison. No real identity data was changed or copied into the audit packet.

The file-based `.ps1` invocation was blocked by the existing execution policy.
The verified source was then executed through the already-authorized PowerShell
command channel. Runner and coordinator policy snapshots stayed unchanged, no
`Bypass` setting was used, and Defender real-time protection remained enabled.
The original file-invocation failure remains in private evidence.

Installed GUI/host/helper/guardian/broker PIDs remained unchanged. No installation,
service restart, virtual-output change, stream, clipboard or input operation was
performed. Temporary scheduled tasks, runners and the exclusive Windows UI lock
were removed and verified. First installation, new-Setup upgrade/rollback,
uninstall/reinstall, missing-service UI and OS-session/restart matrices remain open.

## Exclusive native maintenance window

Use an existing idle disposable Windows guest with a verified restore snapshot,
or reserve the physical test machine exclusively with a recovery operator.
Record the fixed host/VM locks, task/PIDs/time, original processes, service and
task definitions, driver identity, topology, and rollback package hashes. Check
the other Windows/localization chats' current ownership; a free lock alone does
not transfer their session. Do not resume a saved VM without ownership of its
saved state. Keep only one active VM per host.

1. Verify the candidate and rollback full Setup hashes against retained package
   evidence. Both must contain the installed-broker architecture. A downgrade to
   an older per-sharing-elevation implementation is not the same rollback gate.
2. On the disposable guest, take the first-install snapshot before Setup. After
   installation verify exact ImagePath, LocalSystem, automatic startup, DACL,
   two restart actions plus NONE, ownership marker and ordinary-user sharing.
   Generate only synthetic identity/pairing, then quiesce and take the baseline.
3. Install the candidate over the baseline. Require Setup exit zero, complete
   binary hashes and the audit comparison before relaunch. Sharing must run as
   the ordinary console user without `consent.exe`. Verify a synthetic peer
   reconnects. Keep one uninterrupted candidate identity for all later cases.
4. Quiesce, compare again, run the retained compatible rollback installer, audit
   its complete payload and identity hashes, and reconnect. Upgrade back to the
   same candidate and repeat the checks. Do not mix unrelated package candidates.
5. Quiesce and uninstall. Verify the owned service/task/device/package are gone,
   no owned guardian/process/journal remains unresolved, and the identity store
   is retained. Reinstall the same candidate and compare against the baseline;
   confirm the synthetic peer reconnects without new pairing.
6. Use a disposable guest for missing/denied service, another console logon and
   three broker-crash cases. Ordinary-user failure must give a bounded repair
   error with no elevation fallback. Guardian recovery must finish independently.
   Observe the first two SCM restarts and the final stopped state after exhaustion;
   do not change/reset recovery policy to obtain a pass. Restore service state
   explicitly through the authorized administrator coordinator only after the
   guardian restored the desktop. Preserve failed journals for diagnosis.
7. Restore the starting candidate/configuration and verify all owned test tasks,
   fixtures, forwards, processes and locks are released. Shut down the owned guest
   and verify its PID/ports are gone; preserve its disk and restore snapshot.

If a stage fails, stop later destructive stages, retain logs/journals and use the
verified snapshot/compatible full installer. Do not disable UAC/Defender, change
service/desktop permissions, kill a guardian or delete user state for recovery.
Physical uninstall/reinstall is not authorized while a sharing session is active.

## Evidence ledger and remaining matrix — 2026-10-17

The previous native checkpoint uses clean candidates `d659dac9` and `6a33b4ee`;
the latest private package is `test-20261017-184047-6a33b4ee`. Its existing
upgrade/identity/Defender/UAC/DPI evidence is reused, not rerun or attributed to
this new installer change. Exact native details are in the display-service doc.

| Case | Existing native evidence | New isolated coverage | Still required |
| --- | --- | --- | --- |
| First full installation | Earlier installed broker, not a fresh lifecycle pass | Production create/start/configure and synthetic Setup compile/extract | Clean guest exact candidate install, driver and protection checks |
| Overwrite upgrade | Recorded upgrades; seven executable hashes and identity/peers retained at `6a33b4ee` | Production stop/update/start, same service retained | Exact new Setup plus quiescent before/after audit and paired reconnect |
| Rollback and re-upgrade | Not accepted | Same production update path in both directions | Compatible old/new package cycle on one synthetic identity |
| Uninstall/reinstall | Not accepted | Production remove/idempotency/recreate and launch-failure barrier | Actual SCM/task/driver removal, state retention and paired reconnect |
| Missing/quarantined helper | Standalone checked-launch fixture passed at medium integrity; no installed file removed | All 11 helper calls use checked launch | Failed-uninstall barrier with a missing/quarantined installed helper in disposable guest |
| Missing/denied service | Six native request/control denials; no full missing-service matrix | Authorization/framing and no-`runas` checks; API failures | Ordinary-user repair UI, bounded exit and no UAC for each native failure |
| Session switch/logoff | Not accepted for this matrix | Production authorization combinations and cancellation predicate | Two synthetic logons, release/restore and fresh admission |
| SCM finite restarts | Two configured retries exhausted during prior native DPI faults | Exact 5 s / 30 s / NONE configuration and API errors | Controlled three-crash timing on an initially recorded SCM failure count |
| Guardian recovery | Helper/broker death restored topology and nondefault DPI on prior candidate | Production cancellation/restore/journal-error regressions | Exact new Setup during active-lease maintenance and failed restoration |
| Cold boot, lock, secure desktop | No complete acceptance | Outside this task | Original unattended-Windows task retains these gates |

Installed lifecycle gates without recorded observations remain pending. A Linux
Nix build, API double, standalone NSIS launch fixture or a running automatic
broker cannot close them. Do not interpret the ledger as full lifecycle acceptance.
