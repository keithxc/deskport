# PC implementation and acceptance matrix

Reviewed 2026-11-03 against source `3c945555f6f4c1b270178727df633059e826e32e` and pinned core
`10307719a4b139d6674eb96ebd013a8eec175b6d`. This is a source snapshot, not an installed version.
Seven tested task branches are integrated with their scoped acceptance records;
the remaining ten tasks retain explicit gates in [the integration ledger](PC_TASK_INTEGRATION.md).
Historical package and hardware results retain their exact source identities.

This snapshot keeps the previous application code. The core follow-up adds an
offline manual step for ordered fallback addresses and editing them while
connected; display arithmetic and wire contracts are unchanged. See the
[fallback address checkpoint](ROADMAP.md#ordered-fallback-addresses-and-live-edits--2026-10-30).
This review updates the source inventory, without advancing historical package,
hardware or hosted-CI evidence to the newer revision.

The GNOME display row was updated separately on 2026-10-26 against `21ab78c0`
and core `505ae0cc`; its versioned evidence is recorded below. Other rows retain
their original review scope.
The recovery follow-up `4f37c146` corrects the 50.1 fixture diagnosis and adds an
explicit Mutter 51 compositor patch; application installation alone does not apply it.
The 51.1 follow-up adds full-desktop scaling and native loopback reconnection
evidence, with fixture `1160e6dd`; it does not advance physical-hardware evidence.

This page is the current PC scope index. Dated release notes and roadmap entries
retain their original environment and result. A later implementation supersedes
an old limitation without retroactively completing its acceptance checklist.

## Reading the matrix

**Implemented** means a production path exists at the source snapshot. It does
not mean every package, OS, GPU, compositor or client combination passed.
**Pending acceptance** means the named observation has not been established for
the current exact package; it is not a finding that code is missing.
**Unsupported** identifies an explicit product/backend boundary. **Deferred**
identifies work intentionally outside the current alignment commitment.

Evidence below distinguishes physical hardware, isolated compositors, VM/fixture
results and hosted CI. Recorded passes quote the scope of existing repository
checkpoints; this documentation audit did not rerun their hardware experiments.
An unknown SHA or package hash is stated explicitly and limits reuse. No row
promotes a historical partial pass into current end-to-end acceptance.

## Native PC capabilities and remaining gates

| Feature | macOS arm64 | Linux x86_64 | Windows x64 | Remaining acceptance / explicit boundary |
| --- | --- | --- | --- | --- |
| Viewer, host and mutual binding | Implemented in full package | Implemented in native/Nix/AppImage host packages; Flatpak viewer only | Implemented in full installation; portable viewer does not install the sharing broker | Test exact installed endpoints in both directions; binding success is not video/input success. Client-only binding remains an explicit mode. See [binding](PEER_BINDING.md). |
| Adaptive workspace and physical layout | Owned virtual display; exclusive/mirror/extended policy adapters | KWin policies; guarded Mutter extension/primary-only/matching-mode mirror; guarded Hyprland policies; preconfigured X11 dummy extension at 1x | Owned VDD; exclusive/mirror/extended policies | Real hardware, compositor/version, hotplug, multi-monitor restoration and client resize/input mapping remain combination-specific. See [display policies](SESSION_DISPLAY.md), [GNOME evidence](#evidence-gnome-49-51) and [Linux backends](LINUX_ADAPTIVE_DISPLAY.md). |
| Workspace scale / DPI | Native 1x/2x backing | Native 1x/2x when compositor accepts | **Implemented and partially accepted:** owned VDD 100%/200%, observed window DPI and recovery; mirror preserves physical DPI | [windows-dpi](#evidence-windows-dpi) covers a private package, including old-client 1x fallback. Do not reopen DPI as missing code or apply it to physical outputs. |
| Background transport, hide/recall and input release | Implemented | Implemented; Wayland shortcut interception depends on compositor | Implemented | Hidden viewers release input; presented/background audio and clipboard ownership depend on session policy. Route-change recovery, sleep/wake, long sessions and real key/button release need exact-package checks. See [desktop UX](DESKTOP_UX.md) and [lifecycle](SESSION_LIFECYCLE.md). |
| Login startup and service lifetime | Opt-in per-user supervisor at graphical login | User service/autostart or foreground CLI in a suitable graphical session | HKCU login startup; full installer owns automatic SYSTEM **display broker**; GUI/host stay ordinary user | [windows-broker](#evidence-windows-broker) and [windows-dpi](#evidence-windows-dpi) record signed-in sharing without repeated UAC. Cold boot, logout/session change and full upgrade/rollback/uninstall matrix remain pending for current packages. Service auto-start is implemented; reboot acceptance is separate. |
| Pre-login, lock screen, credential/UAC desktop | No FileVault unlock or pre-login promise | No universal greeter/login-screen service; session capture permissions remain necessary | Old lock-screen video is a partial historical result; current signed-in broker does not supply a pre-login network host | [windows-old-lock-video](#evidence-windows-old-lock-video) is not current secure-desktop/input acceptance. Current locked-session transitions, credential input and pre-login hosting remain with the Windows lifecycle work; no service rewrite is authorized by this matrix. |
| Keyboard, IME and caret | Physical key routing and host IME; native caret adapter | Physical key routing and host IME; AT-SPI caret adapter | Physical key routing and host IME; UIA/Win32 caret adapter | Opt-in local IME is implemented on desktop and defaults off; candidates use a fixed top-left anchor. Native Wayland and end-to-end remote insertion remain pending. See [local text input](LOCAL_TEXT_INPUT.md). Caret geometry, composed text, shortcuts and mixed DPI require native editors/terminals/browser/IME tests; synthetic key delivery alone is insufficient. |
| Audio | Capture/session receive and host-playback controls implemented; OS permissions apply | Capture/session controls implemented; session audio stack required | Capture/session controls implemented; device/driver routing required | Host configuration enables audio capability; per-device session choice controls receiving sound. Do not reuse the old globally-disabled default. Actual output device, mute/focus behavior, playback quality, reconnect and A/V sync remain hardware acceptance. |
| Desktop clipboard | Native lazy pasteboard text/PNG/files | Wayland ext/wlr data-control, GNOME portal fallback and X11 Qt providers; compositor permission/capability required | Native OLE delayed provider and pipe transport | [clipboard](#evidence-clipboard) contains isolated and historical VM checks. Current three-platform clipboard matrix needs synthetic bilingual text/images/file bytes, inactive app, cancellation/reconnect and large-paste timing. No initial clipboard push; no permission bypass. See [limits](CLIPBOARD.md). |
| HDR / 10-bit | Managed virtual capture supports 8-bit H.264/HEVC; managed HDR unavailable | Inherited codec/HDR paths depend on capture, encoder, decoder and output | Inherited codec/HDR paths depend on capture, encoder, decoder and output | A setting, encoder flag or build is not HDR acceptance. No complete current PC HDR/color pipeline matrix is accepted. Browser is H.264 SDR. See [encoding audit](ENCODING_AUDIT.md). |
| Packaging and trust | Developer ID/notarization/stapling/extracted-ZIP Gatekeeper required | Nix/native/portable/AppImage; distro/session runtime requirements; Flatpak viewer only | Full offline Setup and portable viewer; exact-sample normal-protection checks required | Published package verification and a later source/private package are separate. New Windows samples still need protection-enabled checks; old detection clearance is sample-specific. No activation is implied. |
| Seamless | Deferred | Deferred | Deferred | Excluded from current PC alignment and acceptance commitments; no Seamless development, experiment or live test in this work. |

The later [installed iPad takeover checkpoint](#evidence-ipad-takeover) records
Linux/Windows video, synthetic text and ordinary reconnect. Its incomplete
installed-version provenance and the unexercised slow-restoration budget mismatch
remain explicit; it does not complete new-main or all-PC acceptance.

## Browser access is a separate client

All three full desktop host builds contain the browser gateway/media path;
Flatpak does not host. The browser implements authenticated HTTPS pairing,
H.264 SDR/Opus, adaptive size, one controller, physical keys and explicit composed
text/paste. This is not automatic bidirectional native clipboard sync or native
local-IME preedit forwarding. File transfer, HDR, HEVC/AV1 and gamepads remain
unsupported in the browser client. Display policy availability follows the host
backend; a browser option cannot add a capability the host did not advertise.
Browser UI now uses the shared 22-language catalog, with native-speaker review pending.

[browser](#evidence-browser) records Chrome fixture capture/audio and persistence;
[mac-browser](#evidence-mac-browser) records a partial macOS host checkpoint.
Windows host browser media/input, installed Linux compositor restoration, physical
Safari certificate/touch/audio and real IME/latency combinations remain pending.
The unsuccessful WebKitGTK setup completed no product assertions. See
[browser controls and validation](BROWSER_ACCESS.md).

## CI scope

The three baseline runs below were read through GitHub's run **and job** APIs on
2026-10-17. The workflow's name is not its execution platform. Linux build and
Windows source checks ran on `ubuntu-24.04`; display contracts ran on `macos-15`.
There is no native Windows runner, physical Linux/Windows GPU or personal-host
input job. The macOS job runs contracts, not distribution signing or physical UI.

`python3 .github/scripts/check-pc-support.py` checks evidence provenance, local references,
workflow/job/runner inventory and this generated evidence section. It runs in
Linux CI before the build and needs only Python's standard library. It never
launches a compositor, VM, installed application or remote connection.

The local [audit verification](PC_VERIFICATION_2026-10-17.md) records this task's
Linux build/CLI result and an isolated long-temporary-path setup failure. It does
not extend the historical GitHub runs or establish physical streaming acceptance.

## Evidence register

The machine-readable source is [pc-support-evidence.json](pc-support-evidence.json).
Regenerate this section with `python3 .github/scripts/check-pc-support.py --write` after
reviewing evidence. Do not fill unknown provenance with the current main SHA.
Keep historical CI rows on their tested SHA when advancing the implementation
snapshot; append new exact-run results instead of relabeling old passes.

<!-- pc-support-evidence:start -->

Generated from the reviewed register; recorded results are scoped to each row.

### Evidence windows-dpi

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `6a33b4ee4999ee23c40cc39eb15dd13f80e6f031` |
| Package / installed identity | DeskPort-0.7.2-windows-x64-setup-full-test-20261017-184047-6a33b4ee.exe. Reported installed 0.7.2 / test-20261017-184047-6a33b4ee; installation not reread by this audit |
| Package SHA-256 | `dace6daa2b6478097f5596dd0d18b4dd85a9f980a6d0ec801c0a6ead13675a9b` |
| Environment | physical-hardware: Windows 11 x64 console; owned VDD; existing macOS viewer; synthetic animated desktop |
| Feature / actual result | VDD 100%/200%, disconnect/crash restoration, signed-in sharing without repeated UAC. Recorded pass: 24 scale cases, four 125% restorations, mirror preservation, pointer/keyboard, reconnect, four sharing cycles |
| Limits / pending | Partial checkpoint, not a complete IME/audio/clipboard/latency or locked/pre-login matrix. Package ID and Setup SHA-256 were cross-checked against the existing private asset-verification record; no payload was installed or executed in this audit. Main SHA is not an installed version. |
| References | [source 1](../docs/WINDOWS_DISPLAY_SERVICE.md#native-dpi-checkpoint--2026-10-17) |

### Evidence windows-broker

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `d659dac9ecf3e3acbdbe0c8909963ab60d114b59` |
| Package / installed identity | Full Windows x64 candidate from d659dac9. Reported installed candidate at that checkpoint |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: Windows 11 x64 signed-in console, medium-integrity app/host/helper, SYSTEM broker/guardian |
| Feature / actual result | Automatic SCM display broker, signed-in leases, identity retention, recovery and request denials. Recorded pass: 20 sharing cycles, 21 display scenarios, broker death recovery, six denial cases, direct macOS video/pointer/keyboard |
| Limits / pending | Not cold-boot verification, complete denial/lifecycle coverage, or a pre-login network host. No secure-desktop or credential-input acceptance. |
| References | [source 1](../docs/WINDOWS_DISPLAY_SERVICE.md#native-checkpoint--2026-10-17) |

### Evidence windows-old-lock-video

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-23; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.5.6 development candidate; exact package/hash absent from public summary. Historical installation; superseded by later candidates |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: Closed-lid Windows AMD laptop, Apple tablet/macOS viewers |
| Feature / actual result | Lock-screen video, twelve orientation changes, display recovery. Recorded partial pass: actual lock-screen frames and display/reconnect/takeover observations |
| Limits / pending | Incomplete provenance; video alone does not establish credential input, secure-desktop transitions, pre-login hosting or current broker behavior. |
| References | [source 1](../docs/WINDOWS_DEVELOPMENT.md#physical-windows-checkpoint--2026-09-23) |

### Evidence linux-compositors

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-16; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.3.6 Linux preview; exact package/hash absent from public summary. Isolated fixture only; installed desktop untouched |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-compositor: KWin 6.6.6 with permission checks; Mutter 50.4; private D-Bus/Wayland/PipeWire |
| Feature / actual result | Virtual output resize/scale, software capture, EOF/crash cleanup. Recorded pass: twelve mode/scale changes per backend and capture probes |
| Limits / pending | Historical incomplete provenance. Does not qualify current compositor versions, physical GPU encoding, input or hotplug. |
| References | [source 1](../docs/LINUX_ADAPTIVE_DISPLAY.md#validation) |

### Evidence gnome-49-51

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-26; `21ab78c0985d6535f797f74532a09cd224d4bcfd` |
| Package / installed identity | Managed Nix build gnome511-reconnect03; production recovery 21ab78c0, fixture 1160e6dd and shared core 505ae0cc. Explicit Mutter source patch loaded only in disposable guests; no published package or user activation |
| Package SHA-256 | Unknown: No single distribution package was produced; the linked checkpoint records the tested application/display launcher SHA-256 values and exact Nix source snapshot. |
| Environment | virtual-machine: Private GNOME 49.4/50.4 headless sessions; Ubuntu 26.04 / GNOME 50.1 and Ubuntu 26.10 / GNOME 51.0 full QEMU/KVM desktops; upstream Shell/Mutter 51.1 installed only in a disposable Ubuntu 26.10 guest. Software graphics throughout. |
| Feature / actual result | Guarded display policies, shared workspace scaling, software capture, PipeWire loss, physical restoration and native client reconnection. PASS: 28/28 headless checks each on 49.4 and 50.4; 30/30 full-desktop checks on 50.1; 54/54 on patched 51.0. Patched 51.1 passes 72 desktop checks, including nine scale/input configurations with three independent GTK windows each, plus eight native GameStream recovery cases across all policies and supported 150%/80% desktop adjustments. Same client/supervisor, fresh rendered frames/capture, unchanged compositor and restored local layout are required. |
| Limits / pending | GNOME 51 PipeWire-loss recovery requires the explicit Mutter patch; DeskPort installation does not patch the compositor. Mirror requires matching physical modes/scales. Native reconnect uses guest loopback with seeded disposable trust; pairing UI, physical GPU/network/input, HDR/audio/clipboard and latency remain unqualified. The invalid 0.75 fixture failure is retained separately from the corrected eight-case acceptance. |
| References | [source 1](../docs/GNOME_DISPLAY.md#compatibility-checkpoint--2026-10-26) · [source 2](../docs/GNOME_DISPLAY.md#gnome-511-full-desktop-and-native-recovery-follow-up) |

### Evidence linux-hyprland

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-03; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Development Nix package; exact package/hash absent from public summary. Isolated compositor; no Omarchy installation acceptance |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-compositor: Real isolated Hyprland compositor, WLR capture and software H.264 |
| Feature / actual result | Owned headless output, three pixel/scale modes, target-loss rejection and cleanup. Recorded pass: fixture and real-compositor checks; disconnect/EOF/SIGKILL/group SIGTERM cleanup |
| Limits / pending | Not a physical Omarchy/GPU/input/audio/reboot result. Mirroring/physical-disable policy is not offered. |
| References | [source 1](../docs/CLI.md#isolated-verification) · [source 2](../docs/ROADMAP.md#ssh-host-management-and-omarchy--2026-10-03) |

### Evidence clipboard

| Field | Record |
| --- | --- |
| Date / tested source | 2026-09-15; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | 0.3.2 protocol checkpoint; Windows OLE follow-up 2026-09-20. Named macOS pasteboards/private compositor/VM fixtures only |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-fixture: Synthetic Unicode, PNG and file trees; macOS, KWin and historical Windows VM OLE follow-up |
| Feature / actual result | On-demand desktop text/images/files; source validation and cancellation. Recorded pass: protocol exchanges, lazy reads and isolated native providers; partial Windows VM transfers |
| Limits / pending | Not current three-platform installed clipboard acceptance. Browser explicit text/paste is separate. Historical summaries lack full package provenance. |
| References | [source 1](../docs/CLIPBOARD.md#validation-and-manual-acceptance) · [source 2](../docs/ROADMAP.md#display-recovery-incident-follow-up) |

### Evidence browser

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-05; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Browser integration fixture; exact source/package absent from summary. Disposable browser profiles and native Linux fixture; no personal installation |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | isolated-fixture: Google Chrome 154; Linux Xvfb/PulseAudio capture; native input disabled |
| Feature / actual result | HTTPS pairing persistence, H.264/Opus, reconnect and revocation. Recorded pass: changing 720p30 frames, nonzero decoded audio, restart persistence and revocation |
| Limits / pending | Does not qualify Safari/WebKit, Windows host WebRTC, physical audio/touch/IME or compositor restoration. Later up-to-60-fps implementation is not this 30-fps checkpoint. |
| References | [source 1](../docs/BROWSER_ACCESS.md#implementation-and-validation) |

### Evidence mac-browser

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-08; Unknown: The cited historical summary does not identify a complete source SHA. |
| Package / installed identity | Isolated macOS host instance; exact binary/hash absent from summary. Used installed display policy with isolated instance; not general release acceptance |
| Package SHA-256 | Unknown: Exact package hash is not available in the cited public checkpoint. |
| Environment | physical-hardware: macOS host and headless Chrome client over private VPN |
| Feature / actual result | Browser H.264/Opus, adaptive size and continued frame reception. Recorded partial pass: changing video/audio, portrait/ultrawide/content size/fullscreen |
| Limits / pending | Incomplete provenance; no physical Safari, actual audio playback quality, input/IME or latency qualification. |
| References | [source 1](../docs/BROWSER_ACCESS.md#macos-host-checkpoint--2026-10-08) |

### Evidence ci-linux

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Linux build job on ubuntu-24.04 |
| Feature / actual result | Nix Linux package, CLI/desktop-entry smoke, isolated contracts and regressions. Observed completed/success through GitHub run and job API |
| Limits / pending | Hosted Ubuntu runner; no physical AMD/KDE session, native GPU/input/audio or distro installation acceptance. |
| References | [source 1](../.github/workflows/build.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059457) |

### Evidence ci-macos

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Display control contracts job on macos-15 |
| Feature / actual result | Core, binding, lifecycle and topology contracts. Observed completed/success through GitHub run and job API |
| Limits / pending | Hosted macOS runner; no signed/notarized app package, physical display/input, installation or latency acceptance. |
| References | [source 1](../.github/workflows/core.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059520) |

### Evidence ci-windows

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `b0c28182b8048497e32eadbae3561147d2c1969f` |
| Package / installed identity | CI source checkout at baseline; no installed package. No deployment |
| Package SHA-256 | Unknown: No package artifact is produced by this CI evidence row. |
| Environment | hosted-ci: Windows source checks job on ubuntu-24.04 |
| Feature / actual result | Build-script syntax, source/network/smart-stream/translation checks. Observed completed/success through GitHub run and job API |
| Limits / pending | Linux runner; no Windows compilation, installation, SCM, native Windows UI or GPU acceptance. |
| References | [source 1](../.github/workflows/windows-branch.yml) · [source 2](https://github.com/keithxc/deskport/actions/runs/39028059456) |

### Evidence native-audit

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `be315a1a788b21defb782104e3009be7094b43b1` |
| Package / installed identity | Nix output /nix/store/cclirqg1svs2qqaz4r75cavm0z4g98n0-deskport-0.7.2; hash is the uncompressed output NAR, not a release installer or full closure. Not installed or activated; isolated CLI processes only |
| Package SHA-256 | `4acdb72971703c2f05e5f752681c968714653fba72d92148a2c1c7a9f8b2814d` |
| Environment | isolated-fixture: Native NixOS x86_64; builder CPU 0-3 / nice 15 / four cores; private HOME/XDG and loopback TLS; no sharing or input |
| Feature / actual result | Evidence validation, full package build, package identity and isolated local CLI. Observed pass: constrained Nix build, version/help/desktop identity/icon, short-TMPDIR CLI enrollment/config/ownership/revocation/SIGTERM; initial 131-byte socket-path setup was rejected before product assertions |
| Limits / pending | No GPU, physical streaming/input/audio/clipboard/IME/HDR or native Windows/macOS acceptance; Seamless not run. Same binary passed after using a short isolated TMPDIR inside the build root; long TMPDIR support remains unaccepted. |
| References | [source 1](../docs/PC_VERIFICATION_2026-10-17.md#observed-results) · [source 2](../tests/test-host-cli.py) |

### Evidence ipad-takeover

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; Unknown: Documentation commit a8174c968eff7de7b962396279720141063da193 records the observation but does not identify all installed host/client source revisions. |
| Package / installed identity | Existing installed Linux KDE Wayland and Windows 11 builds; exact package/version IDs absent from this public checkpoint. Existing host installations and native iPad/macOS clients; no deployed binary or service replaced/restarted |
| Package SHA-256 | Unknown: The exact installed package hashes are not recorded in this public checkpoint. |
| Environment | physical-hardware: Native iPad takes over active macOS viewers on installed Linux KDE Wayland over Tailscale and Windows 11 over Wi-Fi LAN |
| Feature / actual result | Normal takeover, HEVC video, synthetic text, disconnect/reconnect and Linux virtual-output cleanup/physical-layout preservation. Recorded partial pass: old viewer processes exited, HEVC reached iPad, synthetic text reached the remote app, reconnect restored video; observed Linux physical positions/scales/enablement preserved |
| Limits / pending | Not new-main acceptance, Windows overlay networking, cold boot, secure desktop, audio, clipboard or long recovery. Package provenance is incomplete. The 10.5-second admission barrier versus 20/25-second helper/settling budgets remains an unexercised slow-path risk, not fixed by the fast successful takeover. |
| References | [source 1](../docs/ROADMAP.md#native-ipad-takeover-of-linux-and-windows--2026-10-17) |

### Evidence hyprland-policies

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `559c5dcb13134834745b20911e5127ee6d3d6699` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | isolated-compositor: Hyprland Lua and legacy test sessions |
| Feature / actual result | Three display policies and guardian recovery. 68 IPC checks, HostManager/core, Nix and native WLR/H.264 probes passed |
| Limits / pending | Physical panels, client frame delivery, audio/input and latency remain unaccepted. |
| References | [source 1](../docs/HYPRLAND_DISPLAY.md) |

### Evidence x11-workspace

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `72eb11a4b6dcce766b2b6b51f5566d325d5c618e` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | isolated-fixture: Preconfigured dummy Xorg server and packaged host |
| Feature / actual result | Owned software output leases, capture and recovery. 44 lifecycle and 172 binding checks, Nix, Xorg H.264 capture and crash restoration passed |
| Limits / pending | DUMMY1-15 only; extension at 1x. No 2x, physical topology policies or ordinary GPU qualification. |
| References | [source 1](../docs/X11_ADAPTIVE_DISPLAY.md) |

### Evidence wayland-clipboard-fallback

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `4a8f83e3b8854e7e9dc8e2a603b5e37b9e04fc38` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | isolated-compositor: GNOME portal and Hyprland data-control fixtures |
| Feature / actual result | Clipboard fallbacks and capability negotiation. 19 portal and 8 demand checks, native isolated paths and Nix passed |
| Limits / pending | Physical machine and file-manager paste, wlr-v1-only environment remain unaccepted. |
| References | [source 1](../docs/CLIPBOARD.md) |

### Evidence desktop-local-ime

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `abbe167ed609030f429989a9039fe07e3f88d309` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | isolated-fixture: Native macOS/Windows IME and Linux X11 test windows |
| Feature / actual result | Opt-in local composition, commit, cancellation and focus lifecycle. Native macOS/Windows Pinyin, X11 CJK composition and platform builds passed |
| Limits / pending | Default off; fixed top-left candidates. Native Wayland, packaged HiDPI/fullscreen and real remote insertion remain unaccepted. |
| References | [source 1](../docs/LOCAL_TEXT_INPUT.md) |

### Evidence web-localization

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `8e01e23425b3f1b1f50e623d8b99e29bcc6f3518` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | isolated-fixture: Offscreen Chromium, HTTPS and RTC fixtures |
| Feature / actual result | Shared browser UI localization and RTL. 126 originals across 22 locales, 71 browser checks, 67 RTC and 165 HTTPS checks, Nix passed |
| Limits / pending | Native-speaker review, other browsers, physical font/rendering and real streaming remain separate. |
| References | [source 1](../docs/BROWSER_ACCESS.md) |

### Evidence linux-package-lifecycle

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-17; `f60841210caef5ea8a21261b37104a4919387757` |
| Package / installed identity | Task-specific build and fixtures recorded in the referenced report. No deployed application activated by integration |
| Package SHA-256 | Unknown: This row summarizes separately identified fixture/build outputs, not one distribution archive. |
| Environment | virtual-machine: Retained distro desktop VM overlays and synthetic media fixture |
| Feature / actual result | Pinned VA runtime, launcher isolation and package lifecycle. Ubuntu candidate GUI/API control, final Nix, 26 synthetic AV assertions and 28 VM teardowns passed |
| Limits / pending | Old published DEB rollback and GNOME AppImage capture failures remain; KWin 6.3 unsupported. Physical GPUs and OS input/clipboard not qualified. |
| References | [source 1](../docs/LINUX_DESKTOP_ACCEPTANCE.md) |

### Evidence integration-regressions

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-18; `49cd7a1a0134c6b428b99026750a812a5be2c88b` |
| Package / installed identity | Native Nix acceptance output; source snapshots identified in the integration ledger. Not installed or activated |
| Package SHA-256 | Unknown: Nix store output and source hashes are recorded, not a distribution archive. |
| Environment | isolated-fixture: Native Linux build host, CPU 0-3/nice 15/max four jobs; isolated Qt and private-bus fixtures |
| Feature / actual result | Combined core and production adapter regression checks. Thirteen groups passed: core/Qt/translations/preferences, 45 host lifecycle, 172 binding, Hyprland, 8 clipboard demand (3 platform skips), 19 private-bus portal, IME, VA runtime and provenance |
| Limits / pending | No deployed application, physical GUI, real host stream or OS input used. |
| References | [source 1](../docs/PC_TASK_INTEGRATION.md#integration-verification) |

### Evidence integration-cards-build

| Field | Record |
| --- | --- |
| Date / tested source | 2026-10-18; `ddf6e6eaacb806245ade99abdb425d4e2cbfab95` |
| Package / installed identity | Native Nix acceptance output; source snapshots identified in the integration ledger. Not installed or activated |
| Package SHA-256 | Unknown: Nix store output and source hashes are recorded, not a distribution archive. |
| Environment | isolated-fixture: Native Linux build host, CPU 0-3/nice 15/max four jobs; isolated Qt and private-bus fixtures |
| Feature / actual result | Square cards and complete native Linux build. 27 UI cases, resource-constrained Sunshine plus DeskPort Nix build, packaged CLI, 1611 tracked source blobs and core NAR checks passed |
| Limits / pending | Resource-only exported-flake hooks; initial unconstrained dependency attempt cancelled and retained. The prior regression snapshot differs only in PcView.qml plus its UI tests and documentation. No deployed/native end-to-end acceptance inferred. |
| References | [source 1](../docs/PC_TASK_INTEGRATION.md#integration-verification) |
<!-- pc-support-evidence:end -->

## Updating after other PC work merges

This audit completes the documentation item only. It does not mark the other
sixteen PC work items complete. On integration, review each contributing commit
and update the source/core snapshot together with the affected feature row and
evidence. Preserve old checkpoints instead of overwriting their tested SHA.

Required incoming sources: current Web gateway/media/browser tests and real client
results; native IME/caret/input results; audio capture/playback/focus results;
clipboard provider/protocol and synthetic file results; capture/codec/HDR results;
platform display/hotplug/recovery results; service lifecycle/installer results and
exact final package verification. Read the affected topic and `ROADMAP.md`, record
the actual source SHA, package ID/hash, installed identity (or explicitly unknown),
date, environment, feature, result and limits. A build/VM/mock pass must retain
its evidence type. A failed setup is not a failed product assertion; an unrun
case remains pending. Re-run the lightweight checker after each update.
