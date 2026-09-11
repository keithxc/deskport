# DeskPort 0.1.3

Released 2026-09-11. This release adds a route back to the device list while a
remote session keeps running, and fixes adaptive resizing that could stall.

## Changes

- Closing the remote window releases input, hides the viewer and opens the device
  list without ending the session. A banner there returns to the remote desktop.
- The tray menu separates **Open device list** from **Return to remote desktop**;
  a second application launch opens the device list.
- Fix adaptive resolution changes stalling on "Adjusting resolution": the
  continuation page is now created in the StackView's context instead of the
  page it replaces.
- Guard macOS native display mode detection against a display that disappears
  during startup.
- macOS packaging checks signing access before building.

## Validation

Linux Nix build, macOS arm64 package with strict signature and bundle checks,
14 UI checks and 30 retained-window transition cycles passed. The control center,
tray switching and adaptive resizing were accepted in a real KDE Wayland to macOS
session.
