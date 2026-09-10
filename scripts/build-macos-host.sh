#!/bin/bash
# Build the pinned DeskPort host with virtual-display input routing.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$repo/build-macos.noindex/sunshine-source"
build_dir="$repo/build-macos.noindex/sunshine-build"
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
sdk=$(xcrun --sdk macosx --show-sdk-path)
pc="$repo/build-macos.noindex/host-pkgconfig"
mkdir -p "$pc"
curl_version=$(sed -n 's/^#define LIBCURL_VERSION "\([^"]*\)"/\1/p' "$sdk/usr/include/curl/curlver.h")
cat > "$pc/libcurl.pc" <<PC
Name: libcurl
Description: macOS SDK libcurl
Version: $curl_version
Libs: -lcurl
Cflags:
PC
export PKG_CONFIG_PATH="$pc:/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/openssl@3/lib/pkgconfig"
export BUILD_VERSION=2026.906.222525 BRANCH=deskport
cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
    -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 -DOpus_ROOT_DIR=/opt/homebrew/opt/opus \
    -DICU_ROOT=/opt/homebrew/opt/icu4c@78 -DSUNSHINE_PUBLISHER_NAME=DeskPort \
    -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/keithxc/deskport/issues
cmake --build "$build_dir" --target sunshine -j6
