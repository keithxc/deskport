# Linux virtual display helper

## Receiving keyboard and remappers

Both Linux host builds expose their existing uinput keyboard as
`DeskPort Keyboard`. The Nix host retains its non-default seat suffix, for
example `DeskPort Keyboard (seat1)`. This lets receiver-side evdev remappers
such as xremap select DeskPort separately from Sunshine's legacy
`Keyboard passthrough` device. In particular, an existing xremap
`--ignore 'Keyboard passthrough'` rule no longer excludes DeskPort input.
Explicit device allowlists or custom udev rules matching the old name must
be updated to include the new name.

The input path is remote key down/up → host uinput → receiver remapper →
desktop/application. DeskPort does not install a remapper or swap Meta/Ctrl;
the receiver's active application rules decide which shortcuts to translate.
Enable device hotplug watching in the remapper (xremap `--watch=device`) and
give it permission to read the host keyboard's evdev node. Keep any existing
sender-side remote-window bypass, so keys are not transformed on both ends.
If the receiver is itself forwarding keys to another remote desktop, its
focus guard may intentionally suspend remapping for that window.

The host still needs write access to `/dev/uinput`. Without it, the Nix host
cannot create its virtual keyboard; the portable host retains its upstream
X11/XTest fallback, which does not pass through evdev remappers. When the
remapper is stopped, the uinput keyboard sends the original keys directly.
Mouse, gamepad, key-code translation, release handling and non-Linux hosts
retain their existing behavior. No peer protocol or shared-core pin changes.

`tests/test-host-linux-keyboard.py` checks both vendored backends without
creating input devices. After activating a build, confirm the receiving
remapper selects `DeskPort Keyboard`, then check application shortcuts,
modifier release after disconnect and reconnection on the actual desktop.
Compilation and isolated regressions do not establish live input acceptance.

## Virtual display lifecycle

Output enumeration uses the KWin 6.7 device registry when available and retains
the individual device globals used by KWin 6.6. Both removal paths end the owned
lease without falling back to a physical capture target.

On KDE, DeskPort owns one virtual output through a dedicated Wayland connection. KDE
Plasma 6.6 or newer is required for virtual custom modes. The UUID-named virtual
output becomes primary, and physical outputs enabled before connection mirror its viewport. Physical
pixel modes, scale and rotation are preserved. An independent recovery process
restores the original physical enablement, modes, scale, rotation, positions,
replication sources and output order when the owner exits,
including SIGKILL. It is armed before the first layout change. Closing stdin, stopping the helper
or losing its Wayland connection releases the virtual output.

Full client disconnection removes the virtual output before restoring the physical
snapshot. The helper remains available and takes a fresh snapshot before recreating
the output for a later connection. Idle recovery is disarmed to preserve subsequent
local edits. Video-only reconnects retain the output within a display-control session.
Custom modes use KDE output management. It acknowledges only the observed current
pixel mode and scale. A protocol timeout closes the helper, preventing a late
configuration result from being mistaken for a newer request.

KWin 6.7 derives CVT custom timings, so their advertised refresh can differ from
the requested 60 Hz. The helper selects the closest advertised refresh while
requiring exact pixels and scale. With output-management v21, CVT also rounds
custom widths to multiples of eight. If no exact mode is available, the helper
sequentially replaces only its own virtual output using an exact initial mode,
retaining the admitted output name, policy and original recovery snapshot.
Sunshine reopens the same owned name after the PipeWire stream disconnects.
A rounded mode is never acknowledged as the requested size.

The three protocol XMLs and generated client bindings are from the pinned
Sunshine plasma-wayland-protocols submodule. The XMLs carry their upstream
copyright/license notices (MIT-CMU for output management/device and
LGPL-2.1-or-later for screencast; see COPYING.LIB). Regenerate with
`wayland-scanner client-header` and `wayland-scanner private-code`.

On GNOME, `gnome-display.cpp` owns a Mutter ScreenCast session and a PipeWire
sizing stream. It verifies owned pixels/scale, implements the shared policies
within Mutter's matching-mode/adjacency limits, and uses an independent recovery
guardian. Other clients' virtual outputs are never takeover targets. See
[Linux adaptive display](../../docs/LINUX_ADAPTIVE_DISPLAY.md).

2026-09-17: Failed local restoration releases the old lease and retains its
baseline for later recovery. Subsequent clients can recreate and verify an
independent workspace. Physical-layout policy failures become recorded warnings
when workspace pixels/scale are correct. Diagnostic layout snapshots are saved
under `$XDG_DATA_HOME/DeskPort/display-layout-events.jsonl` (default
`~/.local/share/DeskPort`). Wayland disconnects and unverified modes still fail.

On X11, `x11-display.cpp` leases only an explicitly selected inactive
xf86-video-dummy connector through an independent guardian. It adds exact modes,
extends the software X screen and restores the root framebuffer on release.
Only verified 1x is supported; global UI DPI, physical modes and system Xorg
configuration are never changed. See [X11 adaptive display](../../docs/X11_ADAPTIVE_DISPLAY.md)
for driver limits, ownership, journal recovery and isolated native checks.

## GNOME display leases

The Mutter adapter implements the shared session-display policy IDs through
ScreenCast and temporary DisplayConfig transactions. Its independent guardian
restores attached original monitors, preserves foreign outputs, and retains
failed recovery journals. See [GNOME display compatibility](../../docs/GNOME_DISPLAY.md)
for version evidence and the matching-mode mirror restriction.

The Mutter topology adapter and independent `--restore-mutter` guardian are in
`gnome-topology.cpp`. The existing shared policy IDs are unchanged. GNOME mirror
requests are deliberately restricted to the physical monitors' existing mode,
scale and rotation, because a Mutter clone group cannot express arbitrary-size
letterboxing while preserving those modes. See the candidate and native-test
boundaries in [Linux adaptive display](../../docs/LINUX_ADAPTIVE_DISPLAY.md).
