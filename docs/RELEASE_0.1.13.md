# DeskPort 0.1.13

macOS BGRA/NV12 capture now uses ScreenCaptureKit's native frame status instead
of scanning two pixel buffers with the CPU. This covers the standard 8-bit
H.264/HEVC VideoToolbox path. New complete frames are forwarded immediately;
idle samples do not map, compare or copy pixel data in the change detector.
The existing CVPixelBuffer-backed VideoToolbox encoder path is preserved.

A 100 ms lifecycle heartbeat keeps shutdown and reconfiguration responsive on
static screens. One retained surface is republished at most once a second when
idle so new encoders receive an initial image. Smart streaming retains its
approximately five-fps idle encoding target and congestion recovery policy.
Capture errors and first-frame timeouts terminate safely; cancellation drains
callbacks before releasing their C++ state.

P010/10-bit capture retains AVFoundation because ScreenCaptureKit does not list
P010 as a supported output format. CPU pixel comparison is removed from this
compatibility path too, but native static-frame savings do not apply to it.
System-reported redraws may include identical content; we conservatively forward
complete frames rather than spending CPU/GPU work proving pixel equality.

## Validation

The native bridge is tested with synthetic CoreVideo/CoreMedia samples, without
screen capture or input injection: complete/started/idle status, retained-surface
refresh, lifecycle ticks, cancellation, late callbacks and first-frame timeout.
Streaming-policy regression tests also remain applicable.

Compilation and packaging do not establish live capture permissions, virtual
screen correctness, color fidelity, cursor behavior or power savings. After
updating, compare a static desktop and video at fixed resolution, check the
ScreenCaptureKit startup diagnostic, and exercise reconnect/resize/stop. Measure
host CPU and system activity separately; do not infer watts or bandwidth from FPS.
