# DeskPort 0.6.3

Desktop release for macOS Apple Silicon, Linux x86_64 and Windows x64.

## Changes

- Bound reconnect resource lifetimes in Linux EGL, PipeWire and Vulkan paths and Windows desktop, pointer-device and network QoS handling.
- Improve connection feedback, binding and session admission, with bounded diagnostics and localized memory indicators.
- Register portable KDE sharing permissions for changing AppImage mount paths.
- Start sharing for a fresh Windows profile and select launch after interactive setup, while preserving saved choices.
- Refresh Windows version metadata and validate packaged executable versions.

## Downloads and scope

macOS arm64 requires macOS 26 or newer. Linux portable/native packages require glibc 2.39 or newer; DEB, RPM, Arch, AppImage and portable archives share the private runtime. Nix users can pin the release. Flatpak is client only and requires Freedesktop Platform 25.08. Windows x64 Setup contains the host and signed virtual-display driver for offline installation.

Use the release checksums and verification report for exact artifact provenance. Package checks do not establish every GPU, live-input, secure-desktop or long-session behavior. Microsoft antivirus determinations apply only to the submitted sample; protected scans of new packages are recorded separately. Mobile clients are distributed separately.
