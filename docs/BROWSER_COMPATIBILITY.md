# Browser compatibility acceptance

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

## Client fixes — 2026-10-17

The browser now offers a playback gesture when autoplay rejects even muted
video, including connections with audio disabled. A rejected sound start keeps
video usable and offers a separate sound gesture. Playback completions belong
to their connection and attempt; a late rejection cannot revive a stopped
session or override a newer gesture. Unsupported decoders end the reservation
with codec guidance. Missing H.264 and Opus capabilities are explained before
reserving media; users can explicitly disable audio when Opus is unavailable.
No capture permission or autoplay/security setting is changed to unlock sound.

A resolution media query is rearmed whenever the device pixel ratio changes.
Window, visual-viewport and element-size notifications share the existing
coalesced resize path. The actual drawable element remains the size source;
pinch zoom and the on-screen keyboard do not become a second workspace policy.
The host continues to apply the pinned shared-core policy. Disconnection also
clears interrupted text composition so the next session can send text normally.
If a congested input channel cannot send a blur release, the client ends that
session so host teardown releases held input; it does not silently drop release.

## Evidence and limits

| Check | Result | What it establishes |
| --- | --- | --- |
| Production JavaScript with deterministic DOM/API doubles | 20 checks passed | Blocked audio/silent playback, manual recovery, stale playback completion, decoder errors, missing codecs, DPR query rearming, blur release and composition reset. No native browser/OS claim. |
| Linux Google Chrome 155 with the production `RtcSession` | 73 checks passed | Real H.264 decoding, Opus negotiation, ordered DataChannel/HTTP fallback, mouse/keyboard/touch events, composed text, disconnect, expiry, reconnect, pairing UI, layout and playback recovery. Authentication/API responses are doubles. |
| Same Chrome run with certificate validation enabled and namespace/seccomp sandbox | Passed | A short-lived fixture root in a disposable HOME NSS database is trusted; an unrelated certificate fails with `ERR_CERT_AUTHORITY_INVALID`. System/user trust is untouched. |
| DPR in Chrome automation | Passed with an explicit resize notification | CDP changes the sampled ratio but does not simulate a physical monitor's resolution-query event. Physical mixed-DPI movement remains untested. |
| Linux `nix build --no-link --cores 4 --max-jobs 1` | Passed | Built the current production browser source and locked host dependency; no installation or activation. |
| Native Linux Xvfb/Pulse capture through the production HTTPS gateway | 27 checks passed | Real capture, H.264 encode/decode, Opus receive/decode statistics, fresh-TOTP reconnect and stop/released reservation. No HTTP mocks or TLS/policy bypass; native input disabled. All fixture processes stopped. |
| Windows native input-codec / RTC loopback fixtures | Both executables exited 0 | Native serialization and local ICE/DTLS/SRTP/DataChannel execution with synthetic packets. Resident DeskPort/host/display processes unchanged; GUI lock and scratch returned. No capture, decoder or OS-input claim. |
| macOS Safari 27.0.1 native WebDriver against the Linux synthetic capture fixture | 12 checks passed | Normal Keychain TLS validation, production HTTPS enrollment, 89 decoded H.264 frames, Opus packets, unmuted playback, disconnect, refresh and forget. Fixture reservation released and all owned processes stopped. No Windows-host or iPad claim. |
| Windows full host/browser session | Not run | No claim about Windows capture, audio startup, display resize/restoration, native input, certificate enrollment or Safari-to-Windows streaming. |

The existing Mac-host checkpoint in [browser access](BROWSER_ACCESS.md) is
historical Chrome evidence. This task reran the Linux Xvfb/PulseAudio fixture
with real capture/encode and production HTTPS. It disables native input and
uses a test management adapter. Its previous Chromium certificate override did
not test trust enrollment. The current passing default driver uses private NSS fixture trust,
keeps the Chromium sandbox enabled, and uses normal chain/hostname checks plus
an exact pin on the loopback upstream. Each reconnect obtains a fresh TOTP
step through the owner-only fixture control socket; the previous driver reused
a consumed code and could not complete a second temporary login. The existing WebKitGTK probe also uses a trust override and
is not Safari. Its legacy cookie assertion was also corrected from an
impossible greater-than-347-day expiry to the documented seven-day window;
that WebKitGTK probe was not rerun in this task. The separate native Safari run
used an explicitly approved one-day fixture certificate in the login Keychain.
After the run, its certificate and trust were removed: the exact fingerprint
was absent and normal SSL verification rejected it. These runs do not establish
Windows-host acceptance, physical touch, audible output, latency or compositor restoration.

## Reproduce the client checks

Run the dependency-free policy regressions from the checkout:

```sh
node tests/test-browser-compat.mjs
```

For Linux Chromium, first build the isolated transport with
`tests/test-browser-media.py`, then supply the existing H.264 test pattern,
Playwright module and installed Chrome executable to
`tests/test-browser-client.mjs`. The raw pattern must contain actual
Constrained Baseline H.264, 30 fps SPS timing and AUD delimiters; the script
checks it with FFprobe/FFmpeg before use. Set the required
`DESKPORT_TEST_CERTUTIL` to the locked nixpkgs `nssTools` certutil executable.
`DESKPORT_FRONTEND_TEST_OUTPUT` must name a dedicated run under the checkout's
`bash scripts/build-paths.sh releases` directory.

The launcher creates a private HOME and both supported Linux NSS database paths,
imports only the generated one-day fixture root, and removes that HOME after
closing Chrome. It uses no certificate-error, autoplay or browser-security
bypass. Omitting `DESKPORT_TEST_CERTUTIL` fails before launching the browser.
The Chrome trust layout follows
[Chromium's Linux certificate documentation](https://chromium.googlesource.com/chromium/src/+/master/docs/linux/cert_management.md).

The new playback recovery assertions inject only a `NotAllowedError` from
`play()` before restoring the real method. Subsequent decoding uses the real
browser. This is a regression for the documented policy response, not proof
of a particular Safari setting. See WebKit's
[autoplay policy](https://webkit.org/blog/7734/auto-play-policy-changes-for-macos/)
and [receive-only WebRTC audio policy](https://webkit.org/blog/7763/a-closer-look-into-webrtc/).

The Linux locked devShell now exposes the already pinned libdatachannel and
JSON development headers for the isolated transport checks. No dependency
version or shared-core pin changed. Rebuild the standalone live gateway from
this checkout before running `test-browser-native.py`; its default driver also
requires `--certutil`. Custom legacy pairing/WebKit drivers retain their own
explicit certificate-validation scope and do not become strict acceptance by
being attached to this supervisor.

The Nix validation snapshot included a scheduling-only `preUnpack` hook to
apply CPU affinity 0–3 and nice level 15 inside the daemon's builder. This hook
is absent from the integration branch. The built source's `app.js` hash equals
the final production source, and its host dependency is the exact binary used
by the 27-check capture run. No dependency was upgraded for validation.

## Native Safari / Windows browser gate

Reserve the canonical host GUI lock and check process/session ownership before
opening a browser. Use only a caller-owned synthetic fixture with native input
disabled. Keep its supervising process running throughout the probe. Do not
point a fixture at the installed host, personal display, existing credentials
or real clipboard. `tests/test-browser-webdriver.py` accepts this metadata:

```json
{
  "url": "https://127.0.0.1:PORT/",
  "code": "SYNTHETIC_CURRENT_CODE",
  "isolatedSynthetic": true,
  "nativeInputEnabled": false
}
```

The URL must be loopback HTTPS; a tunnel must preserve the certificate's name
and the gateway's port. The production gateway validates the HTTP authority;
forwarding through a different local port correctly produces HTTP 400 before
the application loads. Reserve the same unused loopback port on both hosts.
Keep the metadata private under the run directory. It is never committed or
included in reports. Prepare a certificate with matching SAN and appropriate
validity, verify its SHA-256 fingerprint through the fixture's local terminal,
and explicitly trust its issuer through the platform's normal certificate UI
before running. Safari uses Keychain trust; Windows browsers use their normal
platform certificate trust. This script never installs trust or handles an
interstitial. Use an existing administrator-approved test trust path, or leave
that gate untested until the user completes enrollment. Remove a temporary
trust entry through the same path after testing. Never select an insecure
exception, disable TLS, or change security features as an acceptance shortcut.

`test-browser-native.py --fixture-certificate ... --fixture-key ...` can reuse
a caller-owned synthetic certificate prepared for that enrollment. Supply
both paths together. The default remains a new one-day certificate in the
isolated run; existing host identity and keys are never imported implicitly.

The installed Safari driver needs the user to enable **Allow remote
automation** in Safari's developer settings (**Allow remote automation and
external agents** in Safari 27); see
[Apple's WebDriver instructions](https://developer.apple.com/documentation/safari-developer-tools/macos-enabling-webdriver).
Once the fixture and trust are ready:

```sh
python3 tests/test-browser-webdriver.py \
  --fixture "$RUN/browser.json" --driver /usr/bin/safaridriver \
  --browser safari --output "$RUN/safari-native"
```

For Windows, use the matching Edge WebDriver and `--browser MicrosoftEdge`, or
ChromeDriver and `--browser chrome`. This is a visible native browser test and
requires the corresponding GUI lock. `acceptInsecureCerts` is always false.
`--auth-only` tests production HTTPS enrollment/refresh/forget without claiming
media. A media probe checks H.264 decode statistics, Opus receipt and unmuted
playback, then explicit stop, refreshes pairing
and forgets the synthetic enrollment. It does not prove physical input/audio.
The recorded Safari run used this media probe against the isolated Linux
capture fixture. A native Windows browser run remains pending. On failure the
probe stores only structural page diagnostics in JSON and a private screenshot
of its synthetic session under the caller's output directory.

The bundle-oriented `run-isolated-session-host.sh` documents macOS parallel
display isolation. The Windows display adapter does not consume the Mac
`DESKPORT_DISPLAY_ISOLATED` / `DESKPORT_DISPLAY_SERIAL` identity variables; a
portable settings directory alone does not establish a second independent
Windows display lease. Do not treat that macOS harness as Windows isolation or
reuse personal pairing material to make a test pass. A full Windows host run
needs a dedicated, explicitly owned synthetic desktop/session. This task does
not introduce another Windows service or VDD architecture.

Complete the remaining native matrix with a synthetic moving pattern and tone:
TOTP failure/expiry, remembered and temporary pairing, browser/server restart,
H.264 frames and Opus packets plus audible startup gesture, resize/orientation,
DPR, mouse/key/touch/text, held-input blur/background release, disconnect,
revocation, expiry, failed network and explicit reconnect. Repeat with a
Windows host, then Safari as the client. Record native OS receipt/restoration
separately from serialized browser events. Untrusted, wrong-name and expired
certificates must be blocked before credentials or media are sent. An
inaccessible TLS page cannot display an application-level certificate error;
a failed API call can only offer network/certificate guidance.

## Windows transport fixture

`test-browser-media.py --cross-windows` builds standalone, static MinGW
executables from the production `RtcSession` and existing input-codec tests.
Run it inside `nix-shell winbuild/scripts/shell.nix` so the compiler's pinned
mcfgthread development headers and library are available. The locked compiler
uses mcfgthread; adding `-pthread` incorrectly requests an absent pthread import
library. Set `CXX=x86_64-w64-mingw32-g++`, `--rtc-prefix` to the pinned Windows
libdatachannel prefix, `--openssl-prefix` to the static OpenSSL prefix, and
`--json-include` to the pinned JSON headers. Pass a dedicated `--output` under
the build root. It verifies the compiler target and does not try to execute a
Windows binary on Linux.

Run the resulting `input-codec-test.exe` and `rtc-transport-test.exe` only in a
task-owned Windows scratch directory. They instantiate no capture or native
input backend and use two local test peers. Preserve installed services,
firewall/security settings, pairing and the user's desktop. The recorded native
run completed both executables with exit code 0 and preserved the resident
process set. Windows' PowerShell file execution policy stayed in place; the
executables ran as ordinary foreground CMD commands with normal exit codes.
Only Windows system DLL imports were present. This establishes serialization
and ICE/DTLS/SRTP/DataChannel execution, and does not complete the full Windows
host/browser gate above.
