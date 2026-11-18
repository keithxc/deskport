#!/bin/bash
# Build the pinned DeskPort host with virtual-display input routing.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export DESKPORT_TARGET=macos
source "$repo/scripts/build-paths.sh"
source_dir=$(deskport_output_path "${DESKPORT_HOST_SOURCE_DIR:-$DESKPORT_MACOS_BUILD_DIR/sunshine-vendored-source}")
storage_prepare
build_root="$DESKPORT_MACOS_BUILD_DIR"
build_dir="$build_root/sunshine-vendored-build"
python3 "$repo/scripts/prepare-host-source.py" "$source_dir"
# Every DeskPort host change is listed, in order, in host/series.
python3 "$repo/scripts/apply-host-patches.py" --platform macos "$source_dir"
if [ "${DESKPORT_HOST_PREPARE_ONLY:-0}" = 1 ]; then
    printf 'Prepared macOS host overlays: %s\n' "$source_dir"
    exit 0
fi
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
# The vendored Sunshine source is materialized without Git metadata, so its
# upstream CMake fallback would otherwise download a mutable "latest" FFmpeg
# archive. Keep the known-compatible macOS archive content-addressed instead.
if [ -z "${DESKPORT_FFMPEG_ROOT:-}" ]; then
    ffmpeg_manifest="$repo/host/vendor/sunshine-macos-ffmpeg.json"
    ffmpeg_urls=$(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m["url"]); print(m.get("mirror", ""))' "$ffmpeg_manifest")
    ffmpeg_sha256=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["sha256"])' "$ffmpeg_manifest")
    ffmpeg_root="$build_root/ffmpeg-$ffmpeg_sha256"
    if [ -n "${DESKPORT_RUN_ID:-}" ]; then
        mkdir -p "$DESKPORT_DEPENDENCY_CACHE_DIR"
        ffmpeg_root="$DESKPORT_DEPENDENCY_CACHE_DIR/ffmpeg-$ffmpeg_sha256"
    fi
    if [ ! -f "$ffmpeg_root/lib/libavcodec.a" ]; then
        ffmpeg_lock="$ffmpeg_root.lock"
        mkdir "$ffmpeg_lock" 2>/dev/null || {
            echo "Pinned FFmpeg cache is locked; inspect $ffmpeg_lock before retrying." >&2
            exit 2
        }
        ffmpeg_stage=$(mktemp -d "$build_root/.ffmpeg.XXXXXX")
        trap 'rm -rf "$ffmpeg_stage"; rmdir "$ffmpeg_lock"' EXIT
        # Try upstream first, then the project mirror; both must match the lock.
        actual_ffmpeg_sha256=
        for ffmpeg_url in $ffmpeg_urls; do
            rm -f "$ffmpeg_stage/ffmpeg.tar.gz"
            /usr/bin/curl --fail --location --retry 3 --output "$ffmpeg_stage/ffmpeg.tar.gz" "$ffmpeg_url" || continue
            actual_ffmpeg_sha256=$(shasum -a 256 "$ffmpeg_stage/ffmpeg.tar.gz" | awk '{print $1}')
            [ "$actual_ffmpeg_sha256" = "$ffmpeg_sha256" ] && break
        done
        if [ "$actual_ffmpeg_sha256" != "$ffmpeg_sha256" ]; then
            echo "Pinned macOS FFmpeg checksum mismatch" >&2
            exit 1
        fi
        tar -xzf "$ffmpeg_stage/ffmpeg.tar.gz" -C "$ffmpeg_stage"
        for library in libavcodec.a libswscale.a libavutil.a libcbs.a; do
            test -f "$ffmpeg_stage/ffmpeg/lib/$library" || {
                echo "Pinned macOS FFmpeg archive is missing $library" >&2
                exit 1
            }
        done
        mv "$ffmpeg_stage/ffmpeg" "$ffmpeg_root"
        rm "$ffmpeg_stage/ffmpeg.tar.gz"
        rmdir "$ffmpeg_stage"
        rmdir "$ffmpeg_lock"
        trap - EXIT
    fi
    export DESKPORT_FFMPEG_ROOT="$ffmpeg_root"
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
cmake --build "$build_dir" --target sunshine -j"${DESKPORT_JOBS:-4}"
