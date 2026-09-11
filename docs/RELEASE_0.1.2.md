# DeskPort 0.1.2

Released 2026-09-11. This maintenance release makes the completed desktop input
features work out of the box.

## Changes

- Enable system-shortcut capture and plain-text clipboard sharing by default.
- Migrate existing 0.1.1 profiles once so retained disabled or missing settings
  do not silently prevent Super+Space forwarding or clipboard sessions.
- Keep both controls available in Keyboard & pointer for users who want to turn
  either feature off after migration.

## Validation

Linux Nix build, macOS arm64 compilation, clipboard protocol, service lifecycle,
UI, settings migration and translation checks passed. Native KDE Wayland shortcut
capture and bidirectional clipboard behavior still require real-session acceptance.
