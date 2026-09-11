# DeskPort 0.1.4

Released 2026-09-11. This release makes HiDPI client text sharp, handles client
sleep and shutdown cleanly, prevents a host hang during reconnection and shortens
adaptive resizing.

## Changes

- Adaptive workspaces stream the client's drawable pixels 1:1. Clients at 150%
  scale or more get a 2x HiDPI host desktop instead of an upscaled 1x image.
  Cached 1x window workspaces are discarded once.
- Linux clients hold a logind delay inhibitor and end the stream before suspend
  or hibernate, without quitting the remote application.
- SIGTERM and SIGINT end the session and stop the bundled host, so logout,
  shutdown and `systemctl stop` no longer wait for SIGKILL.
- Returning from the control center no longer repeats connection-termination
  handling or error dialogs.
- The bundled macOS host bounds its first-frame wait. A reconnection racing a
  virtual display mode change previously blocked the host until it was restarted.
- Adaptive resizing skips the launch-warning toast wait and the segue delay,
  about 5 of the 7.5 seconds measured for a resize.

## Validation

Linux Nix build, 14 UI checks and desktop-state checks passed. The macOS arm64
package passed strict signature and bundle checks. HiDPI streaming, SIGTERM exit
and the patched host were used in a real KDE Wayland to macOS session. A real
suspend/resume cycle and the timing of the faster resize are still to be measured.
