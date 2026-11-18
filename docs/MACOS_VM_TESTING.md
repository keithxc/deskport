# Disposable macOS package testing

## Purpose and scope

Use one macOS 26 ARM64 guest on an Apple Silicon development Mac to test the
published DeskPort package without replacing or restarting the host installation.
The VM is not an acceptance substitute for physical GPU decoding, streaming
latency, HiDPI, screen capture or remote-input permissions.

The first lab uses a pinned Cirrus Labs Tahoe base image and Tart 2.37.0. Exact
image digest, archive checksum and image sizes are in
[`scripts/macos-vm-lock.json`](../scripts/macos-vm-lock.json). Tart's upstream
repository currently redirects to `openai/tart`; this version ships under
FSL-1.1-ALv2. The lab does not require Orchard, a subscription, or an Xcode image.
See the [upstream quick start](https://tart.run/quick-start/) and the
[pinned source/license](https://github.com/openai/tart/tree/2.37.0).

## Disk and resource budget

The pinned Tart tool, private `TART_HOME`, guest disk and ownership lock remain
under `~/Library/DeskPortVM.noindex`. New package-test inputs and results use
the checked build workspace; the guest still sees its existing `input` and
`results` shared-directory names. `DESKPORT_VM_RUN` selects a stable run name.
It is not installed into `/Applications`, and does not use or prune `~/.tart`.

- Keep at least **80 GiB free** on the host volume.
- Stop managed download/run operations if allocated lab storage exceeds **65 GiB**.
- Check both limits every five seconds while cloning or running. These are monitored
  safety limits, not an APFS quota; concurrent unrelated writes can still consume space.
- Before the first download require 80 GiB reserve plus the image's 50 GB virtual disk.
- One VM, **4 vCPUs and 8 GiB RAM**. Keep the image's **50 GB virtual disk**;
  do not enlarge it automatically. Sparse disk capacity differs from allocated bytes.
- Clone once; prune only this lab's OCI/IPSW cache after cloning. Do not retain a
  second baseline, snapshots, suspended-memory files, Xcode or multiple OS images.
- APFS clones can cause `du` to count shared blocks conservatively. The separate
  host free-space check remains authoritative for available capacity.
- Results use fixed filenames, so later runs replace the previous report/screenshot.
  Copy a report deliberately if it needs to be retained; do not archive VM disks.
- Stop the guest after testing. No automatic background service is installed.

## Commands

From the repository root:

```sh
python3 scripts/macos-vm.py status
python3 scripts/macos-vm.py bootstrap
python3 scripts/macos-vm.py start
# In another terminal, while the supervised start command remains running:
python3 scripts/macos-vm.py exec /usr/bin/sw_vers
python3 scripts/macos-vm.py stop
python3 scripts/macos-vm.py prune
```

`bootstrap` downloads the checksum-pinned Tart distribution if needed, clones the
image by digest, configures CPU/RAM and removes the private image cache. Interrupted
image downloads can be resumed by rerunning bootstrap. A file lock rejects concurrent bootstraps/VM starts.

`start` is deliberately foreground and supervised. It disables host audio and
clipboard sharing. Boot uses Tart's default NAT; the guest test disables Ethernet
before starting DeskPort. The `--net-host` option in Tart 2.37.0 requires the
additional Softnet helper, so this lab does not use it or install a privileged
network helper. Only `input` (read-only) and
`results` (writable) are mounted. Never share the user's home, SSH keys, signing
keychain, work repositories or personal clipboard with this guest.

The upstream base image has a preconfigured account and Tart Guest Agent. Commands
use the agent's virtual socket channel, rather than requiring guest networking or
copying host credentials. The test disables guest Ethernet before launching the
application to prevent discovery of personal hosts.

## Package smoke test

Prepare these files under `~/Library/DeskPortVM.noindex/input`:

- `DESKPORT_VM_TEST_ONLY`: a marker identifying this dedicated test share.
- `DeskPort.zip`: the published, notarized ARM64 ZIP.
- `package.sha256`: expected SHA-256 followed by two spaces and `DeskPort.zip`.
- `expected-version.txt`: the expected package version.
- `test.sh`: a copy of `tests/test-macos-vm-guest.sh`.

For a complete start → test → shutdown cycle, run:

```sh
python3 scripts/macos-vm.py test /path/to/DeskPort-0.3.0-macos-arm64.zip
```

This command requires Python 3 and the host Xcode command-line Swift compiler
(to build a small window inspector; Xcode is not installed in the guest).
It checks the ZIP against the test lock, prepares the shares, waits for
the Guest Agent, runs the test and shuts down in a finally block. It does not
bootstrap a missing image implicitly. For manual debugging, start the VM and execute:

```sh
python3 scripts/macos-vm.py exec /bin/bash '/Volumes/My Shared Files/input/test.sh'
python3 scripts/macos-vm.py stop
```

The guest script refuses to run unless `kern.hv_vmm_present` is 1 and the
dedicated VirtioFS share/marker exists.
It tests a fresh installation, Developer ID identity, offline Gatekeeper assessment, CLI version/help, three GUI
starts, visible window geometry, duplicate activation and forced-exit recovery. It writes `latest.log`,
`version.txt`, `help.txt`, `STATUS.txt` and, if guest permissions permit,
`desktop.png` and three `window-*.json` reports to the results share. A process being alive alone does not prove the
window rendered correctly: inspect the screenshot separately. Screenshot failure
is reported explicitly and is not silently counted as visual acceptance.

The initial test lock is pinned to DeskPort 0.3.0. Update its expected version and
input checksum in `macos-vm-lock.json` deliberately when validating another release. Installation tests do
not authorize modifying the development host's application or running services.

## Cleanup and recovery

1. Stop only `deskport-macos26` with the wrapper's `stop` command.
2. Run `prune` to remove this lab's download caches.
3. Delete only known lab result files or the copied input ZIP when no longer needed.
4. To remove the whole lab, first stop its VM and confirm no Tart process uses it,
   then remove `~/Library/DeskPortVM.noindex`. This deletes the disposable guest;
   it does not touch the host DeskPort installation or other VM managers.
5. If a storage guard stops a run, inspect `status` and free only lab-owned files.
   Do not automatically delete Nix generations, releases, unrelated caches or user data.

If a guest permission prompt prevents a screenshot or later UI automation, record
that limitation. Do not disable host security controls, edit host TCC databases,
change the host login keychain or grant broad local-network exemptions.

## Verification record

2026-09-15: the pinned image booted successfully as macOS 26.6.2 (25G83),
VirtualMac2,1, ARM64. The VM has 4 vCPUs, 8 GiB RAM and a 50,000,000,000-byte
logical disk. The downloaded OCI cache was pruned to zero. After boot, APFS trim
reduced the complete lab to about 30.13 GiB, leaving 155.57 GiB free on the host.
The guest is stopped, its cache is empty and the test installation has been removed.
Local reports are under `~/Library/DeskPortVM.noindex/results`. The final
`macos-vm.py test` start/test/cleanup/shutdown cycle exited successfully with
`STATUS.txt` set to `PASS`; screenshot limitations below remain separate.

- CI [34870681467](https://github.com/keithxc/deskport/actions/runs/34870681467)
  passed the Nix build, CLI identity, streaming/translation checks and disk-guard tests.
- After explicit user approval, the guest source policy was set in System Settings
  to **App Store and Known Developers**. Gatekeeper remains enabled. The strict
  offline assessment now accepts the package as **Notarized Developer ID**.
- The checksum-matched 0.3.0 ZIP passed Developer ID signature, CLI version/help,
  three visible-window starts, direct duplicate executable handoff and forced-exit
  recovery. The test removes its installation before reporting PASS.
- The first-run Local Network prompt was denied during graphical setup, but
  returned on the automated fresh installation. The screenshot shows the rendered
  devices page behind that prompt; unobstructed interactive UI acceptance remains
  unverified. No local-network access was granted.
- The base image initially had Gatekeeper disabled. Its initial assessment remains
  discarded as security evidence; `initial-smoke.log` is historical smoke evidence only.
- The development host's installation, services and security policy were not changed.

For a new clone, initialize this guest-only source policy through System Settings
before testing third-party packages. Use `start --graphics` for setup, then `stop`
and `test` for the repeatable automated run. Do not disable Gatekeeper to pass.

The storage guard refused an oversized synthetic writer and prevented starting
a child under simulated low-space conditions. The guest-only marker check was
corrected to accept Apple's actual `AppleVirtIOFS` filesystem label. Stop waits
for the runner's lock to be released, and successful tests must explicitly remove
the test installation before writing PASS. A final guest `sync` flushes cleanup
and reports before Tart powers off the VM.
