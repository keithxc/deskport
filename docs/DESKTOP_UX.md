# Desktop interaction guide

The desktop preview shares one QML interface across macOS and Linux. Device trust
remains reciprocal, while streaming preferences are local to the computer running
the viewer. This is not cloud synchronization of preferences or permissions.

## Main navigation

The top bar is the only navigation: manual, devices, edit, refresh, sharing and
settings, with the current page name beside the buttons. There are no Back or
Done buttons; secondary content opens as a floating panel or expands in place.

- **Devices**: a card for each remote computer with a status light, system icon,
  name and one action button. Clicking a card does its action; the icon, a right
  click, a long press or the menu key opens the device panel with its facts,
  actions and device settings (behind the gear).
- **Sharing**: the local sharing switch, permission guide, saved-access management
  and sharing preferences. macOS offers a virtual display; Linux captures the
  current desktop. Compatibility PIN entry and logs are secondary controls.
- **Settings**: appearance, language, diagnostics and about, as grouped rows.
- **Data saver** (leaf icon beside the session traffic): one switch for every
  device. Off, automatic picture mode sets the bitrate from the negotiated
  resolution and frame rate (15 Mbps for 2560×1440 at 60 fps, up to 80 Mbps) and
  the host lowers it live on a congested remote path. On, office data saver caps
  video at 1 Mbps (or an already lower manual setting), limits continuous changes
  to 8 fps on compatible hosts, and disables remote audio capture for the session.
  It keeps the desktop resolution, uses brief 2 fps refinement after changes,
  and refreshes a still picture once every 5 seconds. These are video-policy
  limits, not a total-byte or hourly-data guarantee; recovery frames bypass the
  cadence gate. Saved audio and automatic/manual preferences are preserved and
  restored when data saver is turned off. A switch reconnects if audio or capture
  cadence must be renegotiated; compatible video-only switches apply live.
- **Daily data budget** (in the panel opened from the traffic chip): a daily limit
  for every session on this computer, reset at local midnight or with Reset. The
  chip shows today's percentage and turns amber at 70% and red at 85%; at 100%
  data saver turns on for the rest of the day.
  Choices open option panels; privacy and licences open as reading panels.
  Streaming preferences belong to each device's settings.

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
sync with `lrelease app/languages/*.ts`. Run `python3 tests/test-translations.py`
and `python3 tests/test-host-lifecycle.py --ui` (inside `nix develop` on Linux).
The UI test changes language selection without changing stream preferences and
loads each primary catalog for live QML retranslation and English restoration.
Community review of terminology and native-language layout remains welcome.

## Validation

- macOS: `python3 tests/test-host-lifecycle.py --ui`, `--binding`, and the default
  host lifecycle suite, plus signed package validation and native window review.
- Linux: `nix build`, the packaged CLI smoke check, and
  `nix develop --command python3 tests/test-host-lifecycle.py --ui`.
- Manual acceptance: review both desktop layouts, tab/keyboard navigation, actual
  OS app-drop support, recipient approval, and picture/input in both directions.
  Keep independent remote-access services available during acceptance.

On 2026-09-10, a user confirmed that the Linux client connected to the macOS host
after the stream-start fix in `4bbedf1a`. The UI suite now activates stream and quit
pages and verifies their operations execute without the removed toolbar. This
connection confirmation does not complete the remaining permission, input or
display-lifecycle acceptance checks.
