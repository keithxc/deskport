# DeskPort 0.1.8

Released 2026-09-12. The tray icon separates the two things a resident client is
asked for: the left button moves the window, the right button opens the menu.

## Changes

- A single left click shows the window and hides it again. During a session that
  is the remote desktop; with no session it is the device list. The control
  center and the remote window are never up at the same time, so a click while
  the control center is showing brings the remote window back.
- The tray menu is now the right button alone, with four entries: open the
  device list, disconnect, restart DeskPort and quit DeskPort. Returning to the
  remote desktop moved to the left click, and stopping sharing moved to the
  device list, where the rest of the sharing controls already live.
- On macOS a menu attached to a status item is opened by either mouse button and
  suppresses the button action entirely, which leaves no left click to toggle
  the window. The menu is popped up as a native menu from the right button
  instead. Linux keeps the tray's own context menu.
- A double click no longer acts. It arrives after its own single click, so
  toggling twice would undo itself.

## Validation

A Nix build and the service and UI suites passed on pk4. The retained-window
transition test covers a tray toggle hiding and recalling the window. Built and
packaged on mm4. Live tray clicks on both desktops remain a user acceptance
check.
