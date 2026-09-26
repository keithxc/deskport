# Vulkan encoder lifetime backport

The pinned Sunshine package links the LizardByte FFmpeg prebuilts at upstream
revision `fb216b5facde2c97cb0ce2e75fb3228aa5ac21fa`. The Vulkan H.264, HEVC and
AV1 close functions in that revision omit CBS context/access-unit cleanup.
Repeated encoder probing and session teardown therefore retain parameter sets
and bitstream buffers even after the host's encoder and frame owners are gone.

`ffmpeg-vulkan-cbs.patch` preserves the upstream patch and attribution from
[FFmpeg commit 569674ac](https://github.com/FFmpeg/FFmpeg/commit/569674ac8dc00c11eaa96d86870b186cfb076639).
The adjacent upstream fixes are also backported:

- [ddfa8420](https://github.com/FFmpeg/FFmpeg/commit/ddfa842088cd86bd48bc4533f10a4b5badcbd02d)
  destroys image views for queued pictures during teardown.
- [b672ae39](https://github.com/FFmpeg/FFmpeg/commit/b672ae39053759557a42f3a54f31f8413a37d871)
  releases encoded session-parameter feedback buffers and propagates API errors.

The Nix derivation fetches the exact original FFmpeg source by hash, applies these
patches, and recompiles only the four affected archive members with the prebuilt
configuration and generated `avconfig.h`. Version and public-header comparisons
fail the build if the dependency no longer matches. Other archive members and
the remaining media libraries are copied unchanged; no ABI upgrade is intended.

This backport affects the Nix Linux host only. It does not change macOS media
prebuilts. Remove it when the pinned Linux prebuilt includes the upstream fix,
after repeating Vulkan reconnect validation. Do not silently relax the version
guard to accept a different dependency revision.

Validation must include real encoded Vulkan sessions and fully destroyed-session
allocator snapshots. Balanced host owner counters alone do not detect allocations
inside FFmpeg. Compare the original and patched host with identical capture,
encoder and receiver settings; verify that CBS allocation stacks no longer
remain in an exit-time heap profile. A headless encoded-frame receiver does not
replace deployed GUI first-frame/input acceptance after user activation.
