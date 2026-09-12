# DeskPort 0.1.5

Released 2026-09-12. This release stops DeskPort from fighting a declarative
system configuration over login startup, which had been launching two instances
on one machine.

## Changes

- A declarative system configuration (Nix home-manager) now owns login startup
  whenever it installs the entries. When the autostart entry or the user unit is
  a symbolic link into the Nix store, DeskPort reports the on-disk state instead
  of its own setting and never rewrites or removes those files. The preference
  switch becomes read-only and says where to change it.
- Previously both sides wrote the same two paths as indistinguishable plain
  files and overwrote each other on every launch and every rebuild.

## Validation

Nix build, 7 service checks and 14 UI checks passed on NixOS. A full
`nixos-rebuild build` of the consuming configuration passed with the generated
unit pointing at the release binary. The macOS package for this version has not
been built yet, so 0.1.5 is Linux-only until that asset is published.
