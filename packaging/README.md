# Distribution repository submissions

These recipes support distribution submissions for DeskPort. For the current
desktop packages, use the [DeskPort 2.9.0 release](https://github.com/keithxc/deskport/releases/tag/v2.9.0)
and [the Linux installation guide](../docs/LINUX_PACKAGES.md).

## AUR

`aur/deskport-bin/PKGBUILD` is a submission recipe pinned to 0.6.3. It downloads
the versioned upstream portable archive and verifies its SHA-256. It retains the
private runtime and component notices, supplies desktop entries, and leaves
host/device setup to the user. The package provides and conflicts with
`deskport`. Only x86_64 is qualified.

New-account registration is temporarily closed according to the notice reported
by the maintainer on 2026-09-27. Do not automate retries or request a manual
registration queue. Once registration reopens, register an AUR account and a dedicated SSH public key,
and verify the AUR SSH host key against Arch's published fingerprints. Check that
the package name is still free. In a normal Arch user session:

```sh
cd packaging/aur/deskport-bin
makepkg --verifysource
makepkg -si
makepkg --printsrcinfo > .SRCINFO
```

After reviewing the recipe and installed application, copy `PKGBUILD` and
`.SRCINFO` into the AUR Git checkout and push them. Do not upload the built
package, source archive or the DeskPort repository to AUR. Keep the recipe here and
in AUR synchronized. Publishing the AUR recipe does not put it in Arch extra.

For each stable release, change `pkgver`, reset `pkgrel` to 1, update and verify
the download hash, regenerate `.SRCINFO`, then repeat install/upgrade/removal
checks before pushing. Increment `pkgrel` for packaging-only changes.

## Nixpkgs

The [0.7.8 submission](https://github.com/NixOS/nixpkgs/pull/572043) is open for
upstream review. The local example below retains the 0.6.3 pin used for the
[earlier submission](https://github.com/NixOS/nixpkgs/pull/567358).
To run DeskPort 2.9.0 with Nix, use the
[versioned upstream flake](../docs/LINUX_PACKAGES.md#nixos-and-nix).

`nixpkgs/deskport/package.nix` is a standalone `callPackage` expression. It builds
the viewer from the release tag and the shared core from its exact public commit.
`host.nix` builds the private session host from DeskPort's vendored source archive,
with the same host overlays and FFmpeg lifetime fixes used by the release.
It deliberately does not inherit a moving `pkgs.sunshine` build recipe. The
session launcher isolates Sunshine's default files under DeskPort's private
runtime directory and disables migration from standalone Sunshine configuration,
including for `--version` and service-provided `CONFIGURATION_DIRECTORY`.

The host recipe and npm lock were adapted from Nixpkgs commit
`93108a538f079596c9a16c72cf03e9322782b6dd`; the viewer recipe was adapted from the
Nixpkgs Moonlight recipe. These packaging expressions and the adapted lock file
are provided under the MIT license in `nixpkgs/LICENSE`. The applications and
vendored components retain their original licenses.

The host still uses LizardByte's fixed-hash FFmpeg build-deps archive, matching the
original Nixpkgs Sunshine recipe and DeskPort's host ABI. Four translation units
are rebuilt with DeskPort's backports and matching-source/header checks. This is
not a claim that every bundled media dependency is compiled from source here.

To prepare an upstream submission against a current Nixpkgs checkout:

1. Copy `nixpkgs/deskport/` to `pkgs/by-name/de/deskport/`.
2. Add `keithxc` to `maintainers/maintainer-list.nix` in its alphabetical position,
   using the identity in the candidate's `meta.maintainers`.
3. Replace the inline maintainer record with `maintainers = [ lib.maintainers.keithxc ];`.
4. Format the expressions with Nixpkgs' formatter and build with sandboxing:

   ```sh
   nix-build -A deskport --cores 4 --max-jobs 1
   nix-build -A deskport.tests.version -A deskport.tests.hostIsolation -A deskport.tests.headlessStartup --cores 4 --max-jobs 1
   ```

5. Review the output, permissions and dependency closure. Create a maintainer
   commit and a `deskport: init at 0.6.3` package commit.
6. Review the current [Nixpkgs contribution policy](https://github.com/NixOS/nixpkgs/blob/master/CONTRIBUTING.md#automationai-policy).
   The responsible human maintainer must review and understand the contribution
   before submission. Include the required `Assisted-by: OpenAI Codex (GPT-6)`
   trailer and disclose assistance separately in the PR body.
7. Submit the reviewed PR, respond to review, then verify merge and actual channel
   availability before advertising `pkgs.deskport` as available.

For an isolated pre-submission check without modifying Nixpkgs:

```sh
nix-build --no-out-link --cores 4 --max-jobs 1 --expr \
  'let pkgs = import <nixpkgs> {}; in pkgs.callPackage ./packaging/nixpkgs/deskport/package.nix {}'
```

The candidate intentionally limits supported platforms to `x86_64-linux`.
A small backport rejects non-Vulkan probe windows before calling SDL's Vulkan
extension API. Current SDL2-compat otherwise crashes during headless startup.
The same fix is included in DeskPort's source tree; drop the backport when a
future release includes it. The package tests cover the CLI version, private
host configuration and headless GUI startup.

No NixOS module is submitted in this first package contribution. System input,
trusted-interface firewall rules, LAN discovery and startup remain explicit
administrator configuration; installing a package does not activate them.

## Verification scope

See [VERIFICATION.md](VERIFICATION.md) for the exact checked inputs and results.
Package/CLI validation does not establish GPU support, live capture, physical
input, desktop-session integration or performance on every distribution.
