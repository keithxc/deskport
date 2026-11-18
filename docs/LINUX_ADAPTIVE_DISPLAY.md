# Linux adaptive workspace

Current backend scope reviewed 2026-10-17. See the
[PC implementation and acceptance matrix](PC_SUPPORT_MATRIX.md) for dated evidence
and gaps; historical compositor checks do not qualify every installed version.

The Linux native/Nix host creates a dedicated virtual desktop on supported
Wayland compositors. An explicit [X11 software-Xorg backend](X11_ADAPTIVE_DISPLAY.md)
leases an existing inactive dummy-driver connector at 1×; it does not add a
connector to ordinary physical-driver X11 sessions. A bound client can resize it over the existing authenticated
display-control connection. The output stays alive during an active display-control session, including video
reconnects. Full disconnection removes it after a short grace period; a later
connection recreates it. Sharing startup leaves the output absent until an authenticated display lease
admits a client; capture/encoder probing waits for that owned target. This lifecycle requires a
DeskPort client with display control.

## Backends

- **KDE Plasma / KWin 6.6+:** KDE screencast virtual output plus output-management
  custom modes. The packaged desktop entry grants only the helper's screencast
  protocol access. KWin permission checks remain enabled. During a client session the virtual
  output becomes primary and previously enabled physical outputs mirror it.
  Previously disabled outputs stay disabled. A snapshot records enablement, modes,
  scaling, rotation, positions, order and replication before each session.
  Disconnection removes the virtual output before restoring that snapshot; an
  independent recovery process also restores it after helper process death.
  Recovery is disarmed while idle so later local layout edits are not overwritten.
  KWin 6.7 uses the device registry instead of individual output globals. Its CVT
  custom modes may round refresh rates and widths; when no exact pixel mode is
  available, DeskPort sequentially replaces only its virtual output with an exact
  initial mode, preserving the admitted name and recovery snapshot. Capture may
  briefly reconnect to that same owned name; physical fallback remains forbidden.
- **GNOME / Mutter:** connection-owned ScreenCast `RecordVirtual`, PipeWire
  format negotiation and temporary DisplayConfig transactions. Supports extension,
  primary-only and compatible-mode mirroring, with a journaled independent recovery
  process. Output admission compares the full monitor identity, including its
  per-stream serial, and waits for DisplayConfig publication. Sunshine attaches
  to the admitted PipeWire object serial without a second picker. See
  [GNOME compatibility and validation](GNOME_DISPLAY.md).
- **Hyprland / Omarchy:** a dedicated headless output through public Lua/text
  runtime monitor APIs, with direct WLR capture. Development sources implement
  the shared mirror, turn-off-others and extension policies behind native
  capability checks. A locked guardian journals original topology and normal
  workspace assignments, excludes later outputs, verifies restoration and
  handles helper/host failure. Stable physical EDID identity, ordinary 8-bit
  sRGB and VRR-off state are required for physical topology leases; limited
  setups retain extension with a warning. See [Hyprland display leases](HYPRLAND_DISPLAY.md).
- **X11 / xf86-video-dummy:** explicitly selected inactive `DUMMY1`–`DUMMY15`,
  exact dynamic modes and an independent extension at 1×, with EOF/crash recovery.
  2× UI scaling and topology policies are unsupported and explicitly rejected.
  No Xorg/driver installation or physical output fallback. See the X11 document.
- Other desktops retain existing-screen portal capture and do not advertise
  adaptive-display support. An unavailable native backend reports an error rather
  than silently capturing another screen.

The common Portal API defines a `VIRTUAL` source, but availability and resizing
behavior depend on the compositor. It does not provide a universal arbitrary-mode
resize command. Backend-specific operations are kept behind the same helper
request/acknowledgment contract.

Sizes are bounded to 640×360–7680×4320 pixels, aligned to four. Wayland backends
support 1× and 2× scales when accepted by the compositor; the X11 backend accepts
only observed 1×. Successful replies require
observed pixels and scale, not just successful API submission. Failed or timed-out
requests are reported; a lost helper stops capture instead of falling back to a
physical screen. Only one approved device can control the display.

**KDE uses virtual-primary mirroring while a client is connected.** The physical screens
show the same workspace as the client; differing aspect ratios may add borders.
Disconnecting restores the physical layout captured before the connection.
This does not promise restoration of individual application window positions.
Monitor hotplug or manual layout edits during sharing are not yet acceptance-tested.

**GNOME extension preserves the physical display layout.** Primary-only temporarily
turns off previously active physical outputs. Mutter mirroring requires matching
physical pixel dimensions, scale and unrotated displays; unsupported mirror modes
are rejected before creating a virtual output. GNOME does not implement arbitrary
letterboxed physical mirroring. Hyprland policies have their own capability guards
in [Hyprland display leases](HYPRLAND_DISPLAY.md). Video may briefly reconnect
during mode changes. An active logged-in Wayland session,
PipeWire and the existing Sunshine input permissions are required. This does not
implement a GNOME login-screen service or automatic session login.

## Validation

`nix build` builds both the client and helper plus the patched host. Run the real
compositor tests with `nix develop -c python3 tests/test-linux-display.py
/path/to/deskport-display`; add `--gnome` with `gnome-shell` available, or set
`DESKPORT_GNOME_SHELL` to its executable. Tests create isolated D-Bus, Wayland,
PipeWire, configuration and data directories; they do not connect to personal
hosts or inject input. An independent Wayland observer checks modes, output
identity, physical pixel modes, EOF cleanup and crash cleanup. KDE additionally
checks mirrored outputs, virtual-primary priority and exact physical-policy
restoration. Add `--disabled-output` to cover a previously disabled panel.
The KDE/GNOME harness checks disconnect removal and recreation; GNOME checks unchanged
other outputs. Set
`DESKPORT_TEST_HOST=/path/to/deskport-host` to include real software-encoder
capture at 1280×720@1×, 1668×2388@2× and 1920×1080@1× after recreation, plus refusal to capture another output
after the owned display disappears.
Use `--gnome-native` with PyGObject and GTK 4 typelibs to run the complete
Mutter policy/recovery scenario in that private session. See the versioned
[GNOME validation and limits](GNOME_DISPLAY.md), including service-loss behavior.

The 0.3.6 Linux preview passed these capture checks and 12 successive mode/scale
changes on both KWin 6.6.6 (permission checks enabled) and GNOME 50.4. Host
lifecycle and binding suites each passed 27 tests; the UI suite passed 18 tests.

`nix develop -c python3 tests/test-host-lifecycle.py` checks host supervision,
resize rejection/timeouts and capture-descriptor cleanup. `--binding` checks the
existing authenticated control path. Hardware encoding, iPad rotation, text size,
touch/mouse alignment and reconnect behavior still need real device acceptance.

Hyprland has separate `tests/test-hyprland-display.py` fixtures and
`tests/test-hyprland-display-live.py` isolated-compositor checks. The recorded
2026-10-03 run exercised real software capture/encoding, three modes and
target-loss/crash cleanup. It did not establish physical Omarchy input/audio,
GPU encoding, reconnect or reboot/logout acceptance. See [CLI verification](CLI.md#isolated-verification).

## Upstream interfaces

- [Portal ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [KWin virtual output implementation](https://github.com/KDE/kwin/blob/Plasma/6.6/src/plugins/screencast/screencastmanager.cpp)
- [Mutter ScreenCast interface](https://github.com/GNOME/mutter/blob/main/data/dbus-interfaces/org.gnome.Mutter.ScreenCast.xml)

These compositor integrations are implementation-specific. Runtime capability
checks and explicit errors are intentional; this document does not promise
compatibility with every historical or future compositor version.

## Mutter topology candidate — 2026-10-17

The GNOME adapter now interprets the existing core session policies. Extension
preserves the physical layout; primary-only makes the owned virtual monitor
primary at the desktop origin and disables only displays active at admission.
Matching-mode mirroring groups those displays with the owned virtual monitor.
It preserves their pixel modes, scale and transform. Mutter's clone group uses
one mode size, scale and transform: a request that differs from any active
physical monitor is rejected before creating or resizing the virtual workspace.
There is no arbitrary-size physical mirroring/letterboxing promise. Extension
and primary-only remain alternatives the user can explicitly select.

A guardian takes an exclusive per-Mutter-session lock, replays pending recovery,
and atomically journals the admission snapshot before RecordVirtual or a layout
change. Resizes never replace this snapshot. Full monitor identity includes the
connector, vendor, product and serial. A prepare-removal replay re-enables the
local monitors before stopping capture; EOF/SIGKILL also triggers an independent
post-removal replay. Restore skips detached identities and preserves monitors
added after admission. Configurations are verified, applied temporarily, and
read back before success. A failed recovery retains the journal; no persistent
Mutter monitor preference is written. A different compositor instance gets a
separate recovery scope rather than replaying stale hardware state.

This uses Mutter's compositor-specific D-Bus interfaces, not the portal API:
[DisplayConfig](https://github.com/GNOME/mutter/blob/50.1/data/dbus-interfaces/org.gnome.Mutter.DisplayConfig.xml)
documents monitor identities, clone groups, scales, primary flags, verification
and temporary application. Mutter's
[mirror configuration implementation](https://github.com/GNOME/mutter/blob/50.1/src/backends/meta-monitor-config-manager.c)
requires a common mode size. [ScreenCast](https://github.com/GNOME/mutter/blob/main/data/dbus-interfaces/org.gnome.Mutter.ScreenCast.xml)
provides the connection-owned virtual stream; PipeWire still determines its
requested pixel size. Unsupported modes and compositor policy denials are errors,
never successful extension acknowledgments for mirror or primary-only requests.

Run `DESKPORT_GNOME_TEST_SESSION=1 python3 tests/test-gnome-display.py
/path/to/deskport-display` only in a reserved isolated GNOME VM/session with
PyGObject. It checks native clone/primary-only/extension state, 1x/2x negotiation,
release/recreation, EOF/SIGKILL and unsupported-policy refusal. Helper frame
negotiation is capture evidence; it does not establish a Sunshine network stream,
hardware encoding, physical input or latency acceptance.

Mutter can re-enable old physical monitors when another output is hotplugged.
DeskPort observes `MonitorsChanged` and rechecks only its admitted policy scope.
A newly added output remains enabled and is never mirrored or disabled by this
lease. Mutter also requires adjacent logical monitors: when keeping an unowned
output in place prevents a valid physical policy, the existing verified virtual
workspace remains usable with an explicit `layoutWarning`. The host UI displays
its existing physical-layout warning; local records contain time, policy,
original and observed layouts and the native API error. No successful initial
policy admission is issued for an unsupported policy. A missing owned output,
wrong workspace pixels/scale or lost PipeWire connection still ends the owner.
See Mutter's [configuration validation](https://github.com/GNOME/mutter/blob/50.1/src/backends/meta-monitor-config-store.c).

Removing an owned output lets Mutter reflow the remaining outputs. Restoration
preserves new outputs' identities, enabled state, modes and scale; their origins
may change through the compositor's automatic reflow. It does not take over a
foreign virtual monitor to force an old arrangement. A pending original recovery
checkpoint is never replaced by a new admission. Startup attempts recovery first;
when a saved mode is unavailable but an independent workspace can be verified, a
new lease can proceed with the existing local-layout warning and original
checkpoint retained. Idle recovery retries it after owned-output removal.

### Native verification scope and open fault

The isolated GNOME Shell 50.1 software VM verifies mirror/primary-only/extension
admission and observed topology; extension and primary-only negotiate
1280x800@1x, 1668x2388@2x, 1920x1080@1x and 2560x1440@2x. Matching-mode mirroring
uses the fixture's original 1280x800@1x physical mode. Sunshine's exact owned
PipeWire stream and software H.264 encoder are checked; missing owned outputs
must fail capture. Individual probes at all listed sizes pass, but repeated
capture startup intermittently stalls after format negotiation, including a
run with continuous guest repaint. The cause is not established. The complete
native matrix remains **FAIL**; this is not stable streaming acceptance.
Releasing, recreating, EOF and SIGKILL restore the original
physical layout. Guardian death, pending/unavailable-mode checkpoints, foreign
output insertion/removal, refusal of pre-existing foreign takeover and a physical
2x mode/scale restoration are also exercised. Guest-only GTK receives a click
and fixture keyboard text through Mutter's RemoteDesktop API. This does not test
Sunshine's production network/input transport. Shared-core checks, the 40 host
lifecycle checks and native Nix packaging pass.

**FAIL: compositor-wide PipeWire service loss on GNOME 50.1.** The helper exits
and retains its recovery journal, but Mutter's DisplayConfig becomes unresponsive,
including after the service is restarted. A separate direct-Mutter probe creates
a fixed-mode RecordVirtual monitor, applies a virtual-only temporary configuration
and interrupts/restarts the guest PipeWire service. It reproduces the same timeout
without running DeskPort or Sunshine. This establishes a compositor/API boundary;
it does not establish the upstream internal cause. This failure remains an
activation/merge gate, not a passing recovery result. Mutter's
[stream-source error handling](https://github.com/GNOME/mutter/blob/50.1/src/backends/meta-screen-cast-stream-src.c)
closes a source on a lost PipeWire core; DeskPort cannot promise successful layout
transactions while that compositor API is unavailable. Run the destructive test
in a separate fresh VM with `--pipewire-loss`; the default native matrix does not
restart the compositor's dependency.

Physical GPU encoding, real connector hotplug/detach, mixed-monitor hardware,
production client streaming/input and latency remain untested. A virtual-output
hotplug fixture does not establish physical connector acceptance. No deployed
service, system configuration, release or installed application is changed by
these checks.
