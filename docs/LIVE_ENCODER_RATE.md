# Live encoder bitrate adapters

Branch validation below records the original development checks. The combined
integration candidate still requires native application builds and current
platform acceptance; historical results are not a new integration pass.

## Scope (2026-10-17)

This change uses the existing optional `video-rate` contract and pinned media
libraries. The live transaction retains the negotiated dimensions, frame rate,
codec, chroma/HDR format, capture session and saved viewer limit. Fixed backends
use the existing reconnect path to negotiate their capped budget. No shared core
contract or dependency pin changes are required. Seamless is outside this work.

| Host path | Live adapter | Downgrade |
| --- | --- | --- |
| FFmpeg H.264/HEVC VideoToolbox | Native average rate, one-second data limit and congestion QP floor | Revoke live on rejection; restore previous properties; next ceiling request uses reconnect fallback |
| Windows native NVENC (pinned SDK 11/12/13 implementations) | `NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE` plus `NvEncReconfigureEncoder` | A missing capability or failed call reports fixed; restore last accepted config on failure |
| Linux FFmpeg NVENC | Fixed | Pinned wrapper reconfigures on frame submission but does not expose its asynchronous result; changing `AVCodecContext` alone is not an acknowledgment |
| Windows FFmpeg AMF | Fixed | Native AMF has dynamic properties; the pinned FFmpeg adapter initializes them without a supported host acknowledgment entry point |
| FFmpeg VAAPI / Vulkan / QSV / software / MF | Fixed | No qualified synchronous live adapter in these pins; retain negotiated configuration and existing viewer reconnect fallback |

Windows previously omitted the remote-rate, data-saver, pause and rate API
overlays from its host preparation. The same platform-neutral overlay chain now
serves all desktop build routes. It does not pull Linux capture/display code into
Windows. Both upstream pins are checked independently.

## Transaction and ownership

Native changes execute only on the owning encoder thread, between completed
frames. HTTP/TLS threads publish atomics; they never call the hardware encoder.
The native NVENC adapter retains copies of successful initialization and encoder
configuration, with no pointer to the temporary initialization stack. It rejects
nonpositive rates, rates above 200 Mbps and rates above the initial target.
Before native encoder creation, a configuration copy applies the host's
`max_bitrate` ceiling as FFmpeg already does. This also bounds drivers without
live support, without changing the client configuration or saved viewer limit.
The controller additionally respects the negotiated and host-configured ceilings.

Each candidate keeps the original codec/profile, input/output format, dimensions,
reference settings and frame rate. VBV size, initial fullness and peak bitrate
scale from the original configuration, avoiding rounding drift across repeated
changes. The adapter uses NVIDIA's bitrate-only reconfiguration without resetting rate control or
forcing an IDR, as required by the existing shared `VIDEO_RATE.md` contract. A driver rejecting
that transaction falls back instead. No capture or encoder session is recreated. A failed native call attempts to restore
the previous accepted config and permanently revokes live capability for that
encoder, even if restoration succeeds. A failed restore is logged explicitly.

The integrated VideoToolbox transaction snapshots average rate, data limits,
minimum/maximum QP and expected frame rate. It preserves the current saver
three-times burst limit, sparse cadence hint and per-picture quality policy.
The API acknowledges both ceiling and saver state after encoder application,
including a saver toggle at unchanged kbps.

VideoToolbox snapshots its readable average-rate/QP properties. Some hardware
accepts DataRateLimits but returns `kVTPropertyNotSupportedErr` on readback; the
adapter reconstructs the previous one-second limit from pinned FFmpeg's initial
`rc_max_rate` or its last accepted value. Partial mutation restores all five
properties. The next viewer request receives fixed/reconnect if a native call
fails. Autonomous adaptation then keeps the last accepted pacing target; it does
not repeatedly retry a failing driver or force-disconnect a viewer.

Every RTSP negotiation clears stale live capability, including synchronous and
non-smart sessions. The authenticated local rate API waits at most 250 ms for
the encoder thread to acknowledge the requested ceiling. A rejection or stalled
encoder returns `live:false` through the existing wire result, so the viewer's
existing reconnect path can apply the requested cap. Reconnect/activation policy
and saved user settings are unchanged. The viewer checks an initial saver cap
after the first video keyframe, and retains `live:false` across the existing
controller's reconnect. Requests wait for stream readiness under the controller
mutex; a user toggle during startup supersedes the initial request, and an
encoder generation counter confirms a cap again after stream replacement. The next stream then negotiates the actually capped
budget, instead of repeatedly assuming that protocol support implies a live
encoder. This is a target acknowledgment, not a
claim that measured elementary-stream bitrate equals the target on each frame.

## Reproduction and evidence

Run pure transaction/overlay tests with scratch under the resolved build root:

```sh
root=$(bash scripts/build-paths.sh checkout)
mkdir -p "$root/live-bitrate/tmp" "$root/live-bitrate/evidence"
TMPDIR="$root/live-bitrate/tmp" python3 tests/test-host-live-bitrate.py
TMPDIR="$root/live-bitrate/tmp" python3 tests/test-host-cadence-overlay.py
TMPDIR="$root/live-bitrate/tmp" python3 tests/test-smart-stream.py
TMPDIR="$root/live-bitrate/tmp" python3 tests/test-live-rate-fallback.py
nix develop -c python3 tests/test-core.py
# On the Linux build host, hold its shared heavy-build lock and use CPU/job limits:
taskset -c 0-3 nice -n 15 nix build --no-link --cores 4 --max-jobs 1
```

With multi-user Nix, the daemon's builders do not inherit the CLI's affinity or
nice value. The dated build uses an isolated source export with the following
resource-only hook on **both** the Sunshine and client derivations; the original
flake is restored afterward. Its patch is retained with the build evidence.
This changes scheduling only, without changing dependency pins, compiler flags,
production sources or global daemon configuration. Verify actual compiler
`Cpus_allowed_list` and nice values, not just the CLI wrapper.

```nix
preUnpack = (old.preUnpack or "") + ''
  python3 - <<'DESKPORT_RESOURCE_PY'
import os
pid = os.getppid()
os.sched_setaffinity(pid, {0, 1, 2, 3})
os.setpriority(os.PRIO_PROCESS, pid, 15)
assert os.sched_getaffinity(pid) == {0, 1, 2, 3}
assert os.getpriority(os.PRIO_PROCESS, pid) == 15
DESKPORT_RESOURCE_PY
'';
```

`test-host-live-bitrate.py` compiles the production native transaction extracted
from both patched pins, using their actual NVENC SDK types and an injected API.
It exercises the initial host ceiling (including fixed drivers), live bounds,
VBV accuracy over 200 down/up cycles, unchanged format,
mutation rejection, rollback rejection, failure latching and concurrent API
acknowledgment/timeouts. These are fault-injection tests, not NVENC hardware proof.
`tests/vt-rate-transaction.cpp` injects each independent property-setting failure
and the write-only-limit behavior; no screen capture is involved.

Run the synthetic hardware/independent-decoder checks with the existing verified
host dependency root (no download or dependency upgrade):

```sh
python3 tests/test-vt-live-rate.py --ffmpeg-root /absolute/path/to/pinned/ffmpeg
```

`tests/vt-live-rate.cpp` uses the existing checksum-pinned macOS host FFmpeg and
synthetic 2560x1440 NV12 content at 60 fps. It checks hardware acceleration,
changes 15 -> 3 -> 15 Mbps without reopening the encoder, records output by
original PTS (including dropped frames), and can save elementary streams with
`DP_RATE_OUTPUT=/absolute/build/path/prefix`. An encoder-only FFmpeg archive
reports `decoded=-1`; independently decode the saved streams and count frames
before calling the hardware probe accepted. Streams contain synthetic content
only. See the dated results below for the actual acceptance boundary.

## Official API/source references

- [NVIDIA programming guide: reconfiguration and capability queries](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html)
- [Pinned FFmpeg NVENC reconfiguration](https://github.com/FFmpeg/FFmpeg/blob/fb216b5facde2c97cb0ce2e75fb3228aa5ac21fa/libavcodec/nvenc.c)
- [AMD AMF dynamic properties](https://github.com/GPUOpen-LibrariesAndSDKs/AMF/blob/master/amf/doc/AMF_Video_Encode_API.md)
- [Pinned FFmpeg AMF adapter](https://github.com/FFmpeg/FFmpeg/blob/fb216b5facde2c97cb0ce2e75fb3228aa5ac21fa/libavcodec/amfenc.c)
- [Pinned FFmpeg VAAPI rate initialization](https://github.com/FFmpeg/FFmpeg/blob/fb216b5facde2c97cb0ce2e75fb3228aa5ac21fa/libavcodec/vaapi_encode.c)
- [Apple VTSessionSetProperty](https://developer.apple.com/documentation/videotoolbox/vtsessionsetproperty(_:key:value:)) and the installed SDK's `VTSession.h`: NULL restores a property's default.
- [Pinned macOS FFmpeg limit initialization](https://github.com/FFmpeg/FFmpeg/blob/bf1b838f/libavcodec/videotoolboxenc.c)

## Dated results (2026-10-17)

Local Apple Silicon, hardware-required VideoToolbox, pinned `bf1b838f` host
FFmpeg, synthetic 2560x1440 NV12 60 fps, 540 submitted frames per run:

| Codec | Settled low rate (3 Mbps target) | Recovered rate (15 Mbps target) | Output / dropped | Independent software decode |
| --- | --- | --- | --- | --- |
| H.264 | 2.521 Mbps | 15.008 Mbps | 443 / 97 | All 443 output frames, 2560x1440, no decode error |
| HEVC | 2.986 Mbps | 14.991 Mbps | 444 / 96 | All 444 output frames, 2560x1440, no decode error |

Baseline runs emitted all 540 frames. Both changed-rate streams contained one
initial keyframe and stayed in the same codec session through down/up changes.
Rates use submitted PTS windows, including dropped frames as zero bytes. The
hardware-status readback itself is unsupported on this host; `allow_sw=0` and
`require_sw=0` require hardware through the pinned FFmpeg initialization code.

The native Linux x86_64 `nix build --no-link --cores 4 --max-jobs 1` passed
for production source `098431a4`, including the normal client preBuild fallback
regression. Both derivations confirmed CPU affinity 0-3 and nice 15 through the
resource-only hook above; a host compiler snapshot independently confirmed those
values. The isolated export's original flake was restored after the build.

The NVENC API/rollback and two-pin overlay tests passed, as did VideoToolbox
property-failure injection, smart-stream policy/cadence and locked-shell core
checks. `test-live-rate-fallback.py` compiles the actual viewer pending/result,
readiness and budget expressions, including startup toggles and same-cap encoder
replacement. The changed macOS viewer translation unit also compiled with the
locked devShell's Qt. No real NVENC/AMF/VAAPI encode, physical Windows session, capture/input,
network shaping, packet-loss transport, WAN or deployed-application acceptance
is claimed. Preset target changes in synthetic hardware streams are not actual
network-change evidence.

Windows target compilation passed for NVENC SDK 11/12/13 and the changed
`confighttp.cpp`, `rtsp.cpp`, `stream.cpp` and `video.cpp`, inside the locked
Windows cross shell. It reused only the existing pinned dependency inputs; its
prepared production source, seven objects and logs belong to the isolated test
build root. An attributable compiler sample confirmed CPU affinity 0-3 and nice
15. This is translation-unit compilation, without a Windows link/package or
native driver/session acceptance. The heavy-build lock was released after checks.

Accepted logs, source/helper hashes, Linux store metadata, compiler resource
samples, synthetic streams and independent decoder results are retained under
the immutable build-root release ID
`encoder-live-rate-20261017-final-098431a4`. Canceled attempts, the initial
incomplete Windows test environment and an unattributed process sample are
excluded from acceptance. No GUI/VM/capture resources or deployed services were
used by this test run.

For targeted Windows compilation on the Linux build host, enter
`nix-shell winbuild/scripts/shell.nix`, hold the same heavy build lock and pass
the existing pinned host compile database as a read-only
input. `scripts/check-windows-host-rate.py --compile-commands /absolute/path/to/compile_commands.json`
prepares its own source and compiles the three NVENC SDK variants plus the four
changed host translation units, with all object/log outputs under this checkout's
build root. This does not install, package or prove native Windows runtime behavior.
