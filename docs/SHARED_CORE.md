## Polish UI review — 2026-11-04

Core `10a84ac8382755f65f1b1b6e60cb94557e4377f9` corrects 575 of the 1,214 Polish strings: wrong
senses (Sharing had been translated as "participating"), inconsistent terms for
the same label, literal phrasing, agreement errors and button style. The Qt
catalogs are regenerated from the same table. Desktop Git and Nix pin the same
published tree. Mobile pins and wire contracts are unchanged.

## Binding terminology — 2026-11-04

Core `4e65606c6c5115f641da5ed681b372b4f97cb8a8` replaces bookbinding and other inconsistent
translations of "binding" with one device-binding term per locale, keeps the
key-binding sense in keyboard shortcut strings, and fixes agreement errors found
in review (378 strings). The Qt catalogs are regenerated from the same table.
Desktop Git and Nix pin the same published tree. Mobile pins and wire contracts
are unchanged.

## Manual translations — 2026-11-04

Core `d5d7ffde97c72384edce9a3253a86d0cdfada89f` fills the 50 offline manual titles and steps that
still fell back to English in the 19 primary locales other than English and
Chinese. They are machine-assisted drafts that reuse each locale's UI labels for
the menus they name. `scripts/export-manual-site.py` renders the same manual on
the project site. Desktop Git and Nix pin the same published tree. Mobile pins
and wire contracts are unchanged.

## Fallback address manual step — 2026-11-03

Core `10307719a4b139d6674eb96ebd013a8eec175b6d` adds one offline manual step under "Connect to a computer":
several addresses per saved device, one per line on the paired port, the first
reachable entry in list order, and editing while connected. English and both
Chinese scripts are authored, with drafts in the other 19 primary locales that
reuse each locale's Change address label. Desktop Git and Nix pin the same
published tree. Mobile pins and wire contracts are unchanged.

## Data saver 1x workspace — 2026-11-02

Core `0b19bbb7841f23e3d27b746a02d6c7459ab5582f` adds `dp_workspace_for_saver`: with data saver on, a
2x workspace becomes the same logical desktop at 1x (a quarter of the pixels),
rounded to the protocol's multiple of 4 and its 640x360 minimum; 1x workspaces
are unchanged. Desktop clients apply it before tuning and the shared host floor.
Mobile pins, scale values on the wire and wire messages are unchanged.

## One host desktop floor for every OS — 2026-11-01

Core `1d02ba187769da7443d8c8833e43f8f5b3b6111c` adds `dp_workspace_for_host`: one 800x600 logical floor applied after
tuning, with the axes swapped for portrait workspaces, for macOS, Windows,
Linux and unknown hosts alike. Linux previously had no floor, so an iPhone's
2x viewport produced a ~390-point-wide Plasma/GNOME desktop. The macOS results
are unchanged. Windows portrait workspaces now use the swapped 600x800 floor;
its native adapter still downgrades to verified 1x with the existing warning
when 200% UI DPI is unavailable. `dp_workspace_for_macos`/`_for_windows` remain
as compatibility aliases. The desktop adapter no longer inspects the host OS.
Mobile clients apply the same floor for every host through the alias until
their pins advance. No wire message or scale value changes.

## Fallback address editor hint — 2026-10-30

Core `f54beef6f2f9007aa8a4092d1864fc08bb8f7698` adds one BindView-only string
explaining the one-address-per-line, in-order fallback list, with drafts in all
22 locales and matching Qt catalog metadata. Desktop Git and Nix pin the same
published tree. Mobile pins and wire contracts are unchanged; address ordering,
probing and storage stay in the desktop adapter.

## macOS window movement and resizing help — 2026-10-28

Core `866c09547cd47d4b26a0725374e70614cf2845db` adds SettingsHome help for
Control+Command dragging: move inside the picture, resize at its edges or corners.
The UI-only entry has drafts in all 22 locales and matching Qt catalog metadata.
Desktop Git and Nix pin the same published tree. Mobile pins and wire contracts
are unchanged; native gesture handling remains in the desktop adapter.

## Hidden-title-bar window movement help — 2026-10-28

Core `27caf8317b959da9c240dd5f8843b319d7fa74b7` adds five SettingsHome-only
help strings with drafts in all 22 locales: macOS Control+Command+drag,
Linux desktop movement shortcuts, the Windows limitation and the Wayland
Devices-page behavior. The Qt catalog metadata includes the same entries.
Git and Nix pin the same published tree; mobile pins and wire contracts are
unchanged. Physical window movement remains a desktop adapter responsibility.

## Shortcut key chooser and KDE conflicts — 2026-10-24

Core `505ae0ccd0a4ead4decb6eaba62585bf278e6dbe` adds the desktop key-chooser
labels in all 22 primary locales and updates the manual's single-row settings
entry. English and both Chinese scripts explain recording, mouse selection and
KDE Plasma's commonly occupied Meta+1–9 bindings; other manual locales use
English fallback. Git and Nix pins reference the same published tree. Keyboard
mapping and the window-scoped Wayland recorder inhibitor stay desktop-owned;
shared policy and peer wire contracts are unchanged.

## Custom desktop recall labels and manual — 2026-10-24

Core `b12d33c8811be6ee96efd45a0aeb14f775903ca1` adds the shortcut recorder,
validation/status labels and a manual paragraph covering save, cancel and reset.
The Git/Nix pins reference the same published tree. Settings text has draft
translations in all 22 primary locales; the new manual paragraph is authored in
English and both Chinese scripts with English fallback elsewhere. Registration,
validation and local worker IPC remain desktop-owned; shared policy and peer
wire contracts are unchanged.

## Desktop manual and window shortcut labels — 2026-10-24

Core `e482f24563b2026693e284fbd8ca54eb420b7896` expands the offline manual to
13 chapters / 66 steps and adds the desktop global-shortcut settings labels.
Git and Nix reference the same tree. New manual paragraphs are authored in
English and both Chinese scripts; missing translations use the existing English
fallback. Settings labels have draft translations in all 22 primary locales.
No display arithmetic, wire message or mobile implementation changes.

This core commit is published on main before the desktop consumer update.
It also retains the newer shared appearance terminology and manual-navigation
translation corrections.

## Combined local IME and Web catalog — 2026-10-18

Core `f34fc7b1524ce436185c4b085a903e6af4314790` merges the reviewed local-IME
catalog/contract (`2d98ec8`) and browser UI catalog (`b6400fd`). Desktop Git and
Nix pins reference the same public tree. The table has 1,133 UI-only entries and
22 locales; the explicit browser view contains 126 originals. Existing Qt
catalogs include the opt-in IME labels, and Web resources remain generated from
the same table. Mobile pins and wire behavior are unchanged.

## Windows system-DPI workspace floor — 2026-10-17

Core `e1850b0` adds the Windows 800x600 effective desktop floor, applied after
tuning while retaining aspect ratio and backing scale. Windows does not swap
the floor axes for portrait modes. The existing macOS floor retains its prior
results. Desktop advances the Git and Nix pins together; mobile pins are not
changed. The native Windows adapter still checks actual OS DPI availability and
supports older clients by reporting verified 1x with the existing warning field
when their workspace cannot support 2x. No new wire message or scale value.
A shared UI-only host-limit warning replaces the macOS-only note and covers the
Windows native downgrade, with 22 locale drafts and unchanged wire identifiers.

## Twenty-two shared UI locales — 2026-10-17

Core `b4b0ed1` expands the English-keyed UI table and offline manual to
22 primary languages. Both consumers advance their core pin; desktop advances
the matching Nix lock too. No wire contract, transport or input code changes.
Native generation handles Indonesian's legacy `in` picker/resources and modern
`id` locale setting. Translation drafts still need native-speaker review.

# Shared core development

See [extraction verification](SHARED_CORE_VERIFICATION.md) for completed checks and limits.

2026-09-17: Extract workspace arithmetic and freeze the existing display contract
to avoid implementing the same policy separately in desktop and native clients.

The public [deskport-core](https://github.com/keithxc/deskport-core) repository is
the source of truth for the drawable-pixel workspace algorithm and scoped display
wire contract. It is pinned at `shared/deskport-core`; Nix consumes the same commit
through the non-flake `deskport-core` input. `tests/test-core.py` rejects differing
Git and Nix pins. Ordinary GitHub flake fetches do not include submodules, so the
derivation explicitly copies the locked core into the source tree before building.

```sh
git submodule update --init shared/deskport-core
nix develop -c python3 tests/test-core.py
nix develop -c python3 tests/test-host-lifecycle.py --binding  # macOS, fake host
```

For a core update, check out the reviewed commit in the submodule, then run
`nix flake update deskport-core --override-input deskport-core github:keithxc/deskport-core/COMMIT`.
Commit the gitlink and flake.lock together. Run the adapter tests and platform
checks before updating the other consumer. Pins may differ temporarily; on-wire
compatibility must not depend on simultaneous product upgrades.

`app/backend/workspaceresolution.h` only adapts QSize and output scale. The public
core takes drawable pixels, never logical points. The desktop adapter retains its
existing invalid-input sentinel and proposal behavior. HostManager independently
validates requests; native capture/display APIs remain authoritative. Window state,
input capture, TLS, media and Qt UI remain in this repository.

Every cross-platform feature should record its wire/behavior contract, platform
support, downgrade behavior and test cases once in the core. Consumers implement
only their native adapters. New optional messages require peer opt-in; a shared
header alone is not a compatibility test. `protocol/display-cases.json` is exercised
through the production desktop TLS handler and Apple result/caret handler.

Backlog: binding fixtures across both adapters, versioned capability evolution when
needed, and a session-scoped input state machine only after a second consumer needs
it. Do not move global Moonlight input wrappers, keyboard/safe-area policy or release
tooling into the arithmetic core. No deployed service activation is part of a core
update; the existing release and manual-activation workflow still applies.

## Session policy update — 2026-09-17

The next core pin adds the optional `displayPolicy` capability, three stable mode
values, and malformed-policy fixtures. See [SESSION_DISPLAY.md](SESSION_DISPLAY.md).
The base binding version remains 1; peers negotiate the optional field explicitly.
Local core commits must be pushed before publishing consumer branches that pin them.

## Session takeover prerelease — 2026-09-19

The prerelease pins the shared admission contract and workspace adjustment policy
in both consumer Git submodules and the host Nix input. See core
`protocol/SESSION_TAKEOVER.md`. Physical takeover acceptance remains pending
until the user activates the prerelease.

## Public catalog and private mobile boundary — 2026-09-20

The independently authored `shared/deskport-core/portable/` module is MIT and
owns the port family, desktop tuning options and display policy IDs. The existing
GPL core files retain their licenses. `hostports.h` consumes generated C rules;
StreamingPreferences exposes catalog choices/labels to QML. Platform sockets,
settings persistence, localized text and media remain native.

Run the normal core and device-preference tests after changing the catalog. Never
edit generated C/Java output directly. See core `portable/FEATURES.md` for adapter
coverage and the rules for advancing core and consumer pins. The mobile product's
private repository and paid/proprietary target do not relicense any PC code.

## Stable entry update — 2026-09-21

See core `protocol/ENDPOINTS.md`. Mobile and desktop authenticated refresh retain
the configured connection entry independently from streaming endpoints. Mobile
legacy stream overrides migrate their hostname to the default entry; new explicit
entries are preserved. Run the endpoint transport and binding regressions.

## Private cycle-guard snapshot — 2026-09-26

This test branch pins the core Git tree and the identical immutable NAR snapshot.
The path input is preloaded from the private test Release, not a mutable working
directory. `test-core.py` verifies the Git archive NAR hash against flake.lock.
The delivery preparation tool restores both source snapshots after GC or on a
new machine. Core/source commits remain local; a later public integration must
publish the reviewed core and restore a reachable public input before publishing
its consumer. This private pin must not be merged into the public release branch.

## Public release pin — 2026-09-26

The public main branch now uses the reachable GitHub revision
`e71b21808e15c8bcd55d4af3f7c0cbc769ef0bcb` in both the Git submodule
and flake.lock. The private snapshot instructions above are historical;
public releases must retain the matching public pins.

## Local video pause candidate — 2026-09-28

This development candidate pins core `a71542a` and its matching immutable NAR
snapshot for optional video transmission pause. See core `protocol/VIDEO_PAUSE.md`.
The local snapshot is preloaded on test machines; no signature bypass is used.
Before public consumer integration, publish the reviewed core and restore a
reachable GitHub pin. The current local path pin is not a public release input.

## Reachable dev core pin — 2026-09-29

The video pause core change is now published on the core `dev` branch as
`2ebf5c9` (the local `a71542a` with corrected authorship and an identical
tree). The Git submodule and flake.lock both pin that GitHub revision; its NAR
hash equals the earlier local snapshot. Consumers can fetch DeskPort and its core
without preloaded store paths. Core `main` is unchanged: merging the video
pause specification there still requires review.

## Desktop tuning direction — 2026-10-01

Core `a107606` (on `dev`) inverts desktop fine tuning: larger values now enlarge
remote content by dividing the workspace, so 1.5 shows larger controls and 0.5
fits more content. Saved per-device values keep their number and therefore flip
meaning. Other consumers adopt the new direction when they advance their pins.
The macOS 800×600 logical floor still applies after tuning, so small client
windows can reach the floor at 1.0 and show no further enlargement.

## Core main alignment — 2026-10-03

Core `dev` (video pause specification and the desktop tuning direction) is
merged into core `main` as `4e80ab8`, keeping main's newer full-screen manual
text. DeskPort `main` now pins that core `main` revision in both the Git
submodule and flake.lock.

## Reachable QR invitation dev pins — 2026-10-04

This candidate pins published core `e7b4a47` in both the Git submodule and Nix.
Only `protocol/PAIRING_INVITES.md` and its shared URI fixtures are added relative
to the desktop's prior core. Native adapters implement the optional invitation
capability without changing the binding version. The mobile candidate uses the
same contract and fixtures on its previous core base (`83235de`) to preserve
its existing workspace behavior. Both commits are reachable from core `dev`
through merge `b5472b7`, whose tree is identical to `e7b4a47`. The remote Nix
input retains the verified local snapshot's NAR hash. Core/pin verification and
adapter tests remain required. No release or host activation is performed by
updating a core pin.

## Live video rate candidate — 2026-10-15

The optional live bitrate ceiling (`protocol/VIDEO_RATE.md`) is on core main
since `369d0fb`; its optional `saver` field since `4064059`.

## Unified UI catalog — 2026-10-17

Core `f5d915c` centralizes English-keyed UI translations, platform usage, Qt
context variants, locale metadata and offline manual text in `i18n/`. This
includes platform-only UI text. Desktop Qt source catalogs are generated by
`scripts/sync-translations.py`; Nix checks them against the locked core before
building. Never edit those generated catalogs or translate a protocol token.
The existing GPL catalog and notices remain separate from MIT `portable/`.

Run `tests/test-translations.py`, `tests/test-compiled-translations.py`,
`tests/test-core.py` and `tests/test-device-preferences.py` after advancing
the matching Git/Nix pins. Native resource generation consumes the same table.
UI locale changes do not change any wire messages or persisted policy IDs.

## Shared browser UI view — 2026-10-17

Core `b6400fd` adds the explicit `Browser` usage view: 126 English originals
with nonempty cells in all 22 primary locales. `_web` and optional
`_webTranslations.Browser` scope the view without changing the native platform
schema or any existing native cell. The matching Git/Nix pins move together.
`scripts/sync-browser-translations.py` projects static Web resources; Nix and
CI reject stale output and changed placeholders. The Web client consumes the
same locale registry, direction and native names. No mobile implementation,
wire value, credential, policy ID or translation service is part of this view.

New Web translations are machine-assisted drafts requiring native-speaker
review, especially Tibetan and authentication copy. Resource coverage and
browser font/layout evidence are distinct from linguistic or physical-device
acceptance. Run the browser integrity/fixture checks plus core, compiled
translation and device-preference checks when advancing this pin. See
[BROWSER_ACCESS.md](BROWSER_ACCESS.md#shared-web-localization--2026-10-17).
