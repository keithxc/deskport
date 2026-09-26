#!/bin/bash
# Apply the same narrowly scoped Vulkan backports as the locked Nix package.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
cache="$work/cache"
prebuilt="$cache/ffmpeg-pinned"
source="$cache/ffmpeg-vulkan-source"
headers="$cache/vulkan-headers"
# Archive checksums are verified by package-linux-container.sh before this step.
for directory in "$prebuilt" "$source" "$headers"; do
    mkdir -p "$directory"
done
tar -xf "$cache/ffmpeg-pinned.tar.gz" --strip-components=1 -C "$prebuilt"
tar -xf "$cache/ffmpeg-source.tar.gz" --strip-components=1 -C "$source"
tar -xf "$cache/vulkan-headers.tar.gz" --strip-components=1 -C "$headers"
grep -Fx '#define FFMPEG_VERSION "fb216b5"' "$prebuilt/include/libavutil/ffversion.h"
cd "$source"
for header in libavcodec/avcodec.h libavcodec/codec.h libavutil/hwcontext_vulkan.h; do
    cmp "$header" "$prebuilt/include/$header"
done
for patch in cbs queued-views feedback; do
    patch -p1 < "$repo/host/linux/ffmpeg-vulkan-$patch.patch"
done
cp "$prebuilt/include/config.h" config.h
cp "$prebuilt/include/libavutil/avconfig.h" libavutil/avconfig.h
for unit in vulkan_encode vulkan_encode_h264 vulkan_encode_h265 vulkan_encode_av1; do
    cc -O2 -fPIC -I. -I"$prebuilt/include" -I"$headers/include" \
        -c "libavcodec/$unit.c" -o "$unit.o"
    test "$(ar t "$prebuilt/lib/libavcodec.a" | grep -cx "$unit.o")" = 1
    ar r "$prebuilt/lib/libavcodec.a" "$unit.o"
done
ranlib "$prebuilt/lib/libavcodec.a"
