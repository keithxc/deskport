# Windows development checkpoint

The main branch contains the Windows x64 host and client integration.
The 0.6.3 release is being packaged from the current shared desktop baseline.
Dated checkpoints below describe their original samples, not blanket approval
or current blockers for later binaries.

The deliverable is a self-contained x64 installer (plus an optional portable
ZIP). End users do not need Nix, Qt, MinGW, npm, or a compiler. The maintained
cross-build recipes accept ordinary source archives from `winbuild/source-inputs`
or explicit `S_*` environment overrides; machine-specific `/nix/store` paths
are forbidden by the Windows-branch CI.

## Scope and platform impact

Windows-specific capture, driver ownership, display recovery, native OLE
clipboard and installer helpers coexist with shared changes to peer binding,
display-mode negotiation, session behavior and QML. Shared changes require
macOS and Linux regression review before merging; platform isolation must not
be assumed from Windows compilation.

## Existing evidence

Earlier Windows cross-build and constrained integration builds passed. Targeted UI, native
window transitions, H.264 decoded-frame and clipboard ownership checks passed.
Earlier VM runs exercised bidirectional streaming and clipboard transfers,
tray recall, input release, upgrades and identity retention. Some Linux input
checks used an isolated test-only XTest adapter, not native uinput.
The latest display helper preserved the physical primary through startup,
mode changes and restoration in targeted VM checks. These observations do
not certify the final combined installer on real GPU hardware.

## Windows 0.6.1 correction — 2026-09-24

The original 0.6.0 client payload reported 0.5.7 in both its PE resources and
runtime version. The package label was taken from source while the cached qmake
subproject still used the old generated version header. `build-app.sh` now runs
recursive qmake generation, and both packaging paths and the extracted installer
audit reject mismatched PE file/product versions. Run `tests/windows-cli.ps1`
with the final executable and `-Version 0.6.3` to check redirected runtime output
as well as PE metadata.

On the affected Windows machine, display startup failed after the recovery guard
was ready: GDI rejected positioning, then CCD returned error 87. Moving the owned
source while retaining its old desktop-image geometry produced an inconsistent
configuration. Invalidate only that derived desktop-image index, validate CCD's
resolved configuration, and verify all physical source modes after application.
The native display harness passed normal release, forced exit and restart after
crash, with dynamic sizes and mirror/extended/exclusive policy reuse. These
checks used the interactive elevated test session; they do not establish locked
session or unattended UAC acceptance.

## Windows antivirus notice

Updated 2026-09-24 after reviewing the Microsoft case and final 0.6.0 runtime results. Windows
**development/test builds may be detected and quarantined by Microsoft Defender**,
including after the application starts. The old sample received the decision below; it is not a guarantee for other builds.

The 0.5.6 r4 test package was uninstalled and reinstalled three times with an empty
installation directory, real-time and cloud protection enabled, and no path,
process or extension exclusions. Existing user settings were retained. Each
installation was followed by launching DeskPort and waiting two minutes. The
first two rounds passed runtime checks and on-demand scans; the third detected
`host/deskport-display.exe` as `Trojan:Win32/Bearfoos.A!ml` about eight seconds
after launch and subsequently quarantined it successfully. The affected helper
was removed and hosting components stopped, although the main app remained open.
Definitions were `1.459.343.0`, engine `1.1.26080.3`.

The exact helper sample (SHA-256
`d74533cf246c648311fd54ffedce19c7004a542422f6071d5a58f82ea1ff6567`) was submitted
for Microsoft review on 2026-09-23. Submission
`e28ba5f4-9624-46ec-938a-1e6430c89f89` now shows final determination **Not malware** and **No malware detected**.
The analyst states that the submitted files do not meet malware/PUA criteria and
that the detection was removed. The case header still reads In progress; the
per-file determination and analyst response are the verified result.

The final 0.6.0 installer (SHA-256
`78f4363c05cbaeb85cdfdab4e2ac7e3bd28d0eabac0a5acffdd9d258779c8322`)
passed three clean-install/startup/runtime/Defender-scan rounds with protection
enabled. It has not received a Microsoft determination. The developer submission
form offers malware/false-positive classifications; no detection was invented
to request proactive clearance.

If affected, keep Defender enabled and leave the file quarantined. Open
**Windows Security > Virus & threat protection > Protection history** to identify
the affected file and detection. Do not disable protection, add exclusions or
restore the file to bypass the detection. When reporting the issue, include the
DeskPort version, detection name and security intelligence version; redact
personal information from screenshots or logs. Wait for a reviewed replacement
and documented retesting with protection enabled before retrying the affected
build. A successful installation or a single clean scan does not resolve this
runtime detection. These results remain sample-specific.

## Outstanding acceptance

Final exact-package upgrade/uninstall/reinstall and state retention, abnormal
disconnect/reconnect, revocation/rebinding, complete current-UI streaming and
display stability, and cross-platform regression remain to be completed.
The historical detected sample received the sample-specific Microsoft result
recorded above. Testing with protection disabled or an exclusion does not
qualify a new release. Scan the exact final installer and extracted payload
with normal protection enabled; record native CLI results separately from
installation and live-stream acceptance.

VM testing was stopped after the user reported a VM crash and host stalls.
The cause has not been established. Do not restart heavy VM tests merely to
reproduce the old setup. Keep build outputs, private logs, screenshots and
credentials out of Git. Cross-build and packaging recipes are preserved under
`winbuild/scripts`; generated dependencies and binaries remain local.

## Host build order and offline delivery

Prepare the cached inputs first. Client recipes use `winbuild/source-inputs`
(or matching `S_*` overrides). Host curl/miniupnpc/minhook/onevpl archives,
prepared media libraries, signed VDD payload and immutable host assets are
currently required under `winbuild/full`; this is not yet a one-command bootstrap
from an empty cache. Enter `nix-shell winbuild/scripts/shell.nix` on Linux for
the repository-locked cross compiler, Qt host tools, npm and NSIS. The host recipe verifies the
vendored Sunshine archive, creates `winbuild/full/sunshine-prepared`, applies
the Windows and common DeskPort overlays, and builds into
`winbuild/full/host-build-prepared`:

```sh
nix-shell winbuild/scripts/shell.nix
winbuild/scripts/build-host-deps.sh
winbuild/scripts/build-host-media.sh
winbuild/scripts/build-host.sh
winbuild/scripts/build-app.sh
winbuild/scripts/build-maintenance.sh
winbuild/scripts/package-full.sh
```

Missing Windows-only source archives are downloaded during preparation from
fixed public commits and checked against `host-vendored-deps.json`. Cached
archives are rechecked on reuse. None of these downloads are performed by the
installer or installed application.

Use `DESKPORT_HOST_SOURCE_DIR` and `DESKPORT_HOST_BUILD_DIR` to inspect
separate prepared trees. `export-full-materials.py` records those actual trees
under the normal extraction paths and excludes obsolete `full/sunshine` and
`full/host-build` outputs.

The current Windows work also includes an SSH native CLI patch; its `--version`
check has passed. Native streaming, input, display, installer and upgrade
acceptance remain pending, so a successful cross-build or CLI check is not
native Windows acceptance.

## 2026-09-21 delivery requirements

Windows is a full desktop peer: it both connects to other PCs and hosts incoming
connections. The deliverable is one offline x64 installer containing the static
client, private host, display/recovery helpers, signed virtual-display driver and
notices. Build-time dependency downloads do not run on end-user machines. Windows
system components and GPU drivers remain OS/vendor-managed. `audit-package.py`
rejects external Qt, multimedia, crypto and compiler runtime DLL dependencies.

Mutual binding now waits for host readiness before announcing completion. The
Windows host build reapplies the current common network, encoder, input activity
and cadence overlays from verified source, instead of reusing an old patched
tree. Windows enables the same smart-streaming host switch. Actual performance
and bidirectional session behavior still require native acceptance.

Native CLI regressions can be run over SSH with `tests/windows-cli.ps1`. The
main window title bar follows the application's theme, including system appearance
notifications and window recall. Neither check substitutes for streaming/input
acceptance or proves virtual-display lifecycle parity.

### Current checks

The current candidate passed 98 mutual/client binding regressions, shared core
workspace fixtures, translations, ENet failure handling and smart-stream policy
tests. A Linux Nix build passed for the shared binding/CLI changes. On Windows 11
hardware, all four noninteractive version/help cases passed, and isolated Light
and Dark profiles both matched native DWM title-bar attributes and screenshots.
The client directory scan completed with Defender real-time protection enabled
and no detection records returned. These checks do not certify the final combined
installer, normal-protection operation of the rebuilt host, or bidirectional
streaming/input acceptance.

The final full candidate also passed installer/ZIP payload equivalence and PE
dependency audits. Its exact client binary matches the native title-bar test
binary; its bundled host reported `2026.906.222525` on Windows. Static Defender
scans of the extracted payload and installer completed with protection enabled
and no detection records returned, and the included driver catalog reported a
valid signature. A later installed/portable helper was nevertheless quarantined
by Defender as `Trojan:Win32/Bearfoos.A!ml`; that was a release blocker at
this checkpoint. See the later antivirus notice above for the submitted
sample determination and protected 0.6.0 retesting. Exact-package installation,
two-way real streaming/input and recovery remain pending; the Windows virtual
display is still sharing-scoped.

The full installer verifies all required DeskPort executables and driver files
immediately after extraction and again before completion. If a file is missing,
setup fails and leaves the uninstaller available for cleanup. Review Windows
Security protection history or another security product's quarantine record
before repairing the installation; protection must not be disabled.


## Physical Windows checkpoint — 2026-09-23

The Windows development branch now includes the current desktop UI and core
revision. Native CLI and light/dark title-bar attributes passed. An upgrade
from 0.5.0 to the 0.5.6 candidate preserved the tested identity/state files.
Cross-platform core, binding, lifecycle, UI and Linux package checks passed.

On a closed-lid AMD laptop, both an Apple tablet and a macOS desktop received
actual Windows lock-screen video. Tablet acceptance also exercised twelve
workspace changes, disconnect/reconnect, and takeover of a live desktop viewer;
the previous viewer reported that the session was taken over. The Apple client
needed finite display-mode negotiation to work with the Windows virtual driver.
These results do not establish password-entry, secure-desktop transitions,
pre-login operation, audio/clipboard parity, or reverse-stream input acceptance.

Fast native display enable/resize/release/crash loops exposed output-enumeration
and device-disable races. `tests/windows-display.ps1 -Rounds 10` exercises each
round's normal release, forced exit, and reuse of crash state, checking the
physical layout and disabled owned adapter between scenarios. Run it elevated
in the interactive session, with sharing stopped; session 0 is intentionally
rejected. Retained recovery snapshots and phase logs are administrator-only.
A failed round is a failed acceptance gate even when manual recovery succeeds.
The installed candidate passed all thirty scenarios with `-PortraitSwitch`,
which changes to 1080x1920 and back to 1920x1080 before release or forced exit.
Its helper and mode-list hashes matched the audited package; upgrading retained
the host certificate and key. The physical tablet then passed twelve orientation
changes and disconnect/reconnect against that installation.
Silent uninstall removed the application and its recovery task; reinstall
restored both while preserving the certificate, private key and peer database.
The physical tablet received frames again after reinstall, and all four native
CLI help/version checks passed. The Windows viewer also rendered the macOS
desktop. These checks still do not establish reverse-stream input or audio.

An elevated interactive user can open the ordinary desktop but receives access
denied opening the Winlogon desktop on this target. A controlled service boundary
is still needed for full secure-desktop support; changing authentication or UAC
policy is not a substitute for implementing that boundary.
The development-only [session-service probe](WINDOWS_SESSION_SERVICE.md) passed
thirteen native authentication, desktop-access, timeout and cleanup checks. It is
not packaged and is not yet connected to the streaming host or input path.

The installer allows only the supported application port families from the
local subnet across Windows network profiles. Existing explicit block rules
can still override those allows and require diagnosis. Silent installer errors
must return a nonzero exit code without waiting on an invisible dialog.

The machine's install-directory antivirus exclusion was configured by its owner.
Testing within that exclusion does not resolve public-distribution detection.
The remaining live matrix is still needed; this development checkpoint is not
a stable-release certification.

## Initial indirect-display positioning — 2026-09-23

A normally protected Windows 11 target returned `DISP_CHANGE_FAILED` from the
first GDI positioning call after enabling the owned virtual adapter. CCD still
reported both the physical screen and the owned active path. Returning early
at this point prevented host startup with `Cannot activate the DeskPort virtual
display`.

The helper now re-queries the active topology after either GDI result and uses
its existing CCD update when the requested owned mode/position is not already
established. It continues to verify the physical source modes and target paths,
and now also verifies the owned resolution and position after CCD application.
It does not save display database defaults or relax the recovery guard.

Native validation: `tests/windows-display.ps1 -Rounds 3 -PortraitSwitch` passed
all nine scenarios, including normal release, forced exit, and reuse of crash
state. Every scenario exercised the GDI-rejection fallback and restored the
physical baseline with the owned adapter disabled. This is separate from
installed-package first-frame/input acceptance.

API reference: [SetDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setdisplayconfig).

Installed private Setup acceptance: host start, stop/start, a macOS client's
first frame, keyboard, pointer and full screen passed. An Android client passed
first frame, text, touch and disconnect/reconnect with automatic sizing disabled.
The installed helper hash matched the tested helper. Defender remained enabled
with no new detection during this acceptance. Automatic Android sizing still
failed; a macOS discovery crash interrupted its reconnect check. These are
separate open compatibility gates, not passes implied by this activation fix.
