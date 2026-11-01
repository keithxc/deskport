# DeskPort project instructions

- New GitHub releases upload installers, checksums and small provenance records.
  Do not generate or upload separate source/relink archives by default; reference
  the source repository and exact commit instead. Preserve notices inside packages.
  Verify uploaded assets against GitHub's server-provided SHA-256 digest and size;
  full-byte download verification is explicit opt-in, never an automatic fallback.

## Managed build lifecycle — 2026-10-22

- Use `bash scripts/build.sh` for new desktop build/package runs; read
  `docs/BUILD_WORKFLOW.md`. Keep legacy entry points for explicit compatibility
  or resume work, and pass their output paths deliberately.
- Generated work, packages, private logs and tests belong to a uniquely identified
  run. Retain compiled third-party dependencies separately; never make a retained
  cache depend on a disposable run. Do not move existing CMake/qmake caches.
- Signing configuration stays outside Git and build output. The public
  `config/build-signing.env.example` contains placeholders only. Keep private
  keys and notary credentials in the operator's own keychain; run the privacy
  checks before exporting source or publishing materials.
- After an authorized upload, verify every recorded GitHub asset's server SHA-256
  digest and size before deleting its local package. Managed cleanup keeps small
  receipts and dependency caches, and refuses changed/active/protected trees.
  Legacy outputs still require the workspace's reviewed cleanup manifest.
- Respect the build free-space reserve and managed work/cache budgets. A build
  permission does not authorize publication, activation or deleting historical
  releases, source material, SDKs or Nix store roots.

## Project contracts

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
- Shared workspace policy and display contracts belong in the pinned
  `shared/deskport-core`, not a second local implementation. Read
  `docs/SHARED_CORE.md` before changing policy or wire behavior; update the Git and
  Nix pins together and run `tests/test-core.py` plus the affected adapter tests.
  Every core pin change must also advance `baseline_source_sha`/`baseline_core_sha`
  and `review_date` in `docs/pc-support-evidence.json` and the matching header of
  `docs/PC_SUPPORT_MATRIX.md` in the same commit, then pass
  `python3 .github/scripts/check-pc-support.py`; otherwise CI "Linux build" fails
  with "Core pin changed: review the PC source/core snapshot".
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
- Public macOS downloads must pass `scripts/release-macos.sh`: Developer ID
  signatures for every embedded code object, Apple notarization, stapled app/DMG
  tickets and Gatekeeper verification after ZIP extraction. Local development
  signatures are not sufficient. Preserve existing release assets and their hashes;
  use distinct asset names when adding notarized builds to a published version.

- Performance/test prereleases target only mm4 (macOS arm64 signed/notarized package)
  and pk4 (NixOS x86_64 flake build). Do not build or upload other platform or
  distribution formats for these prereleases unless the user explicitly requests them.

## Build output location — 2026-10-03

- Keep DeskPort generated files under `$HOME/mygit/build/deskport` on each host
  (`DESKPORT_BUILD_ROOT` overrides it). Use `bash scripts/build-paths.sh checkout`
  for this checkout's directory; the path includes a checksum of the absolute
  checkout path to separate worktrees. Do not create new build, staging, log,
  archive, or package directories beside the repositories in `mygit`.
- macOS and portable Linux package scripts resolve their paths automatically.
  macOS caches and staging bundles remain inside `.noindex` directories, with
  separate Nix/Homebrew directories. Existing per-path environment overrides
  remain supported; put explicit overrides under the shared build root as well.
- Give each test/release its immutable ID under the `releases` path returned by
  `scripts/build-paths.sh`; use that directory for candidates, downloaded assets,
  source snapshots and verification logs. Keep source worktrees independent.
- For native Nix builds, use `--no-link` or an explicit `--out-link` under the
  checkout build directory. Never relocate `/nix/store` or reuse a moved CMake
  or qmake cache. Older Windows cross-build scripts require explicit relocation
  of their work/output paths when used; do not launch them in the source checkout
  and assume they honor `DESKPORT_BUILD_ROOT`.

## DeskPort release and activation workflow — 2026-09-14

- Default delivery is a mynix update for pk4 (NixOS x86_64) and mm4 (macOS
  arm64), followed by the user's manual activation and live verification.
- Only run a formal/stable release workflow when the user explicitly requests a
  formal release. Other distribution installers (DEB, RPM, Arch, AppImage and
  Flatpak) are packaged only for an explicitly requested formal release.
  Do not expand to other platforms/distribution formats, publish
  mobile clients, or merge the DeskPort main branch by default.
- Build and verify the two required targets; publish the prerelease assets needed
  by mynix, then update and push its release URL/version/hash and pinned source.
  A prerelease needed for mynix does not authorize a formal/stable release.
- The user manually runs `rebuild switch` to activate the update. Wait for the
  user to confirm activation before checking the deployed version or live behavior.
- Never directly replace `/Applications/DeskPort.app`, use administrator prompts
  to install it, run `rebuild switch`, or stop/restart deployed DeskPort services
  as part of a release. A request to develop, test, publish or update mynix does
  not authorize those deployment actions.
- macOS arm64 and NixOS x86_64 are the prerelease targets unless explicitly changed.
- Explicit installation of the mobile client to the user's iPad is separate from
  the desktop host release/activation flow and does not authorize a host update.
