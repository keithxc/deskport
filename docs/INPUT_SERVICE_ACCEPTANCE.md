# Input and resident-service acceptance

Updated 2026-09-11. Implementation and isolated tests do not establish unattended
availability on a real KDE Wayland/macOS pair.

## Automated checks

Run with the repository's Qt dependencies and isolated state:

- `nix build`
- `python3 scripts/test-host-lifecycle.py --clipboard`: generated certificates,
  loopback TLS and an offscreen in-memory clipboard; 100 exchanges in each
  direction, Unicode/multiline text, echo suppression, conflict ordering,
  1 MiB boundary, unsupported formats, wrong pin, unknown client, exclusive
  session, disabled sharing and reconnect without replay. Also drives the production
  client synchronizer against an isolated SDL dummy clipboard.
- `python3 scripts/test-host-lifecycle.py --service`: generated startup files,
  ordinary quit versus explicit exit, failed-child recovery with stable
  credentials, and explicit stop cancelling recovery. Works on macOS/Linux.
- `python3 scripts/test-host-lifecycle.py`: macOS helper lifecycle regressions.
- `python3 scripts/test-host-lifecycle.py --ui`: settings and navigation checks.
- `python3 scripts/test-desktop-state.py`
- `python3 scripts/test-transition-window.py`: isolated hide/recall/disconnect.
- `python3 scripts/test-translations.py`

## Recorded verification (2026-09-11)

- macOS arm64 application compilation and Linux `nix build`: passed.
- Linux isolated CLI/package identity smoke: passed.
- Clipboard protocol and production SDL synchronizer: passed on macOS and Linux;
  100 protocol exchanges per direction, plus five SDL/Qt round trips.
- Resident-service suite: six QtTest results passed on each platform.
- Existing macOS helper lifecycle: 26 QtTest results passed; device binding and
  adaptive display suite: 13 passed.
- Linux QML/settings suite: 12 results passed; desktop preference/state suite: four.
- Isolated transition-window test: 30 cycles plus hide/recall/explicit disconnect.
- Seven translated catalogs and whitespace checks: passed.

These tests use synthetic data, generated credentials, dummy/offscreen displays and
fake host children. These automated tests did not replace or restart installed services.

## Native checkpoint

1. Keep an independent working remote entrance available while validating updates.
   Enable login startup and text sharing on both bound devices. Reconnect.
2. Copy synthetic Chinese, English, emoji and multiline samples in both directions.
   Confirm initial clipboards survive connection, simultaneous copies converge,
   and disconnect/reconnect does not replay offline contents. Check host clipboard
   permission errors, images/files and an oversized sample.
3. Repeat 100 pointer entry/exit cycles, including letterboxing, held Shift/Ctrl/
   Alt/Super and mouse drags. Confirm no stuck keys/buttons. Test Alt+Tab, Super,
   Super+Space, click-to-focus, explicit release and hide/recall. Pointer entry
   does not force focus on Wayland. Capture flags do not prove compositor support.
4. Close the main window, the viewer and the resize transition. Recall through
   the tray and a second application launch. Local host sharing must remain
   available. Disconnect the viewer independently; fully exit only from the tray.
5. Test one owned helper failure and verify automatic recovery. Explicit Stop
   sharing must prevent recovery, including after an application restart. Never
   kill or reconfigure an independently installed Sunshine service for this test.
6. At a controlled GUI logout/login, confirm the supervisor starts DeskPort in the
   background, bindings remain, both directions connect, and an abnormal process
   exit is recovered. A successful tray exit must stay exited until manually
   opened or a subsequent login. Confirm the refreshed startup path after updates.
7. Validate network interruption/recovery, sleep/wake and a controlled reboot
   while someone can recover the machine locally. macOS FileVault/pre-login,
   Linux login/session permissions and network loss can prevent availability.

## Limits

This release shares Unicode text, not PNG images or files. Clipboard permission
is independent from desktop input permission. The host grants the explicit
clipboard session to an approved device; it does not infer a session merely from
binding, and the channel is not a Sunshine protocol extension. Hidden connected
viewers keep clipboard sharing active. A full disconnect discards its baseline.

Startup files are refreshed for the next graphical login; writing them does not
move an already running GUI into launchd/systemd supervision. Existing installed
processes need a controlled update/login before native acceptance. No pre-login
service, automatic FileVault unlock, redundant network path or sleep policy is
configured by these changes.

## Installation follow-up (2026-09-11)

Both applications were updated and restarted at the user's request. Linux is
active under its user service. Xcode's GUI build signed the macOS update using
the original identity; deep signature and bundle dependency checks passed.
The macOS update was installed by renaming within /Applications, retaining
DeskPort-before-20260911.app as a rollback bundle. Moving the old bundle outside
that directory had failed; administrator authorization was not needed for the
successful same-directory replacement. The running macOS UI reports 0.1.1,
sharing enabled and the bound Linux peer online; its local sharing endpoint returns HTTP 200.
The independent Sunshine process remained running. Real keyboard/clipboard and
unattended reboot acceptance remain pending.
