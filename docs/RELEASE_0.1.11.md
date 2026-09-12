# DeskPort 0.1.11

2026-09-12. A focused desktop UI refresh using the existing Qt/media stack.

## Changes

- Only the active device recalls its session. Other devices show details and
  explain how to disconnect before switching; browsing settings or sharing
  preserves the current session.
- The session header participates in the device layout instead of covering cards.
- Compact device list by default, optional cards, and persistent pinned devices.
  Device identity and source indices stay separate from visual ordering.
- Sharing reports service, permission and saved-approval state separately, with
  direct permission actions. Service availability is not a live capture guarantee.
- Client settings separate remote audio from local hosting. Office, Clear and
  Smooth presets change only frame rate and bandwidth; adaptive mode labels its
  fixed resolution as fallback. Changes show their application timing.
- System, light and dark appearance; shared spacing/colors and visible button
  focus; narrower navigation and settings sections suitable for small windows.
- New UI copy translated into Simplified/Traditional Chinese, Japanese, Korean,
  German, French and Spanish.

## Validation

- macOS and Linux: 15 isolated UI checks, including 50 retained-session
  navigation cycles and identity-aware primary actions; 7 service/recovery checks.
- macOS: 6 desktop-state/lifetime checks, synthetic light/dark/card/list and
  narrow-window screenshots, plus explicit quality-preset and theme assertions.
- Linux Nix build and isolated executable/desktop-identity smoke checks.
- Seven-language translation coverage and whitespace validation.
- macOS packaging uses the locked Nix devShell, including its corrected npm
  dependency. Stable Aqua-session signing and extracted ZIP verification check
  signatures, relocatable dependencies and the executable's 0.1.11 version.
Native image/input quality and unattended access still require real-device checks.

The macOS artifact is a development preview signed with the existing development
identity, without Developer ID notarization.
