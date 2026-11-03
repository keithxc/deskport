# DeskPort 0.7.8

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes since 0.7.7

- **Office data saver:** the existing switch now favors code, terminals and
  documents. It renders the logical desktop at 1x, limits continuous changes to
  8 fps and negotiates audio off without overwriting saved settings. The video
  ceiling follows the resolution and preserves an already lower manual bitrate.
  Changing the effective resolution or audio configuration reconnects the stream.
- **Clearer updates and final-picture delivery:** sparse VideoToolbox submissions
  use their actual timing and complete the submitted picture before checking the
  output queue. Fresh pictures receive a quality bound when the resolution and
  bitrate allow it; large changes and recovery pictures receive a bounded
  refinement tail. Small updates avoid unnecessary refinement, while final
  changes, recovery frames and a periodic still-picture refresh remain deliverable.
  These controls target lower traffic; they are not a total-byte budget.
- **Normal streaming:** with data saver off, normal and automatic streams keep
  the encoder completion, timing and key-frame paths used before the saver work.
  Saver-specific cadence and fresh-picture quality controls stay scoped to saver
  sessions.
- **macOS desktop input:** keyboard, text, mouse and scroll injection run in a
  separate host process using Sunshine's existing input module. This fixes the
  repeated typing observed with data saver on in the tested iPhone-to-macOS LAN
  session. Helper failure falls back to in-process injection while a restart is
  attempted; session teardown releases held input in both processes.
- **Ordered fallback addresses:** save up to eight domain names or IP addresses
  per device, tried in the listed order ahead of discovered and last-used
  endpoints. Edit the list while other sessions remain connected; those sessions
  keep their current connection and the next connection uses the updated list.
  Per-address ports and moving a live session between addresses remain deferred.
- **One minimum desktop size for every host:** clients apply the shared
  orientation-aware 800x600 desktop floor to all host operating systems,
  including Linux. Existing Windows portrait 1x fallback behavior is retained.
- **Linux package checks:** isolated Wayland canvas checks remap their own
  minimized window before continuing, and package API checks bypass proxy
  environment variables for loopback requests. These verification changes were
  also included in the rebuilt 0.7.7 Linux packages.

The shared core is pinned to `10307719a4b139d6674eb96ebd013a8eec175b6d` in
both Git and Nix. Mobile clients are outside this desktop release.

### Data saver field measurement

One working day (2026-11-03) of coding, terminal and document work with data
saver on, measured from DeskPort's own traffic counters and diagnostic logs. A
NixOS Linux client (4K display, 190% scaling) connected to an Apple Silicon
macOS host running the code in this release, including the separate input
process.

- Whole day: the client's daily traffic counter recorded 578.9 MB. The only
  connection lasted 343 minutes without a disconnect, which bounds usage at
  about 101 MB per hour if all counted traffic came from it.
- Received video over that connection (1,967 samples, about one every 10 s):
  mean 92 kbps, median 53 kbps, 90th percentile 141 kbps; median receive rate
  8 fps; round-trip time median 20 ms, 90th percentile 32 ms; network loss in
  8 of 1,967 samples.
- One hour in detail: the stream was 2040×1228 at 1x instead of 4080×2456 at
  2x, encoded with HEVC on VideoToolbox. The 992 kbps cap never triggered a
  congestion step-down. Host encoding averaged 82 kbps (median 36, 90th
  percentile 203, peak 917 kbps) at 6.9 fps on average, with a largest frame of
  130 KB and 22 key frames. The client received 45.6 MB of video data, and
  DeskPort's counter corresponds to about 83–86 MB per hour. Bytes on the wire
  are about 1.8 times the video data, because small frames are padded to full
  packets and carry one parity packet.

Everyday development used about 85–100 MB per hour, below the design target of
about 300 MB per hour. This is one measured scenario, not a guaranteed figure.

## Downloads and scope

- macOS arm64, macOS 26+: Developer ID signed and Apple-notarized DMG and ZIP.
- Windows x64: full Setup and portable viewer ZIP. The application and installer
  are unsigned; bundled driver signature validation is a separate check.
- Linux x86_64: DEB, RPM, Arch package, AppImage, portable archive and Nix.
  Native packages and the portable archive require glibc 2.39 or newer.
  AppImage uses a private runtime; GPU, capture and compositor prerequisites
  still apply.
- Linux x86_64 Flatpak: viewer only, using Freedesktop Platform 25.08.

Fully quit the previous application before upgrading. See the release's
`SHA256SUMS.txt`, `PROVENANCE.json` and `VERIFICATION.txt` for exact packages,
source revisions and checks, with Linux details in `PROVENANCE-linux.json` and
`VERIFICATION-linux.txt`. Source is referenced by repository and exact commit;
upstream notices remain inside packages.

## Validation boundary

The [roadmap](../ROADMAP.md) records live checks of a Linux KDE client with a
macOS host, with data saver both on and off, and an iPhone client with a macOS
host on the LAN, with data saver on. The latter used the separate input process
and no longer repeated typed keys. These checks used private test builds before
the release packaging; they do not establish live acceptance of every final
package or every platform.

The field measurement above covers one Linux-to-macOS working session using
DeskPort traffic counters and diagnostic logs. It does not measure every byte
billed by a network carrier, guarantee a fixed hourly budget, or establish
savings for every workload and platform. Windows and other Linux host/encoder
combinations have no new live saver acceptance claim. The Linux host desktop
floor from an iPhone and live LAN/WAN fallback switching also remain
unverified. Isolated tests and successful builds do not establish physical
input, audio, HDR, display timing or latency on every system.
Existing compositor limitations remain documented in the
[PC support matrix](../PC_SUPPORT_MATRIX.md), including Debian KWin's older
virtual-display protocols and the historical Omarchy fullscreen-restore limit.
Updating mynix prepares configuration builds; activation and live use remain
manual.
