# Desktop interaction guide

The desktop preview shares one QML interface across macOS and Linux. Device trust
remains reciprocal, while streaming preferences are local to the computer running
the viewer. This is not cloud synchronization of preferences or permissions.

## Main navigation

- **Devices**: a card for each remote computer, with its name, endpoint and current
  availability. Open a paired device to connect; use the card menu for details,
  naming, removal and legacy compatibility actions.
- **Sharing**: the local sharing switch, permission guide, saved-access management
  and sharing preferences. macOS offers a virtual display; Linux captures the
  current desktop. Compatibility PIN entry and logs are secondary controls.
- **Settings**: Picture, Input, Sound, Connections and Advanced tabs. Common values
  save when changed. Opening the page or changing tabs does not apply presets or
  replace existing custom resolutions, frame rates or bitrates.

## First use and permissions

New users without saved bindings see the setup guide. It can be skipped for
viewer-only use and revisited from the sidebar. Finishing the guide records only
that the guide was dismissed; it never records a permission grant.

macOS checks Screen Recording, Accessibility and audio-input authorization
separately. Each has a matching System Settings shortcut. The draggable app tile
exports the installed app's file URL; Reveal in Finder is a fallback for settings
panes that do not accept drops. Permission changes remain user actions. Some
macOS grants require reopening the app or restarting sharing before taking effect.
Audio input is conditional, not a requirement for every connection.

Linux describes capture consent and checks access to `/dev/uinput`. It does not
pretend that starting a host process verifies capture or remote input.

## Device binding

Add a device by IP or domain. Show connection progress until the recipient
acknowledges receipt, then wait for explicit **Allow & bind**. Saving trust in both
directions is distinct from the device being online or having system permission.
Saved access is visible and locally revocable; removing access on both computers
still requires removal on both sides. Legacy Sunshine/Moonlight PIN pairing stays
available through an explicit compatibility entry point.

## Language

Settings starts with a language selector. Follow system is the default; an explicit
choice is saved locally and takes effect immediately on supported Qt versions.
Language names use their native scripts so the selector remains recognizable after
switching languages. A language change must not reset connection preferences,
restart sharing or remove trusted devices.

English, Simplified Chinese, Traditional Chinese, Japanese, Korean, German, French
and Spanish cover the new desktop settings, device cards, sharing, binding and
setup pages. The selector also preserves the existing Italian, Portuguese, Russian,
Dutch, Polish, Czech, Swedish, Norwegian Bokmål, Turkish, Hungarian, Greek,
Vietnamese and Thai catalogs. Coverage outside the new pages varies; untranslated
text falls back to English. Regional Chinese system locales select the appropriate
Simplified or Traditional catalog, including Traditional Chinese for Hong Kong.

Translation sources are `app/languages/qml_*.ts`; keep their compiled `.qm` files in
sync with `lrelease app/languages/*.ts`. Run `python3 scripts/test-translations.py`
and `python3 scripts/test-host-lifecycle.py --ui` (inside `nix develop` on Linux).
The UI test changes language selection without changing stream preferences and
loads each primary catalog for live QML retranslation and English restoration.
Community review of terminology and native-language layout remains welcome.

## Validation

- macOS: `python3 scripts/test-host-lifecycle.py --ui`, `--binding`, and the default
  host lifecycle suite, plus signed package validation and native window review.
- Linux: `nix build`, the packaged CLI smoke check, and
  `nix develop --command python3 scripts/test-host-lifecycle.py --ui`.
- Manual acceptance: review both desktop layouts, tab/keyboard navigation, actual
  OS app-drop support, recipient approval, and picture/input in both directions.
  Keep independent remote-access services available during acceptance.

On 2026-09-10, a user confirmed that the Linux client connected to the macOS host
after the stream-start fix in `4bbedf1a`. The UI suite now activates stream and quit
pages and verifies their operations execute without the removed toolbar. This
connection confirmation does not complete the remaining permission, input or
display-lifecycle acceptance checks.
