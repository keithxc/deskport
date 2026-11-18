# Browser access

For the current PC host/client boundaries and dated results, see
[PC implementation and acceptance](PC_SUPPORT_MATRIX.md). Browser feature
availability and Chrome fixtures do not establish every native host, Safari,
physical audio, IME or clipboard combination.

The desktop host starts a local HTTPS browser entry by default. An iPad or
another device can open its URL without installing DeskPort. Sign-in uses a
six-digit code that changes every 30 seconds, shown on the computer and, if you
add it, in an authenticator app such as Ente Auth. No account, password
registration, mobile app or browser key enrollment is required.

## Connect

1. Start DeskPort on the computer and enable sharing.
2. Expand **Browser access** on the Sharing page. It shows whether the entry is
   running and on which networks. Scan the URL QR code or open one of the
   displayed LAN or Tailscale URLs on the other device. The current code stays
   hidden until you select **Show code**; a countdown shows when it changes.
3. Complete the browser's certificate trust step, then type the six-digit code
   shown on the computer or in your authenticator app. Select **Remember this
   browser for 7 days** only on your own device, then select **Connect**.
4. Use the touchpad, direct touch, mouse, physical keyboard, on-screen shortcuts,
   or text panel. The text panel accepts composed text, including Chinese.

The HTTPS entry runs whenever DeskPort runs; no command needs to be issued on
the computer before connecting. `deskport web info` only reports the URLs and
the current code.

## Time-based code

The code is a standard TOTP (RFC 6238: HMAC-SHA1, six digits, 30-second steps).
**Add to authenticator app** on the Sharing page, or `deskport web authenticator`
over SSH, shows an `otpauth://` QR code for Ente Auth or any other authenticator,
so the code is available on your phone even when the computer has no display.
That QR code is the secret itself: anyone holding it can sign in. **Reset code
secret** (`deskport web reset-code`) replaces it, disconnects every authenticator
and ends browser sessions signed in with a code; remembered browsers stay paired.

The computer accepts the current step and one step either side for clock drift.
Each step is accepted once, so a second browser waits for the next code. Ten
wrong codes within 15 minutes pause code sign-in for 15 minutes, on top of the
per-address rate limit; the Sharing page and `deskport web info` show the pause.
Tailscale sign-in of owned devices is not affected. Upgrading from the earlier
fixed six-character code retires it and forgets every remembered browser once.

## Tailscale devices without the code

A browser that reaches the computer through its Tailscale address can sign in
without the access code when the computer's own Tailscale agent confirms that
the device belongs to the same Tailscale (or Headscale) user as this computer.
The page asks once when it opens; an owned device sees **Connect** directly.
DeskPort runs `tailscale status` and `tailscale whois` locally for each such
request. tailscaled has already authenticated the device's WireGuard key, so its
tailnet address identifies the device. Tagged devices, other users' devices,
computers that are themselves tagged, LAN connections and any failure of the
local query fall back to the access code. A Tailscale sign-in creates an ordinary
session but no remembered pairing; ownership is checked again on each visit.

Set `web/tailnetIdentity=false` to require the code everywhere, or
`web/tailscale=/path/to/tailscale` when the CLI is not in a standard location.

The URL QR contains only the HTTPS URL. The code is entered separately and never
appears in URLs or browser storage. A remembered browser restores its pairing
after refreshing or reopening the page and can connect without entering a code
for seven days. Starting desktop media still requires selecting **Connect**. A
browser that is not remembered keeps its sign-in only in page memory, so closing
the tab or browser requires a new code; media packets and API requests within a
connection never ask again.

The server creates a self-signed certificate on first run. Browser trust is
separate from the access code: this version does not silently install a CA or
bypass certificate validation. A browser/device that cannot trust this
certificate needs an explicitly configured trusted certificate before use.

From a local terminal, or SSH as the same desktop user:

```sh
deskport web info
deskport web info --json
```

The local command returns URLs and the current code with its remaining seconds,
and its human output includes a terminal QR of the URL. Ordinary host status and
unauthenticated web responses never include the code or its secret;
`deskport web authenticator` is the only command that prints the secret.

## Remembered browsers

Remembered pairing works in ordinary Chrome tabs; no extension, installed web
app, account or authenticator is required. Safari uses the same browser entry,
but its acceptance checks remain open. Each browser gets an independent
256-bit random credential in a persistent `Secure`, `HttpOnly`, `SameSite=Strict`
cookie. Page JavaScript cannot read it. The server saves only its hash and local
device metadata. A successful restore recovers a temporary session. The pairing
is valid for seven days from the code entry that created it; use does not extend
it, and entering a code again restarts the seven days. Browser and server
restarts preserve the pairing until then.

**Disconnect** stops media and releases control while keeping the pairing.
**Forget this browser** removes its saved authorization. The computer's
**Sharing → Browser access → Paired browsers** list can remove one browser and
invalidate its active sessions without removing the others. SSH users can use:

```sh
deskport web list
deskport web list --json
deskport web remove BROWSER_ID
```

Removal invalidates the old credential. Pairing again needs a current code;
resetting the code secret also stops authenticator apps from producing one.

Keep the HTTPS host name or IP address stable. Browser profiles and private
windows have separate storage; clearing cookies/site data or losing browser
storage requires pairing again. The saved server record cannot restore a
credential that the browser has lost. HTTPS certificate trust remains required
after pairing, including when a certificate is replaced.

## Available controls and boundaries

- H.264 Constrained Baseline SDR desktop video at the remote desktop's own
  resolution (up to about 3840 × 2160 at 60 frames per second, slightly less at
  the largest sizes to stay within H.264 Level 5.2; encoder level chosen from
  the size and rate); stereo Opus audio. The SDP advertises 42e01f with
  packetization mode 1 like browsers' own WebRTC senders. The bitrate selected
  on the page applies at 720p and 30 fps and scales with the encoded size and
  frame rate, up to 40 Mbit/s.
- Adaptive desktop size. The page reports its drawable area, device pixel ratio
  and the chosen content size; the host derives the remote desktop from the same
  shared-core workspace policy as native clients, including the macOS minimum.
  Resizing the window, rotating a tablet, entering fullscreen or changing the
  content size in the toolbar resizes the remote desktop while connected. Only
  video capture restarts; audio, input and the WebRTC connection continue.
- Fullscreen shows only the remote desktop; the toolbar appears near the top
  edge or from the handle and hides again after a few seconds.
- "Use as an extended display" (connection options) keeps the computer's own
  displays and adds the browser as a display to their right, where the host
  supports display policies (macOS, Windows virtual display, Linux except GNOME
  and Hyprland). A headless Mac has no physical display to extend, so the
  browser display becomes its only display, as in the default mode.
- Touchpad movement, tap/click, two-finger scrolling, long-press dragging, direct
  touch, mouse wheel, physical keys and on-screen shortcuts.
- Explicit text entry/paste into the remote computer; this is not automatic
  bidirectional clipboard synchronization. Clipboard reads require the browser's
  permission or manual paste into the text box.
- One active controller. Browser sessions and native clients share the host's
  existing admission gate. Starting a browser session cannot take over an active
  native session. Disconnect releases pressed inputs and stops media before
  restoring the display and releasing admission.
- Browser backgrounding, lost connections and missed heartbeats release control.
  Some browser or operating-system shortcuts cannot be intercepted; use the
  on-screen shortcut buttons in those cases.

This version uses direct WebRTC over the LAN or a Tailscale/Headscale tailnet,
with no STUN/TURN relay, public rendezvous, UPnP change, or automatic firewall
change. Over Tailscale, media follows the tailnet path (direct or DERP-relayed). File transfer, gamepads, HDR, HEVC,
AV1, multiple displays, and native-client feature parity are outside this first
browser client.

## Host configuration

The default HTTPS port is TCP **48992**. Media uses WebRTC UDP **48100–48115**.
Allow these only on the intended network if the host firewall blocks them.
HTTPS binds the available private/link-local IPv4 interfaces, Tailscale or
Headscale addresses, and loopback; it does not bind every public interface. A
tailnet address is RFC 6598 space (`100.64.0.0/10`) on a tunnel interface such as
`tailscale0` or `utun`; the same range on a physical or cellular link is carrier
NAT and is not bound. Listeners follow interface changes within about ten
seconds, for example when Tailscale starts after DeskPort at login, without
interrupting connected browsers.

The generated certificate is retained while it names every served address. When
a new address or name appears, DeskPort extends that self-signed certificate,
keeping the names it already had, so the connecting browser must trust the new
certificate once. The access code and remembered browsers are unchanged. A
custom certificate is never modified; it must already cover any Tailscale address
or MagicDNS name used. Keep `browser.ini` to retain the code secret, so
authenticator apps keep working.

The supervisor accepts `--no-web-server` to disable this entry for that run.
Persistent settings use the existing DeskPort settings file:

```ini
[web]
enabled=true
port=48992
# Optional specific local bind address; otherwise private IPv4 interfaces.
# bindAddress=192.168.1.20
# Optional matching PEM certificate and private key.
# certificate=/absolute/path/certificate.pem
# privateKey=/absolute/path/private-key.pem
# Optional DNS name covered by that certificate and resolving to this LAN host.
# allowedHosts=desktop.example.com
# Owned Tailscale devices may connect without the code (default true).
# tailnetIdentity=true
# Optional tailscale CLI path when it is not on PATH or a standard location.
# tailscale=/run/current-system/sw/bin/tailscale
```

`DESKPORT_WEB_PORT` and `DESKPORT_WEB_BIND` override port and bind address for a
run. With a custom certificate, explicitly configured `allowedHosts` appear
before IP URLs and can be used for the QR entry. Browser credentials and the
generated certificate are stored beneath the
application's local data directory in `browser/`. The directory and private
files are restricted to the current user. The code secret is saved so it
survives restarts. Browser pairings are stored separately in the same private
directory. Multiple independent remembered browsers are supported; only one may
control the desktop at a time.

## macOS host checkpoint — 2026-10-08

A Mac host previously admitted browser sessions but sent no video: the bundled
Nix copy of Apple's libiconv could not load its converter data away from the
packaging machine, and libnice stopped answering ICE checks after GLib produced
an empty error. The package now uses the system libiconv. Verified on mm1 with an
isolated instance using the installed primary/mirror display policy: a headless
Chrome on pk4 signed in over Tailscale without the code and received continuous
H.264 video (about 30 fps, 1280x528) and Opus audio.

Adaptive size checkpoint, same setup: from a 1368x728 viewport the desktop
followed portrait (868x1028), ultra-wide (1568x528), 150% content size, four
rapid window changes (coalesced to the last size) and fullscreen without
reconnecting; decoded frames kept increasing across each video restart.

## Implementation and validation

`BrowserGateway` serves embedded assets and authenticated same-origin HTTPS
requests. `BrowserHost` coordinates admission and the existing display manager.
Only the private, certificate-pinned loopback management channel reaches the
host's browser transport. Browser input uses an ordered WebRTC DataChannel, with
an authenticated HTTP fallback. The existing capture and encoder produce H.264
and Opus directly for libdatachannel; there is no second decode/encode step.

Every desktop host package includes libdatachannel 0.24.1 and the browser
overlay: Nix and macOS link it dynamically from the pinned dependencies; the
native Linux packages build it from pinned source in the packaging container
(`scripts/build-linux-libdatachannel.sh`); the Windows cross build links it
statically (`winbuild/scripts/build-host-datachannel.sh`). All use libjuice for
ICE. The Windows installer opens TCP 48992 for `DeskPort.exe` and UDP
48100–48115 for the host, from the local subnet and Tailscale address ranges.
Flatpak is viewer only and has no browser entry.

Run the isolated checks inside the locked development shell:

```sh
python3 tests/test-browser-gateway.py
python3 tests/test-browser-host.py
python3 tests/test-host-lifecycle.py --cli
```

`tests/test-browser-media.py` checks input serialization and the native
WebRTC transport. `tests/test-browser-client.mjs` exercises the embedded web
client with encoded test media. `tests/test-browser-native.py --help` lists
the dependencies for the separate Linux Xvfb/PulseAudio capture fixture; it
uses `scripts/build-browser-live-gateway.py` and a disposable Chromium profile.
`tests/test-browser-pairing-profile.py` uses an installed Chrome binary with a
new persistent profile and production HTTPS authentication, without a capture
host. `tests/test-browser-pairing.py` adds the isolated native capture fixture.
Both restart the browser process and the gateway process, check profile isolation,
and exercise forgetting and local revocation. All fixture state and evidence
belong under the configured DeskPort build root.

Persistent-pairing checks passed in ordinary Google Chrome 154 without a PWA:
page refresh, full browser restart and server restart preserve authorization;
temporary mode, separate profiles, forgetting and revoked-credential replay
behave as expected. The HTTPS suite also covers malformed storage, persistence
failures, expiry and cross-tab session reuse. Frontend regressions cover delayed
cleanup in one tab while another tab takes control. macOS and native Linux builds,
the paired-browser UI, and the final packaged Linux CLI pass their checks.
Browser automation accepts the fixture's self-signed certificate explicitly; it
does not test the user's initial certificate trust flow or change system trust.
An additional WebKitGTK probe did not reach the page's initial pairing state;
it completed no product assertions and does not establish Safari compatibility.

The isolated native fixture passes real capture/encode/browser sessions with
changing 720p30 frames, nonzero decoded audio, stop and reconnect, and local
revocation followed by new enrollment. It uses
the production HTTPS gateway and web client with a test management adapter;
native input is disabled. The production `BrowserHost` lifecycle has separate
tests. These results do not establish physical iPad Safari certificate, audio,
touch or latency acceptance, nor actual compositor display restoration. See the
dated checkpoint in [ROADMAP.md](ROADMAP.md).

## Shared Web localization — 2026-10-17

The language menu follows the pinned core's 22-language registry. It initially
uses the saved `deskport.language` preference, then the first supported browser
language, then English. System default removes the preference. Chinese script
and region fallbacks, regional Portuguese, and Indonesian `in`/`id` are handled
by the UI adapter. Switching also works for the current visit when browser
storage is unavailable; same-origin tabs follow preference changes.

Every authored label, accessible name, placeholder, state and known error uses
an English key scoped to core's `Browser` context. Run
`scripts/sync-browser-translations.py` to project that view into the embedded
`app/browser/i18n-data.js`; never edit this generated dictionary. Static checks
and Nix pre-build reject stale resources or placeholder mismatches. Build and
runtime do not contact a translation service. The 126 keys have nonempty cells
for all 22 languages; absent cells and unsupported languages still fall back to
English. New translations are machine-assisted drafts and need native-speaker
review, particularly Tibetan and code/pairing instructions.

A language change redraws UI text without restarting authentication, pairing or
media. Connection starts disabled until scripts initialize; the native form
fallback uses POST so a script failure cannot put a code in a GET URL. Normal
JSON authentication is unchanged. Language is a separate browser UI preference: existing IDs, cookies,
TOTP, authorization and session requests are unchanged. Names, unknown server
messages and transmitted text remain literal text; they are never interpreted
as HTML or translation templates. Numbered substitutions happen once, with
bidirectional isolation. The document updates `lang` and `dir`; access codes,
keycaps, video coordinates and stream dimensions keep their original direction.
Login constraint and timeout errors use the selected UI language. Login errors
participate in document flow so long translations cannot cover the Connect
action. RTL layout uses logical spacing. Long controls wrap or scroll inside the session
area so a narrow window can still reach all actions. Quality labels reflect the
existing 60 fps request without changing media options.

Font fallback uses the browser/system's Arabic, Tibetan, Indic and CJK fonts;
no font or remote font service is bundled. A passing layout or platform-font
inventory does not establish every glyph, font installation, native-speaker
quality, physical Safari/Firefox behavior or desktop Qt font acceptance.

The real-browser language fixture uses production HTML/JS/CSS with synthetic
loopback HTTPS/API data. It covers initial selection, all registered locales,
storage denial, persistence, cross-tab updates, active connection state, literal
names/errors, placeholder safety, Arabic RTL, Tibetan, CJK and triple-length
text. It records screenshots and Chromium's actual font inventory. The separate
WebRTC regression uses real encoded test media and a production transport test
binary; neither fixture connects to a personal host or injects OS input.

```sh
python3 tests/test-browser-translations.py
# Headless fixtures use an isolated profile and no physical GUI session.
# Reserve local-ui.lock for headed/physical GUI follow-ups; use the task ID.
DESKPORT_PLAYWRIGHT_MODULE=/path/to/playwright-core \
DESKPORT_TEST_CHROME=/path/to/chromium \
DESKPORT_FRONTEND_TEST_OUTPUT=/resolved/deskport/build/run/evidence \
node tests/test-browser-i18n.mjs
```

Core/adapter checks for this task use
`tests/test-core.py --skip-deferred-seamless` to respect the explicit exclusion
of deferred Seamless validation. No Seamless experiment or implementation is
part of this change.

Validation checkpoint: 71 offscreen Chromium language/layout/failure cases,
15 synthetic screenshots and 67 encoded H.264/Opus WebRTC cases pass. The native
Linux x86_64 Nix package, 165 isolated HTTPS/authentication cases, core/Qt
workspace vectors, compiled translation consistency and device-preference
checks pass. An acceptance-only builder hook enforced CPU 0–3, nice 15 and
at most four jobs; the actual compiler processes were sampled. The transport
fixture binary was reused only after confirming its five production/test C++
source files are unchanged from its recorded build. These are synthetic
transport and Web UI results, not a newly built full macOS application or
physical streaming/input acceptance. No installed application or mynix changed.
## Compatibility follow-up — 2026-10-17

Playback recovery now handles browsers that block even silent autoplay, missing
codec capabilities and decoder errors. DPR-only display changes use a rearmed
resolution query alongside viewport resize notifications. See the
[compatibility acceptance matrix](BROWSER_COMPATIBILITY.md) for strict-TLS
Chromium results, native WebDriver entry points and the remaining Windows-host
and physical-client gates. Native Safari 27.0.1 passed 12 checks against the
isolated Linux capture fixture with normal Keychain TLS validation, real H.264
decoding, Opus receipt and pairing lifecycle checks. The approved temporary
certificate and trust were removed after testing. Windows-host and physical
iPad acceptance remain separate.
