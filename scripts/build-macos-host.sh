#!/bin/bash
# Build the pinned DeskPort host with virtual-display input routing.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$repo/build-macos.noindex/sunshine-source"
if [ "${DESKPORT_NIX_DEPS:-0}" = 1 ]; then
    build_root=${DESKPORT_MACOS_BUILD_DIR:-$repo/build-macos.noindex/nix}
else
    build_root=${DESKPORT_MACOS_BUILD_DIR:-$repo/build-macos.noindex}
fi
build_dir="$build_root/sunshine-build"
revision=cb72dffa3233c5815cd5ba88f09f049dd679ba75
mkdir -p "$repo/build-macos.noindex"
if [ ! -d "$source_dir" ]; then
    git clone --depth 1 --branch v2026.906.222525 https://github.com/LizardByte/Sunshine.git "$source_dir"
fi
[ "$(git -C "$source_dir" rev-parse HEAD)" = "$revision" ] || { echo 'Unexpected host source revision' >&2; exit 1; }
git -C "$source_dir" submodule update --init --depth 1 -- third-party/build-deps third-party/libvirtualhid third-party/lizardbyte-common third-party/libdisplaydevice third-party/moonlight-common-c third-party/Simple-Web-Server third-party/TPCircularBuffer
git -C "$source_dir/third-party/moonlight-common-c" submodule update --init --depth 1 -- enet nanors
hid="$source_dir/third-party/libvirtualhid"
[ "$(git -C "$hid" rev-parse HEAD)" = 6fdb8bd4de3b68d96c30e5303ac2ebb333c09746 ]
patch="$repo/host/macos/patches/libvirtualhid-target-display.patch"
if git -C "$hid" apply --check "$patch"; then
    git -C "$hid" apply "$patch"
else
    git -C "$hid" apply --reverse --check "$patch"
fi
# Undo our final overlay before checking the earlier pinned patches on rebuilds.
sck_patch="$repo/host/macos/patches/sunshine-screen-capture-kit.patch"
if git -C "$source_dir" apply --reverse --check "$sck_patch" 2>/dev/null; then
    git -C "$source_dir" apply --reverse "$sck_patch"
fi
# Bound the first-frame wait so a silent virtual display cannot hang /resume.
capture_patch="$repo/host/macos/patches/sunshine-capture-timeout.patch"
if git -C "$source_dir" apply --check "$capture_patch"; then
    git -C "$source_dir" apply "$capture_patch"
else
    git -C "$source_dir" apply --reverse --check "$capture_patch"
fi
# Include frame identities in recovery diagnostics without changing encoded data.
idr_patch="$repo/host/macos/patches/sunshine-idr-diagnostics.patch"
if git -C "$source_dir" apply --check "$idr_patch"; then
    git -C "$source_dir" apply "$idr_patch"
else
    git -C "$source_dir" apply --reverse --check "$idr_patch"
fi
# Use pkg-config's resolved library path, not a Homebrew-only -l search.
link_patch="$repo/host/macos/patches/sunshine-pkgconfig-link.patch"
if git -C "$source_dir" apply --check "$link_patch"; then
    git -C "$source_dir" apply "$link_patch"
else
    git -C "$source_dir" apply --reverse --check "$link_patch"
fi
smart_patch="$repo/host/macos/patches/sunshine-smart-streaming.patch"
if git -C "$source_dir" apply --check "$smart_patch"; then
    git -C "$source_dir" apply "$smart_patch"
else
    git -C "$source_dir" apply --reverse --check "$smart_patch"
fi
mkdir -p "$source_dir/src/deskport/common" "$source_dir/src/deskport/macos"
cp "$repo/host/common/smartstream.h" "$source_dir/src/deskport/common/smartstream.h"
cp "$repo/host/macos/pixelmatch.h" "$source_dir/src/deskport/macos/pixelmatch.h"
git -C "$source_dir" apply --check "$sck_patch"
git -C "$source_dir" apply "$sck_patch"
cp "$repo/host/macos/screen-video.h" "$repo/host/macos/screen-video.m" "$source_dir/src/deskport/macos/"
sdk=$(xcrun --sdk macosx --show-sdk-path)
pc="$build_root/host-pkgconfig"
mkdir -p "$pc"
curl_version=$(sed -n 's/^#define LIBCURL_VERSION "\([^"]*\)"/\1/p' "$sdk/usr/include/curl/curlver.h")
cat > "$pc/libcurl.pc" <<PC
Name: libcurl
Description: macOS SDK libcurl
Version: $curl_version
Libs: -lcurl
Cflags:
PC
if [ "${DESKPORT_NIX_DEPS:-0}" = 1 ]; then
    export PKG_CONFIG_PATH="$pc:${PKG_CONFIG_PATH:-}"
else
    export PKG_CONFIG_PATH="$pc:/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig"
fi
extra=()
if [ -n "${DESKPORT_CMAKE_PREFIX_PATH:-}" ]; then
    extra+=("-DCMAKE_PREFIX_PATH=$DESKPORT_CMAKE_PREFIX_PATH" -DOPUS_USE_STATIC=OFF -DBOOST_USE_STATIC=OFF)
fi
if [ -n "${DESKPORT_FFMPEG_ROOT:-}" ]; then
    extra+=("-DFFMPEG_PREPARED_BINARIES=$DESKPORT_FFMPEG_ROOT")
fi
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DOPENSSL_ROOT_DIR="${DESKPORT_OPENSSL_ROOT:-/opt/homebrew/opt/openssl@3}" \
    -DOpus_ROOT_DIR="${DESKPORT_OPUS_ROOT:-/opt/homebrew/opt/opus}" \
    -DICU_ROOT="${DESKPORT_ICU_ROOT:-/opt/homebrew/opt/icu4c@78}" -DSUNSHINE_PUBLISHER_NAME=DeskPort \
    -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/keithxc/deskport/issues "${extra[@]}"
cmake --build "$build_dir" --target sunshine -j6
