# DeskPort project instructions

- Discuss work in Chinese; public documentation, code and commit messages use English.
- Preserve upstream notices and keep changes to the media pipeline small and justified.
- Primary target: Linux KDE Wayland / AMD client to macOS Sunshine host.
- Persistent background session and one-action window recall are the first milestone.
- Use an independent application ID and settings directory. Never migrate or modify
  the user's Moonlight settings, pairing credentials or running services implicitly.
- Hiding a window must release remote input; disconnection must preserve local control.
- Do not claim native platform, visual, latency or live-input acceptance from compilation alone.
- Validate code changes with `nix build` and appropriate targeted checks. Use isolated
  test configuration; do not connect to personal hosts or inject input in CI.
- Do not publish personal hostnames, addresses, credentials, real clipboard data,
  employer material or screenshots of work applications.
- Keep `docs/ROADMAP.md` current; do not implement deferred features without a reason.
- Prefer the locked project devShell for macOS build dependencies; run packaging
  through `nix develop`. See `docs/MACOS_PACKAGE.md` for commands and validation.
  Keep Apple's SDK/compiler/signing and pinned upstream media prebuilts in place.
  Inspect binary-cache availability before dependency upgrades. If migration
  requires substantial third-party source builds or complex workarounds, retain
  the working toolchain instead of forcing an all-Nix conversion.
- Before macOS packaging or installation, read `docs/MACOS_PACKAGE.md`. Run the
  signing preflight in the same execution session as packaging. A successful
  manual helper signature does not prove automation can access the private key.
  Record the verified deployment route and rollback path after installing.
