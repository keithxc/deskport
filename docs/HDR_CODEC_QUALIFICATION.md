# HDR and codec qualification

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

2026-10-17. Scope: desktop media adapters on macOS, Linux and Windows. A settings
checkbox, Main10 profile, compiled encoder or successful software decode does not
establish active HDR capture or HDR display output. No system color settings are
changed by this work.

## Implemented behavior

- The client intersects AV1, 10-bit and 4:4:4 profiles with the host's exact codec
  bits, then qualifies the exact final profile through the existing decoder /
  renderer probe, keyed by requested dimensions/FPS. Its compressed validation
  image remains the client's standard 720p sample, so this is not a throughput
  or arbitrary-resolution hardware guarantee. A
  Main8 bit never qualifies Main10 of the same codec. Automatic/software modes
  can use a working software decoder; forced hardware mode rejects software,
  including HDR and AV1 candidates. Rejected profiles are consumed in priority
  order. An empty list produces a local error before launch, without resurrecting
  an untested H.264 profile. Windows D3D11 checks HDR10 swap-chain presentation
  support on the actual output before accepting a 10-bit renderer. This check
  runs before setting the color space, which would otherwise make DXGI report
  the just-set space as supported even on an SDR output. Other renderers may
  provide their own tone mapping; otherwise the client qualifies an SDR profile.
  macOS HEVC probes create an actual hardware-only VT session for the same test
  frame, decode it, and inspect `UsingHardwareAcceleratedVideoDecoder` instead
  of inferring Main10 support from GPU names. Unsupported profiles can then use
  FFmpeg software decoding when allowed. Existing Sunshine HEVC Main8 deferred discovery and
  legacy H.264 behavior are retained.
- macOS 15+ on Apple Silicon captures documented SCK `RGhA` RGB half-float
  surfaces and converts them to video-range P010 with native Metal. The source's
  named `CGColorSpace` is checked; image or sample color attachments must not
  conflict with it. RGB does not require a guessed YCbCr matrix. sRGB is converted
  through linear light to BT.709 SDR; P3/PQ is converted through absolute ST2084
  and a linear P3-D65-to-BT.2020 matrix to BT.2020/PQ. Chroma is low-pass filtered
  at the encoder's left-cosited, vertically centered location. The converter
  preserves the full PQ signal through 10,000-nit white without a display tone
  map or an 8-bit intermediate. This describes signal values, not measured light.
  Pools reuse IOSurfaces and the GPU completes before passing the sample onward.
  Timing and additional native attachments are preserved; converted color-space
  and range tags replace source tags consistently. The encoder uses that same
  limited-range color description instead of retagging samples from client CSC
  preferences. Explicitly tagged native 10-bit YUV can still use VideoToolbox
  conversion; missing YUV matrix/transfer information fails instead of guessing.
- The RGB conversion is intentional: actual SCK `xf44` frames on the tested OS
  carried a color space without a YCbCr matrix, and native RGB pixel transfer did
  not preserve the required PQ highlight ramp. Synthetic analytic tests caught
  both issues. The new path uses system frameworks, without rebuilding or
  replacing pinned third-party media libraries.
- Active physical-source EDR headroom is checked by matching the selected
  `NSScreen` display ID. HDR capture uses Apple's canonical HDR preset with
  Display P3/PQ and accepts only a verified PQ source. Managed virtual outputs
  still publish SDR modes, so they produce **10-bit SDR**, never HDR. Software /
  BGRA capture stays SDR even on an EDR-capable screen. Older OS/Intel unsupported
  P010 probes fail synchronously and discovery retains supported 8-bit codecs.
  AVFoundation is not used as an uncertain 10-bit fallback. Idle heartbeat,
  bounded startup, stop and callback draining remain intact.
- The existing media contract carries active HDR separately from encoder profile
  capability. SCK does not provide mastering-monitor luminance/CLL for its canonical
  stream: these remain unknown/zero, without invented 1000-nit metadata. The macOS
  host omits unknown mastering SEI, and the desktop decoder avoids synthesizing
  an empty mastering block from control metadata. Known stream metadata takes
  precedence. Renderer/display qualification remains native; libplacebo's existing
  SDR tone mapping remains available. No new display-policy or wire fields,
  translations, shared-core changes or pin changes are introduced.

Apple documents the [SCK HDR API and color-space requirements](https://developer.apple.com/videos/play/wwdc2024/10088/)
and [VideoToolbox image conversion](https://developer.apple.com/documentation/videotoolbox/vtpixeltransfersession).
Microsoft documents the output-dependent [DXGI color-space support check](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-checkcolorspacesupport).
The checked local SDK also specifies that SCK HDR requires Apple Silicon and that
HDR dynamic range begins with macOS 15. This is an implementation prerequisite,
not proof of HDR light output on every compatible machine.

## Repeatable synthetic checks

All outputs must be under the checkout path returned by `scripts/build-paths.sh`.
Use a unique immutable run directory; `test-codec-color.py` refuses to replace it.
The synthetic unit and codec checks need no personal host, pairing, capture
permission or real clipboard. The separate opt-in native capture check requires
already-authorized capture access and an available local GUI resource.

```sh
nix develop --command python3 tests/test-video-formats.py
nix develop --command python3 tests/test-core.py

# Native macOS: prepare the exact host overlay stack, without compiling a host.
DESKPORT_HOST_PREPARE_ONLY=1 bash scripts/build-macos-host.sh
python3 tests/test-screen-capture.py
python3 tests/test-vt-hardware.py --require-main

# Opt-in only after coordinating local GUI ownership; no TCC prompt or change.
# Acquires local-ui.lock and captures only an owned, covered virtual output.
python3 tests/test-managed-color-capture.py --native --thread CURRENT-THREAD-ID \
  --output "$(bash scripts/build-paths.sh releases)/managed-color-UNIQUE-ID"

run="$(bash scripts/build-paths.sh releases)/hdr-codec-UNIQUE-ID"
mkdir -p "$run"
python3 tests/test-codec-color.py --output "$run/software"
python3 tests/test-codec-color.py --hardware videotoolbox --output "$run/videotoolbox"
# On an owned Linux build/GPU resource:
python3 tests/test-codec-color.py --hardware vaapi --device /dev/dri/renderD128 --output "$run/vaapi"
# On an explicitly available native Windows GPU; select its actual backend:
python tests/test-codec-color.py --hardware d3d11va --encoder-backend amf --output "$run/amf"
```

The Qt test exercises 18 hardware/software/absent decoder combinations plus an
all-failure case and mismatched host Main8/Main10 profiles using production list
operations. The native bridge test exercises actual Metal RGB-half conversion:
analytic sRGB/709 transfer, P3/PQ color bars with independently computed BT.2020
YUV golden codes (within two codes), and a dense PQ ramp with 877 distinct luma
codes, black 64 and 10,000-nit white 940 (within one code). Tagged synthetic
`xf44` checks also exercise full/video range, timing, metadata and actual gamut
conversion. Invalid source/metadata, idle/stop/timeout and late callbacks are
checked. YUV range conversion permits one 8-bit code of rounding; it is not
bit-exact resampling.

The native managed-output test creates only an isolated, owned virtual output,
paints an opaque floating-point Metal gradient over it, then captures the actual
SCK bridge into P010. It saves only dimensions and synthetic luma statistics.
It runs 1280x800, reopen, 1920x1200 at scale 2, then 1280x800, with fresh capture
sessions. It requires more than 256 observed luma codes, records child PIDs and
exits, verifies baseline display membership/modes/layout after helper shutdown,
and releases its lock. It does not connect a client, encode a stream or establish
network reconnect/input behavior. Do not run it during another GUI task.

The codec checker generates a luma ramp and six chroma bars directly in 8/10-bit
YUV. Ten-bit low bits are populated before encoding. It checks raw H.264, HEVC
and AV1 elementary streams, not container tags: decoded format, transfer,
primaries, matrix, limited range and frame count. HEVC/AV1 software references
are lossless and must match source bytes exactly. H.264 uses High profile at QP 1
with the bounded pixel check: x264 lossless would select High 4:4:4 Predictive,
which is outside the app's ordinary H.264 profile contract. GPU encoding must preserve chart values within the stated
mean-code tolerance (2 for 8-bit, 8 for 10-bit), and ten-bit output must retain
fractional codes. Explicit hardware surfaces plus `hwdownload` prove hardware
device-surface decode; their pixels must match the software decode of the
identical compressed stream. FFmpeg permits a framework software fallback for
HEVC on [VideoToolbox](https://www.ffmpeg.org/doxygen/8.1/videotoolbox_8c_source.html),
so a surface alone is insufficient evidence. For HEVC the checker additionally
uses the production native hardware-only decode of the identical first compressed
image and compares its actual pixels to software. The report explicitly records
one native hardware frame and all-frame device-surface parity. No installed
FFmpeg library is patched, injected into, or replaced. The JSON report records
platform, FFmpeg version, encoder, skips/failures, errors and hashes. Missing
GPU encoders are skips, never qualification. H.264 10-bit/HDR is not offered by
the DeskPort streaming contract and is intentionally excluded.

## Evidence and outstanding gates

Actual macOS test machine: Mac mini, Apple M4 Pro, 64 GB, macOS 27.0.1,
Command Line Tools macOS 27 SDK; utility FFmpeg 8.1.2. Linux codec machine: AMD
Ryzen AI 9 HX 370 / Radeon 890M, AMD PCI 1002:150e, kernel 6.18.56, VAAPI and
FFmpeg 8.1.2. These utility checks are distinct from the pinned host's FFmpeg or
a complete DeskPort capture/stream/display run.

| Check | Result |
| --- | --- |
| Production format selection | 18 decoder combinations and profile/empty-list checks pass |
| Native HEVC hardware-only profile probe | Main, Main10 and RExt 8/10-bit 4:4:4 pass on M4 Pro; runtime property verified |
| Native Metal RGB-half to P010 | Analytic SDR/PQ ramps, full PQ range, golden P3 color bars and 877 distinct 10-bit luma codes pass |
| Native tagged xf44 to P010 compatibility | BT.709 SDR, BT.2020/PQ and P3/PQ to BT.2020 synthetic checks pass |
| Native managed virtual output / SCK | Four create/reopen/resize cycles pass; 543–615 distinct luma codes, SDR remains explicit, topology restored |
| Software H.264 8-bit; HEVC/AV1 8-bit, 10-bit SDR, 10-bit PQ | All seven elementary-stream cases pass; six lossless references and bounded High-profile H.264 |
| VideoToolbox H.264 8-bit and HEVC 8/10-bit encode | Four synthetic cases pass; `allow_sw=0` |
| VideoToolbox H.264/HEVC/AV1 8/10-bit decode | Seven reference cases pass; native hardware-only HEVC first-frame pixels additionally match software |
| VideoToolbox AV1 encode | Not compiled/available; three cases skipped |
| AMD VAAPI encode/decode | Seven reference and seven hardware encoder cases pass; device-surface downloads match software decode |
| Shared core / Qt adapter | Passed; pins unchanged |
| Native Linux Nix application build | Passed with locked dependencies and a resource-only derivation wrapper; no store output link |
| Windows x64 compile checks | D3D11 renderer, session and FFmpeg decoder syntax checks pass against cached dependencies |
| Complete macOS host/client build | Pending; local free disk is below the workspace reserve; exact host overlays and native C/Objective-C sources tested |
| End-to-end managed 10-bit stream / network reconnect | Not run |
| Physical HDR source to HDR/SDR client display | Not run; no owned physical HDR display session |
| Windows D3D11 output gate, NVENC/AMF/QSV encode and native display | Not run; syntax checks are not native acceptance |
| Linux Vulkan renderer / compositor HDR output | Not run; VAAPI utility checks are not display acceptance |

The Linux resource wrapper uses the same locked package and adds `taskset` /
`renice` before configuration so actual Nix daemon builders stay on CPUs 0–3,
nice 15, at most four jobs. It changes no application or dependency source.
The first successful Linux build and Windows checks used `fe93fc1a`; subsequent
production changes are macOS capture/conversion only. Retained run manifests
record exact snapshots and evidence hashes.

Do not call this all-GPU qualification or physical HDR acceptance. Installed
applications/services and system configuration were not changed. No package
publication, activation, Seamless experiment or security change is part of these
checks. The native synthetic display was destroyed and owned processes/locks
released after each completed run.
