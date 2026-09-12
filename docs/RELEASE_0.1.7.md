# DeskPort 0.1.7

Released 2026-09-12. DeskPort runs as a resident background service, so on macOS
it now lives in the menu bar alone.

## Changes

- The macOS application is an accessory (`LSUIElement`): it keeps no Dock tile
  and no application menu, and the tray icon is the only entry point. Ordering a
  window front does not activate an accessory application, so opening the device
  list from the tray now activates DeskPort explicitly.
- SDL turns the process into a regular Dock application when it initializes
  video for a streaming session, which is what a session wants. The accessory
  policy is restored once the session ends, except across an adaptive restart,
  which keeps the window and comes straight back.
- Linux is unchanged: a backgrounded DeskPort opens no window, so it already had
  no task manager entry.

## Validation

Built and packaged on mm4. The application reports 0.1.7, and the packaged
binary carries that single version string.
