# Bundled components

DeskPort is an independent derivative of Moonlight Qt, initially v6.1.0, under
GPL-3.0-or-later. Source and modifications: https://github.com/keithxc/deskport
Upstream: https://github.com/moonlight-stream/moonlight-qt/tree/v6.1.0
The virtual-display helper is part of the DeskPort source tree.

The macOS package includes the unmodified, separately signed Sunshine application
v2026.906.222525 by LizardByte. Its source, dependency gitlinks and build instructions:
https://github.com/LizardByte/Sunshine/tree/v2026.906.222525
Sunshine retains its own license files, notices and application identity.

Qt is deployed as dynamically linked frameworks from the build environment. Qt
source and licensing: https://www.qt.io/licensing/open-source-lgpl-obligations
SDL, FFmpeg, OpenSSL, Opus and other Moonlight dependencies retain upstream notices
and versions from the pinned Moonlight dependency tree. See README.upstream.md,
LICENSE and the relevant submodule licenses in the source distribution.

This is a local development preview signed locally using the configured identity. It has no
Developer ID distribution signing or notarization. Public release packaging must
include corresponding sources, exact dependency versions and license notices.
