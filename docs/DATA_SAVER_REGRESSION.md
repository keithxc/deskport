# Data saver encoder regression

Run the macOS encoder against generated desktop resources before asking for a
live screen check. The test uses no screen capture, remote connection or input,
and needs no administrator privileges.

## One command

Use the retained, checksum-verified host FFmpeg prebuilt selected by the macOS
packager. Do not substitute the software decoder's libraries for this input.
The Python environment needs Pillow and NumPy; `ffmpeg` on PATH supplies software
conversion and decoding. Apple command-line tools and VideoToolbox are required.

```sh
python3 tests/test-saver-regression.py \
  --output "$HOME/mygit/build/deskport/<checkout>/<unique-run>/macos-arm64/tests/replay" \
  --ffmpeg-root /absolute/path/to/retained/host-ffmpeg \
  --seed 47
```

The output directory must be new and below `DESKPORT_BUILD_ROOT` (default
`~/mygit/build/deskport`). Use the workspace path resolver for the surrounding
run. Keep the host's free-space reserve. A host lock serializes this entry
point; do not run other native encoder benchmarks concurrently. Each phase has
a timeout and owns its child process group. Logs and failed evidence remain
available for diagnosis.

The default 42-second timeline switches dark code, a popup, a light document,
a dense terminal, charts and the exposed textured desktop at irregular
intervals. It includes small caret changes and a persistent single-line edit.
Chinese text is included when the recorded system CJK font is available.
Generated PNGs, text masks, fonts, prepared NV12 data and source blocks are
identified by SHA-256. No work application or personal content is used.

## What runs

`test-host-encoder-replay.py` applies the production host overlay series, then
extracts complete `encode_run`, `encode_avcodec`, NV12 conversion and native
bitrate-setter blocks. The test compiles those blocks against the pinned native
FFmpeg/VideoToolbox libraries. Its adapters provide the session factory,
synthetic image mailbox and packet sink. It uses a real monotonic clock and
waits for the real native encoder; it is not a faster-than-real-time simulation.

Packet records distinguish native media PTS, submitted logical frame identity
and contiguous wire identity. The source scene is associated before wire
renumbering. A dropped frame cannot silently select the next scene's reference.
The runtime record includes the negotiated bitrate and effective saver cap.
Native property records distinguish requested values, setter status and getter
status: successful `DataRateLimits` writes do not imply supported readback.

The replay covers the asynchronous encode/mailbox/cadence/refinement path and
native codec. It does not cover ScreenCaptureKit, the real transport, SPS/VPS
rewriting, a client decoder, display presentation or human readability. The
smaller direct codec runner in `test-vt-data-saver.py` is a separate benchmark;
its historical sequential-PTS option is not production acceptance.

## Acceptance and retained evidence

The software analyzer compares each decoded luma picture with its source NV12
reference. The generated glyph mask, including a one-pixel edge neighborhood,
prevents a large flat window background from hiding bad text. It also reports
window and exposed-background PSNR. Background texture contributes to native
encoding and traffic; the hard recovery criterion is text quality.

Every verified switch or persistent edit must produce a matching picture with
text PSNR at least 30 dB within 750 ms of the event. Missing outputs fail.
Startup and the initial short caret warmup are explicitly marked unverified;
the steady persistent edit is verified. The threshold is a repeatable synthetic
regression criterion, not a universal human readability measure.

The default budget check is at most 300 decimal MB/hour of estimated IPv4 video
datagrams, including packet padding and 20% FEC, with minimum parity two. The
clean saver minimum-one result is also reported. Packetization is calibrated
against both vendored host revisions. Audio, control, VPN, link-layer overhead,
retransmission and any SPS/VPS size changes are outside this estimate. It does
not establish carrier-billed total traffic.

`evidence/production.report.json` combines quality and budget: both must pass
for exit status zero. The run also retains `.hevc`, packet/quality CSVs, native
property records, runtime settings, source provenance and a reference/first/
held-at-three-seconds popup comparison. Reanalyze an existing run without
running the hardware encoder again:

```sh
python3 tests/test-vt-data-saver.py \
  --timeline /absolute/run/assets/timeline.json \
  --output /absolute/run/input \
  --analyze-prefix /absolute/run/evidence/production \
  --compare-event 4
```

## November 2 regression

At 2040x1228, nominal 60 fps, negotiated encoder 7462 kbps and saver ratio
133 per mille (effective 992 kbps), the matched old implementation failed all
14 verified switches. Some popup text remained below threshold after three
seconds. Low traffic alone therefore did not pass acceptance.

Sparse submissions had used sequential 60 fps media timestamps, compressing
seconds of desktop time into a small codec-time interval. The production fix
uses real submission spacing for VideoToolbox rate allocation, then restores
logical frame identities before capture/size association and wire numbering.
In uncongested saver mode minimum QP 22 avoids spending excess detail on a
preceding background. A newly captured picture receives one attempt with maximum
QP 26 to protect its text. Refinement, idle and explicitly requested keyframes
use maximum 51 so a hard quality bound cannot indefinitely block their output.
If the strict fresh-picture attempt produces no matching output, the next
submission also uses 51, even during continuous new capture. This retry changes
only the maximum QP; it does not change the average, burst or minimum.

The fresh marker comes from the real capture branch, is consumed only after
successful conversion, and survives the admission gate. Decoder recovery is
distinguished from fresh capture. Native
output is matched to the submitted logical identity before deciding whether a
strict attempt was skipped. Congestion retains minimum 32 and maximum 51;
ordinary mode resets to 0 through 51. The average target and three-times
one-second burst remain. An arbitrarily low manual budget cannot guarantee the
normal text-quality criterion.

The sharp first attempt is enabled only when the effective native budget is at
least 300 kbps per million pixels. This conservative replay-based eligibility
heuristic leaves lower budgets at minimum 22 / maximum 51, avoiding a hard
quality bound that the native encoder cannot afford. It does not guarantee
quality for every source image. It does not change workspace sizing or bitrate.
The native `ExpectedFrameRate` hint follows the 8 fps saver ceiling and restores
the negotiated codec frame rate when saver is off. Codec time base, packet
identity and wire frame numbering remain separate from this hint.

| Replay | Verified events | Maximum text recovery | Estimated video IP MB/h |
| --- | ---: | ---: | ---: |
| Matched old implementation | 0/14 passed | Failed | 46.8 |
| Fixed implementation, identical first resources | 14/14 passed | 19.3 ms | 225.6 |
| Fixed implementation, seed 47 with Chinese and text edit | 15/15 passed | 19.8 ms | 227.4 |

A 499 kbps native-budget negative control delivered all 15 changed pictures
within 20 ms, but only eight met the text criterion and the overall test failed.
Low traffic or timely blurry pictures cannot count as a successful quality run.
Recovery requires sustained quality through the end of each event, so a clear
first frame followed by blurry refinement cannot pass either.

These are local native encoder results. A separately built, manually activated
candidate still needs live window-switch and total-traffic acceptance.

Portable checks:

```sh
python3 tests/test-vt-clock.py
python3 tests/test-picture-quality.py
python3 tests/test-saver-wire.py
python3 tests/test-saver-regression-control.py
python3 tests/test-saver-analysis.py
```

The last check needs NumPy; its real generated-image negative control also uses
Pillow, a system monospace font and software FFmpeg. It proves that a stale
single-line edit fails the text mask even if a large flat ROI averages well.
Missing/late output and conflicting budget/provenance are also rejected.
