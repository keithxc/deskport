# Desktop typography and direction QA

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

This acceptance fixture renders the production Settings, Sharing, offline Manual,
language picker, confirmation and connection-error components. It uses the real
compiled core translations, `StreamingPreferences` save/retranslate code and
manual adapter. Host, diagnostics and peer properties are inert synthetic QML
objects. No network, host lifecycle, media, permissions, clipboard or remote-input
implementation is linked. It never reads the installed application's settings.

The scope is desktop layout and font fallback. Web fonts belong to Web i18n.
Seamless, deployed-host activation and native-speaker signoff are outside this
fixture's acceptance claims. The core catalog and both pins remain unchanged.

## Reproduce

Use the locked development shell and an unused immutable run ID. All output stays
under the checkout-specific build workspace:

```sh
run="$(bash scripts/build-paths.sh releases)/typography-RUN_ID"
mkdir -p "$run/tmp"
TMPDIR="$run/tmp" nix develop -c python3 tests/test-desktop-typography.py \
  --output "$run/offscreen"
```

For native macOS window rendering, replace the output suffix with `cocoa` and
pass `--platform cocoa --renderer native --widths 640,896 --height 480` to use the platform's default Qt Quick
renderer; keep `software` for headless/CI comparisons. First check existing GUI ownership and atomically reserve
`~/mygit/build/deskport/locks/local-ui.lock`, recording thread, PID and time.
Release only that reservation after the runner and child probes have exited.
QTest sends events to its fixture window; it does not inject global input.

Linux builds run on the x86_64 build host, under the shared
`linux-heavy-build.lock`, `nice -n 15 taskset -c 0-3`, at most four jobs.
Use `nix build --no-link --cores 4 --max-jobs 1` as the production build gate.
Offscreen rendering uses that machine's real font installation, but does not
accept a KDE compositor, fractional system scaling or physical display.

The runner probes the platform's native DPR and compensates the test-only
`QT_SCALE_FACTOR` to produce **observed DPR 1 and 2**. It asserts the observed
value, rather than calling a Retina DPR 2/4 run 100%/200%. The headless profile
uses 640/1120 logical widths and a 760-pixel-high viewport. Native viewports must
fit the current screen at DPR 2; this Mac uses 640/896 by 480. `--widths` and
`--height` (PowerShell: `-Widths` and `-Height`) select that budget. Every PNG's
pixel dimensions are asserted. A native backbuffer cropped by the window manager
is a failed run, even if Qt reports the requested logical window size.

For a cached Windows static Qt build, pass its qmake through `DESKPORT_QMAKE`
and additional arguments through `DESKPORT_TYPOGRAPHY_QMAKE_ARGS`, then use
`--build-only`. Include its matching host qmake configuration and MinGW compiler
from the locked `winbuild/scripts/shell.nix`. Do not rebuild dependencies or edit
the shared cached toolchain. Copy the resulting `work/` directory, containing
`desktop-typography.exe` and `gui/`, into a Windows build-workspace run directory.

On Windows, run the companion PowerShell script with explicit fixture/output
paths and the owning thread ID:

```powershell
./scripts/run-desktop-typography-windows.ps1 `
  -FixtureRoot "$env:USERPROFILE/mygit/build/deskport/RUN_ID/work" `
  -OutputRoot "$env:USERPROFILE/mygit/build/deskport/RUN_ID/evidence" `
  -TaskId THREAD_ID -Platform offscreen
```

`-Platform windows -Renderer native` opens only the synthetic fixture with the
default graphics backend, and requires the owned
interactive user session. Session 0/SSH rendering is not interactive Windows
acceptance; use `offscreen` for headless font checks. Both modes reserve the
fixed `windows-ui.lock` and restore their process environment. An existing lock is
never adopted. Check the prior Windows and localization ownership records too;
a free new lock does not authorize taking an older chat's session. No installer,
service, driver, UAC, security preference or deployed application is changed.

## Evidence and limits

Each scale produces PNGs and `report.json`, with the platform, Qt and OS version,
observed DPR, layout direction, glyph-run fallback families, missing glyph counts,
and intentional text elisions. The matrix covers Arabic, Tibetan, Hindi, Thai,
Simplified Chinese, Japanese, Korean, German, Russian and an English reset.

The fixture includes synthetic allowed, needs-attention and verify-on-share
permission rows, and captures the bottom of scrollable pages too. It checks live
English-created Settings switching to the selected language, retained standard
and custom button labels, preserved standard-button bindings, mirrored closed
chevrons, language-picker Up/Down/Enter, modal Tab/Shift-Tab and Escape/Return,
and a second process loading each saved language and direction.
Long synthetic errors must retain their full content in a bounded scroll view;
the runner uses Page Down, Home and End to reach the text and captures its end.
The modal focus loop includes the message body so it remains reachable after
visiting the buttons. Manual chapter previews may elide
while closed; their full text remains available when expanded. Missing glyphs
fail the matrix after the report is written. Glyph coverage and screenshot review
do not prove linguistic correctness or every font installed on another PC.

## Native-speaker review still required

No new translation is marked as human-approved by this work. Review remains open
for every non-English primary catalog locale, including locales outside the
rendering matrix. Reviewers should record locale, keys/chapters, decision and date
in the shared core; translation fixes must be made there and regenerated.

| Locale(s) | Concrete review items |
| --- | --- |
| Arabic (`ar`) | `Theme`, `System`, `Light`, `Dark`, `Start sharing`, `Allow & bind`; action order, agreement, mixed Latin `DeskPort`/`QA`, `%1`/`%2` numbers, parentheses and ports in `Starting %1 failed: Error %2` and the firewall message; all seven manual chapters. |
| Tibetan (`bo`) | All UI terminology and seven manual chapters; especially `Language`, `Theme`, diagnostics/permission/binding vocabulary, tsek/shad boundaries and line breaking. Rendered glyphs do not establish correct terminology or orthography. |
| Hindi (`hi`) | `Theme`, `System`, `Light`, `Dark`, `Accent color`, diagnostics actions, permission/binding vocabulary, numeric error substitutions and all manual chapters. Check meaning as well as conjunct/mark placement. |
| Thai (`th`) | Word segmentation and wrapping, `Theme`/sharing/approval terminology, action labels, sentence spacing, number/port punctuation and all manual chapters. |
| Chinese (`zh-Hans`, `zh-Hant`), Japanese (`ja`), Korean (`ko`) | New shared host-limit warning, diagnostic/privacy text, permission/binding terminology, `%1 × %2`/`%3×` formatting, manual consistency and punctuation at line endings. |
| German (`de`), French (`fr`), Spanish (`es`), Portuguese (`pt`), Italian (`it`), Dutch (`nl`) | Long action labels and compounds, formal/informal address, `Allow & bind`, diagnostic/privacy wording, host-limit warning and all manual chapters. Portuguese regional suitability remains open. |
| Russian (`ru`), Ukrainian (`uk`), Polish (`pl`) | Case/number agreement around substitutions, sharing/approval/recovery terminology, action labels and all manual chapters. |
| Indonesian (`id`), Malay (`ms`), Turkish (`tr`), Vietnamese (`vi`) | Desktop/permission/binding terminology, diagnostics/privacy text, host-limit warning, action labels and all manual chapters; Vietnamese accents and Turkish casing require review. |

## Validation record — 2026-10-17

Implementation fixes are limited to RTL chevrons, option-list focus, explicit
modal focus traversal, scrollable long messages and refreshing retained standard
button text after a live language switch. The main macOS toolbar also preserves
the physical-left native window-button clearance when its contents mirror. No release,
deployment or native-speaker approval is implied.

| Check | Result and acceptance boundary |
| --- | --- |
| macOS 27.0.1, Qt 6.11.1, native Cocoa/Metal | Passed: 160 page records across ten locales, 640/896 × 480 logical viewports and observed DPR 1/2; every PNG has the matching raster dimensions, with zero missing glyphs. English-created Settings and retained dialog buttons, 20 new-process persistence probes, RTL chevrons/indentation, option keys, modal Tab/Shift-Tab, Page Down/Home/End and Escape/Return passed. Settings/Sharing had no detected text elisions. Synthetic pages and screenshots only; no installed app or streaming acceptance. |
| macOS software comparison | The same font/layout matrix passed with zero missing glyphs. Software rendering left thin repaint seams between manual cards; the native Metal comparison did not. Retain this distinction when reviewing headless screenshots. |
| Existing isolated UI suite | 27/27 passed, including production-main geometry at LTR/RTL and two native window-button inset values. Before the fix, RTL moved the physical-left clearance to 12 points instead of 95. This is a Qt scene-geometry check, not installed AppKit interaction acceptance. A short temporary path under the build root avoids the Unix-domain socket path limit in its single-instance test. |
| Core and translation adapters | Passed: source-catalog/placeholder checks, 17,825 compiled translation comparisons, device preferences, core C/C++ and Qt workspace checks. These are structural checks, not native-speaker signoff. |
| Linux font fixture / production Nix build | Not run: the shared build reservation remained occupied through handback. The owned waiting process was canceled; no deferred build remains running. These required gates remain open. |
| Windows static fixture build | Not run: the same shared build reservation was unavailable. The isolated cross-build command and cached toolchain configuration are prepared; this is not a successful compile claim. |
| Windows native font/window run | Not executed: the authorized target timed out over SSH. The PowerShell runner is prepared but has not been executed there. |

The retained-button regression first reproduced English `Cancel` after an
Arabic live switch. Recreating only the footer's standard buttons on show fixes
that cache while retaining the caller's binding and the custom accepting label.
An earlier oversized native profile returned a cropped Metal backbuffer at DPR
2. Those images do not pass the final pixel-dimension gate and are excluded from
the accepted native profile above; the large headless profile remains separate.

Physical KDE/Wayland compositor and system-scale changes, the installed main
window/title-bar chrome, other OS/font inventories, the twelve primary locales
outside this rendering matrix and native-speaker review remain open. No personal
host was connected for screenshots, and no deployed application, settings,
pairing, system permission or service was changed by the fixture.
