#!/bin/bash
# Build the portable host with the same DeskPort protocol patches as Nix.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
source="$work/cache/sunshine-vendored-source"
python3 "$repo/scripts/prepare-host-source.py" "$source"
python3 "$repo/scripts/patch-host-diagnostics.py" "$source"
python3 "$repo/scripts/patch-host-network.py" "$source"
python3 "$repo/scripts/patch-host-session-takeover.py" "$source" --revert
python3 "$repo/scripts/patch-host-session-settings.py" "$source" --revert
python3 "$repo/scripts/patch-host-smart-stream.py" "$source"
for patch in session-settings session-takeover linux-display; do
    python3 "$repo/scripts/patch-host-$patch.py" "$source"
done
mkdir -p "$source/src/deskport/common"
cp "$repo"/host/common/{smartstream,inputactivity,framecadence,encoderpolicy}.h "$source/src/deskport/common/"
for patch in input-activity sync-cadence linux-cadence pipewire-memory encoder-policy; do
    python3 "$repo/scripts/patch-host-$patch.py" "$source"
done
for patch in display-ownership egl-query-lifetime; do
    python3 "$repo/scripts/patch-host-$patch.py" "$source"
done
python3 "$repo/scripts/patch-host-vulkan-lifetime.py" "$source" "$repo/host/linux/vulkan-driver-lifetime.h"
python3 "$repo/scripts/patch-host-memory-diagnostics.py" "$source" "$repo/host/common/memorydiagnostics.h"
# Ubuntu 24.04's libstdc++ lacks ranges::to; only debug formatting changes.
compatibility_patch="$repo/host/linux/patches/sunshine-gcc13-log.patch"
if git -C "$source" apply --check "$compatibility_patch" 2>/dev/null; then
    git -C "$source" apply "$compatibility_patch"
else
    git -C "$source" apply --reverse --check "$compatibility_patch"
fi
bash "$repo/scripts/build-linux-ffmpeg.sh"
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source" -B "$work/host-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DFFMPEG_PREPARED_BINARIES="$work/cache/ffmpeg-pinned" \
    -DCMAKE_CXX_FLAGS="-I$work/cache/vulkan-headers/include" \
    -DSUNSHINE_ASSETS_DIR=share/sunshine \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DSUNSHINE_ENABLE_CUDA=OFF -DSUNSHINE_BUILD_APPIMAGE=ON
cmake --build "$work/host-build" --target sunshine -j "${DESKPORT_JOBS:-4}"
