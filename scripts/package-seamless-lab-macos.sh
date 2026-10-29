#!/usr/bin/env bash
# Build a self-contained, ad-hoc signed SeamlessLab.app debug client.
# Run inside the project devShell:  nix develop -c bash scripts/package-seamless-lab-macos.sh
# This is a local debugging package, not a DeskPort release artifact.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
checkout_root=$(bash scripts/build-paths.sh checkout)
run_id=${SEAMLESS_LAB_RUN_ID:-dev}
work="$checkout_root/seamless-lab/$run_id/macos-arm64/work"
dist="$checkout_root/seamless-lab/$run_id/macos-arm64/dist"
qt_bin=${DESKPORT_QT_BIN:?run inside nix develop}
mkdir -p "$work" "$dist"

(
    cd "$work"
    "$qt_bin/qmake" "$repo/tools/seamless-lab/seamless-lab.pro" CONFIG+=release \
        CONFIG-=debug_and_release CONFIG+=sdk_no_version_check \
        QMAKE_APPLE_DEVICE_ARCHS=arm64 \
        QMAKE_CC=/usr/bin/clang QMAKE_CXX=/usr/bin/clang++ \
        "QMAKE_XCODE_DEVELOPER_PATH=$DEVELOPER_DIR"
    make -j"${DESKPORT_JOBS:-8}"
)

stage=$(mktemp -d "$dist/.stage.XXXXXX")
trap 'chmod -R u+w "$stage" 2>/dev/null || true; rm -rf "$stage"' EXIT
app="$stage/SeamlessLab.app"
ditto "$work/SeamlessLab.app" "$app"
chmod -R u+w "$app"

# Only the Cocoa platform plugin is needed; there is no QML or networking.
IFS=: read -r -a plugin_paths <<< "${DESKPORT_QT_PLUGIN_PATH:-}"
for plugins in "${plugin_paths[@]}"; do
    for category in platforms styles; do
        if [ -d "$plugins/$category" ]; then
            mkdir -p "$app/Contents/PlugIns/$category"
            ditto "$plugins/$category" "$app/Contents/PlugIns/$category"
        fi
    done
done
chmod -R u+w "$app/Contents/PlugIns"
mkdir -p "$app/Contents/Resources"
printf '[Paths]\nPlugins=PlugIns\n' > "$app/Contents/Resources/qt.conf"
python3 scripts/fix-macos-dependencies.py "$app"

# Reject any remaining reference to the build machine's Nix store.
if find "$app" -type f -perm -u+x -o -name '*.dylib' -type f | while read -r binary; do
    file "$binary" | grep -q Mach-O || continue
    otool -L "$binary" | tail -n +2 | grep -q '/nix/store' && echo "$binary"
done | grep -q .; then
    echo "error: bundle still references /nix/store" >&2
    exit 1
fi

codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

stamp=$(date +%Y%m%d-%H%M%S)
revision=$(git rev-parse --short HEAD)
zip="$dist/SeamlessLab-$stamp-$revision-macos-arm64.zip"
ditto -c -k --keepParent "$app" "$zip"
rm -rf "$dist/SeamlessLab.app"
mv "$app" "$dist/SeamlessLab.app"
shasum -a 256 "$zip" | tee "$zip.sha256"
echo "app: $dist/SeamlessLab.app"
