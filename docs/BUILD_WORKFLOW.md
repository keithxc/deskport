# Managed desktop builds

Use `bash scripts/build.sh` for new desktop builds. Existing platform recipes
remain the implementation, while this entry point owns paths, logs, source
identity, artifacts and cleanup. It never installs an application, restarts a
service, switches a Nix configuration or deletes a Git checkout.

## Commands

```sh
# Read-only: show every resolved directory; no directory is created.
bash scripts/build.sh paths --target macos --run example

# Linux x86_64 build host: Nix package plus isolated CLI smoke check.
bash scripts/build.sh build --target linux-nix --run example-linux

# Apple Silicon Mac: local signed package, using the locked devShell.
bash scripts/build.sh package --target macos --run example-macos

# Developer ID distribution, including app/DMG notarization and Gatekeeper.
bash scripts/build.sh package --target macos --run example-distribution --distribution

# Resume an interrupted notarization without rebuilding the candidate.
bash scripts/build.sh notarize --target macos --run example-distribution

# Linux x86_64 build host: full Windows client/host and installer audit.
bash scripts/build.sh package --target windows --run example-windows

# Explicit Linux portable selection; the default is AppImage only.
bash scripts/build.sh package --target linux-portable --run example-appimage

# All native distribution packages, portable archive and client-only Flatpak.
bash scripts/build.sh package --target linux-portable --run example-linux-all --format all --flatpak

# Rebuild the same application version under distinct immutable download names.
bash scripts/build.sh package --target macos --run example-macos-r2 --distribution --asset-suffix r2
```

Omit `--run` on `build` or `package` for a UTC timestamp and source revision ID.
An existing run is never silently reused or overwritten. Run commands on the
appropriate host: macOS on an Apple Silicon Mac, Linux/Windows on an x86_64
Linux builder. The entry point does not synchronize or modify another checkout
over SSH. Apple/Android mobile clients remain in their private repository.

Use `--asset-suffix r2` when a rebuilt version needs distinct download URLs.
Only final filenames change: for example `DeskPort-0.7.4-macos-arm64-r2.zip`
and `deskport-0.7.4-1-x86_64-r2.pkg.tar.zst`. The application and package
versions stay unchanged. Build metadata, supporting public records and checksum
filenames receive the same suffix, and their entries reference the final names.
Suffixes contain 1–32 ASCII letters, digits, dashes or underscores and start with
a letter or digit. Conflicting destinations are refused before renaming.
The suffix is fixed in `run.json`; `notarize` inherits it and refuses a different
explicit suffix. Repeating a completed notarization checks the recorded assets
without regenerating packages or adding the suffix again. Publish, verification
and cleanup use the run's recorded names; no suffix argument is needed.

For Linux portable packaging, `--format all` produces AppImage, DEB, RPM, Arch
and a portable `.tar.gz` containing the installed application tree and notices.
Add `--flatpak` to include the client-only Flatpak in the same run's recorded
assets and checksums; this needs the host's `flatpak` command. Flatpak staging
stays in disposable work. Neither option exports a separate source archive.
Set `DESKPORT_JOBS` to cap parallel compilation, and optionally set
`DESKPORT_LINUX_CPUSET` (for example `0-3`) and `DESKPORT_LINUX_CPU_LIMIT`
(for example `2`) to explicitly constrain the build container's CPUs.

`build --target macos` currently invokes the existing application packager,
including local signing. `build --target windows` stops after host compilation.
Linux Nix builds use `nix build -L --cores 4 --max-jobs 1 --no-link
--print-out-paths`. Stdout goes directly to `records/<run>/linux-nix/nix-outputs.txt`;
stderr remains in the private build log. After a successful exit, the runner
requires exactly one absolute `/nix/store/<hash>-<name>` output that is an
existing directory, records it as `nix_output` in `run.json`, and passes that
store path directly to smoke checks. `steps.json` records the exit status and
separate stdout/log SHA-256 digests. Empty, multiple or malformed outputs stop
the run before smoke checks; stderr is never parsed for output paths.

The managed run creates no store symlinks or GC roots. Store retention belongs
to the delivery configuration prebuild, which maintains only the fixed pending
and rollback roots. Text records do not retain store contents against GC.
Cleanup retains those records without deleting store contents; historical
runs containing `work/result` remain cleanable with `clean --work-only --apply`,
which unlinks the old result link without following it.

## Directory ownership

```text
~/mygit/build/deskport/<checkout-name>-<checksum>/
  dependencies.noindex/<target>/    retained inputs and compiled libraries
  runs.noindex/<run>/<target>/
    work/                          application objects and package staging
    dist/                          installers, checksums and small build metadata
    logs/                          private per-stage logs
    tests/                         disposable test data
  records/<run>/<target>/
    run.json                       source identity, assets and output fingerprints
    github-receipt.json             release/asset IDs, URLs and verified hashes
    cleanup-plan.json               exact proposed deletion paths
    cleanup-journal.json            completed deletions
  locks/                           serializes operations for each target
```

`DESKPORT_BUILD_ROOT` overrides the outer project root. Managed runs ignore
legacy individual output overrides so every output stays in its recorded run.
All application bundles, including signing and ZIP extraction staging, are
under `.noindex`. Legacy platform entry points retain their documented output
overrides; using them directly does not register an automatically cleanable run.
Socket-based smoke fixtures use a short, invocation-owned directory under
`DESKPORT_BUILD_ROOT/t/` to stay below Unix-domain socket path limits; it is
removed when the smoke command finishes, including ordinary failure paths.

Windows dependencies are built at stable cache paths keyed by locked inputs,
toolchain and recipes. Application runs link to those dependencies, never the
reverse. Linux portable dependencies use a recipe-keyed cache. The Mac retains
its verified FFmpeg extraction cache and the existing Nix toolchain cache.
Old CMake/qmake caches are not moved: their absolute paths may be embedded.
When an isolated checkout can reuse another checkout's retained dependencies,
pass `--dependency-cache /absolute/path/dependencies.noindex/windows` (or the
matching target). The existing directory must carry its `.keep` marker and have
no symlink ancestors. Dependency recipes still select their input hash and hold
their cache lock. Work, packages, logs and receipts remain in the new run.

`bash scripts/build.sh cache --target windows` reports retained cache usage.
New builds stop if managed run outputs exceed 30 GiB or dependency caches exceed
40 GiB. The default free-space reserve is 80 GiB on macOS and 20 GiB on Linux;
`--min-free-gib`, `--max-work-gib` and `--max-cache-gib` explicitly adjust a run.
The runner checks available space while its child command is running and stops
that command if the reserve is crossed. This is a guard, not a filesystem quota;
other processes and external build daemons can also consume disk space.
Obsolete dependency caches require separate inspection and explicit cleanup.

## Private signing configuration

Copy [the example](../config/build-signing.env.example) to
`~/.config/deskport/build-signing.env`, replace every placeholder with your own
values and set mode `600`. This file is parsed as literal assignments, never
executed as shell code. Environment variables may supply the same settings.
The file must stay outside the checkout and build output trees.

Keep private keys and notary credentials in your own keychain. Personal signing
configuration, private keys, account credentials and notary logs do not belong
in Git or source archives. Public certificates and upstream license notices
remain available for provenance. The same-session signing preflight in the
platform packager remains mandatory. See [macOS packaging](MACOS_PACKAGE.md).
Signed application binaries necessarily contain public certificate/signature
metadata; that metadata is different from the private signing key.

`check-build-privacy.py` checks tracked source and explicit source archives.
It reports locations and fixed reasons without printing matched values. Public
metadata contains source revisions and artifact hashes, not local paths, signing
configuration or private build logs. These checks supplement review; they are
not a claim that every possible secret format can be detected.

## Upload, verify and clean

Publication requires successful packaging from clean, unchanged source and exact
artifact hashes. Public macOS assets also require completed notarization. New
releases upload only installers, checksums and small build metadata. The metadata
links to the exact public repository commit used for the build.

The managed workflow does not automatically generate or upload application
source archives, dependency source bundles or static relinking materials. This
avoids uploading another copy of source already available from the repository.
GitHub's automatic `Source code (zip/tar.gz)` links still appear for the release
tag; GitHub generates those archives, so the local release flow uploads nothing
for them. See [GitHub's release documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).
The standalone `scripts/generate-src.sh` and Windows material-export scripts
remain available for an explicitly requested source/material delivery. Such a
manual export is separate from the recorded installer upload list, requires
source/privacy review and must retain third-party notices.

Upload uses an explicit recorded list, never a whole build directory or wildcard.
Existing assets with different bytes are never replaced.

Create the intended GitHub release through the separately authorized release
workflow first, then use its exact repository and tag:

```sh
bash scripts/build.sh publish --target macos --run example-distribution \
  --repo OWNER/REPOSITORY --tag vX.Y.Z --clean
```

This uploads the recorded artifacts, then compares GitHub's server-provided
asset `digest` (SHA-256) and byte size with the recorded local values. The default
verification does not download the installers again. `--clean` rechecks delivery
before removing this run's work, packages, logs and test data. Dependency caches
and small local verification/cleanup records remain. Draft releases are not
treated as completed delivery; their local packages are retained.

If GitHub does not provide a usable SHA-256 digest, default verification refuses
to authorize cleanup. Add `--download-verify` explicitly to `publish`,
`verify-upload` or `clean` when a full download and local byte-count/SHA-256 check
is required. This opt-in route streams the asset without retaining another local
installer copy. Cleanup still performs a fresh verification; a previous receipt
does not bypass a missing digest or changed remote asset.

For packages uploaded through another tool:

```sh
bash scripts/build.sh verify-upload --target macos --run example-distribution \
  --repo OWNER/REPOSITORY --tag vX.Y.Z
bash scripts/build.sh clean --target macos --run example-distribution        # plan
bash scripts/build.sh clean --target macos --run example-distribution --apply
```

To retire intermediate files from a failed or local-only build while retaining
its packages:

```sh
bash scripts/build.sh clean --target macos --run example-macos --work-only --apply
```

Cleanup only accepts this tool's recorded run. It checks tree fingerprints,
source state, `.keep`, Git metadata, signing files, ownership, mounts, active
processes/open files and references from other outputs. Cache/source symlinks
inside disposable work are unlinked without following their targets. Changed,
active or ambiguous trees are retained for inspection. Planning never deletes.
Cleanup needs `lsof` for open-file checks. On Linux it is available in the locked
`nix develop` environment; `doctor` reports missing tools before a run.

Historical build roots, existing releases, SDKs, installed applications,
rollback materials and Nix store/GC roots are outside this cleaner's ownership.
Use the workspace's reviewed `cleanup-build.py` manifest workflow for those
legacy outputs. Do not run `git clean -fdx` or delete whole build roots.
The intended steady state is source checkouts plus reusable dependency caches
and small release receipts, without a growing collection of installer copies.

## Implementation

`scripts/build.sh` runs `scripts/build-workflow.py`, a thin entry point for the
`scripts/deskport_build/` package: `common` (constants, refusals, Git and file
helpers), `runs` (layout, locks, records, budgets), `assets`, `package`
(signing preflight, logged phases, packaging), `release` (GitHub upload and
digest verification), `cleanup` and `cli`. Functions that tests replace are
called through their module, so one replacement covers every caller.

## Validation

```sh
python3 tests/test-build-workflow.py
python3 tests/test-build-paths.py
python3 tests/test-prepare-host-source.py
python3 tests/test-build-privacy.py
python3 tests/test-source-export.py
python3 winbuild/scripts/test-build-paths.py
python3 winbuild/scripts/test-host-boost.py
```

These cover path isolation, source privacy, optional source export, mocked GitHub
server-digest and explicit download verification, and destructive-cleanup
refusal/preservation cases. Platform
compilation, signing/notarization and physical streaming/input acceptance are
separate checks.

## Checked workspace outputs

The maintained build and test entry points resolve output with the local
`build_storage.py` adapter and the installed shared mygit resolver. On mm4 the
verified external volume and its reserve are required before creating output;
explicit output paths are checked too. The desktop project's existing run and
checkout layout remains unchanged. The short socket workspace is scoped by
project, physical checkout and task. The host-wide GUI ownership lock remains
separate from build output.

Direct `cmake`, `xcodebuild`, `swift`, `cargo`, Gradle and GUI invocations bypass
these entry-point checks; configure their output and disk validation explicitly
or use a maintained wrapper. This does not move or remove earlier outputs.
