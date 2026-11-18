# Client sleep, lock and wake recovery

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

The desktop shell and each media worker observe client lifecycle events. Linux
uses logind `PrepareForSleep` and the graphical session's `LockedHint`; Windows
uses a private message window with suspend/resume and WTS notifications for the
current session; macOS uses NSWorkspace sleep/wake/session notifications plus
screen lock notifications and the current console state. macOS's distributed
screen lock names and lock dictionary field are OS conventions rather than a
public AppKit lock API and require native acceptance on each supported OS.

This is client lifecycle handling, independent of unattended host services,
pre-login desktops and authentication. No host sharing switch, pairing record,
permissions or shared wire contract is changed.

## Connection and presentation intent

Sleep cancels the active transport, releases input and keeps a continuation only
for an authenticated, retryable lifecycle session with an existing resume token.
The continuation uses the existing multi-session network recovery path. It waits
until both sleep and lock gates are clear; its 30-second, five-retry budget starts
after wake/unlock. A network outage or offline host exhausts that bounded budget;
a refusal, revoked authorization or takeover cannot prompt or force admission.
Legacy or not-yet-admitted sessions do not gain permission to start on wake.
Sleep during display negotiation preserves retryability only when admission has
already issued a resume token. Explicit cancellation remains terminal, including
when it arrives while a continuation is being created.

Lock releases held keyboard/mouse input and capture immediately on the stream's
next event turn, blocks physical/controller input and stops clipboard dispatch.
Hidden/locked viewers retain the existing five-second video pause policy. Lock
alone preserves the transport. Wake/unlock does not present, raise, focus or
recapture a viewer. Explicit recall restores presentation. Each worker preserves
its own connection intent; disconnect and quit cancel pending continuations,
including backoff. Both worker failures and the legacy single-session UI keep
recovery errors in the background. No recovery intent is persisted across
application exit.

Repeated notifications are idempotent. Waking does not imply unlocking, nor does
unlocking imply waking. Existing admission/TLS checks and bounded network retries
remain authoritative after network or routing changes. Sleep never honors the
optional host-app quit-on-disconnect preference.

## Isolated checks and native acceptance queue

Run through the locked devShell with TMPDIR under the checkout build directory:

```
python3 tests/test-client-lifecycle.py
python3 tests/test-client-input-release.py
python3 tests/test-host-lifecycle.py --sessions
python3 tests/test-session-workers.py /absolute/path/to/deskport
nix build --no-link --max-jobs 1 --cores 4
```

Use a short TMPDIR under the build root: macOS local-socket paths are limited
in length, and a deeply nested checkout/test directory can prevent a worker's
IPC listener from starting. Pass it inside the devShell, for example
`nix develop -c env TMPDIR=/short/path/under/build python3 tests/test-host-lifecycle.py --sessions`;
setting it only before `nix develop` allows the shell to append another directory.

The event tests call the normalized boundary without sleeping/locking the host.
Linux additionally starts an owned logind fixture on a private D-Bus and routes
the production system-bus subscription to it, including inhibitor FD acquisition,
release/reacquisition and independent lock/wake events. It never calls the real
logind sleep API. macOS posts process-local NSWorkspace notifications; Windows
has a private-message-window injector ready for native execution. Input release
checks use synthetic gamepad state and recording C stubs, including timer stop
ordering and repeated neutralization.
The multi-session test runs real synthetic workers and checks two-session intent,
blocked opens, duplicate sleep, wake-while-locked, explicit disconnect, quiet
unlock, explicit recall, parked worker deadlines, silent failure and shutdown
while suspended. The UI regression checks silent backoff/failure in the legacy
single-session page. Real-worker IPC checks inject duplicate power/lock/wake
gates without creating a stream. These checks do not prove
native notification delivery, rendered frames, latency or physical input release.

Native acceptance must use an owned isolated desktop/VM and synthetic peers:

- Linux KDE Wayland: lid/suspend/resume and lock/unlock; verify inhibitor release,
  network-down wake and route/address change, and two independent workers.
- Windows: current logged-in client's lock/disconnect/unlock and suspend/resume;
  never mix this with pre-login/UAC/secure-desktop host acceptance.
- macOS: sleep/wake, screen lock/unlock and fast user switching; verify that the
  native lock conventions continue to match the supported macOS release.
- All: hold synthetic key/button input then hide/lock/sleep, confirm release;
  wake behind another app, keep focus there, cancel one session during recovery,
  deny/revoke host access, turn sharing off and quit during backoff. Confirm no
  takeover question, sharing request or authentication bypass. Resume only the
  remaining allowed sessions, with viewers hidden until explicit recall.

Never put a working physical computer to sleep for this test. Reserve shared
build/VM/GUI resources and verify owned teardown before and after native runs.

## Verification record — 2026-10-17

- macOS arm64: full client compilation in the locked devShell. QtTest totals
  (including setup/cleanup) were five lifecycle checks with native adapter
  notification injection, three input-release checks, nine multi-worker checks,
  six UI checks and 22 authenticated admission/endpoint regression checks; all
  passed. Two production worker processes
  completed 20 synthetic presentation switches, injected lifecycle gates,
  independent cancellation and IPC-loss exit with empty isolated settings.
- Linux x86_64: final-source `nix build --no-link` passed. Private-bus logind
  lifecycle, input release and multi-worker QtTest totals were five, three and
  nine, all passed. The packaged binary also passed the two-production-worker
  check with 20 presentation switches, duplicate lifecycle gates, independent
  cancellation and IPC-loss exit, without a server connection.
- Windows x64: full app cross-compilation passed, including PE file/product
  version `0.7.2`. The Windows private-message injector is not yet executed on
  native Windows; cross-compilation does not establish WTS/power delivery.
- An initial Linux multi-worker run could not open its local socket because the
  devShell extended TMPDIR beyond the socket path limit. Its failed log is
  retained separately; the short-path rerun above passed without a product change.
- Actual Linux/Windows compiler processes were sampled at CPU 0–3, nice 15,
  with at most four jobs; this includes daemon-launched Nix builders. Owned test
  workers, fixtures and build processes exited, and the shared CPU lock was
  handed to the next verified waiter. No GUI, VM or deployment was used.
- Native physical suspend/lock delivery, route changes during a real stream,
  rendered recovery, live held-key/controller release and focus remain untested.
- Core Git/Nix pins remain identical at `e1850b07c072f283669a64ec8f312ffc73ba1219`.
  No core behavior, network messages or translations changed.

Native API references: [Windows power registration](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-registersuspendresumenotification),
[Apple inactive-session notifications](https://developer.apple.com/documentation/appkit/nsworkspace/sessiondidresignactivenotification).
