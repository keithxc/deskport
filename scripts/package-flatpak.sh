#!/bin/bash
# Package the portable viewer inside the Freedesktop runtime; no host escape.
set -euo pipefail
appdir=${1:?Usage: package-flatpak.sh APPDIR OUTPUT_DIR VERSION}
output=${2:?Output directory required}
version=${3:?Version required}
repo=$(cd "$(dirname "$0")/.." && pwd -P)
source "$repo/scripts/build-storage.sh"
output=$(python3 -B "$repo/scripts/build_storage.py" validate-legacy "$output")
storage_prepare
id=io.github.keithxc.DeskPort
mkdir -p "$output"
output=$(cd "$output" && pwd)
stage="$output/flatpak-stage"
repo="$output/flatpak-repo"
if [ -e "$stage" ]; then
    echo "Flatpak stage already exists; choose a fresh output directory." >&2
    exit 1
fi
mkdir -p "$stage/files/opt/deskport" "$stage/files/bin" \
    "$stage/files/share/applications" "$stage/files/share/icons/hicolor/scalable/apps" \
    "$stage/files/share/metainfo"
cp -a "$appdir/." "$stage/files/opt/deskport/"
rm -rf "$stage/files/opt/deskport/usr/libexec"
cat > "$stage/files/bin/deskport" <<'SH'
#!/bin/sh
exec /app/opt/deskport/AppRun "$@"
SH
chmod +x "$stage/files/bin/deskport"
cp "$appdir/usr/share/applications/$id.desktop" "$stage/files/share/applications/"
sed -i 's/^Icon=deskport$/Icon=io.github.keithxc.DeskPort/' "$stage/files/share/applications/$id.desktop"
cp "$appdir/usr/share/icons/hicolor/scalable/apps/deskport.svg" \
    "$stage/files/share/icons/hicolor/scalable/apps/$id.svg"
for size in 16 32 48 64 128 256 512; do
    icon="$appdir/usr/share/icons/hicolor/${size}x${size}/apps/deskport.png"
    if [ -f "$icon" ]; then
        mkdir -p "$stage/files/share/icons/hicolor/${size}x${size}/apps"
        cp "$icon" "$stage/files/share/icons/hicolor/${size}x${size}/apps/$id.png"
    fi
done
cp "$appdir/usr/share/metainfo/$id.appdata.xml" "$stage/files/share/metainfo/"
cat > "$stage/metadata" <<EOF
[Application]
name=$id
runtime=org.freedesktop.Platform/x86_64/25.08
command=deskport
EOF
flatpak build-finish --command=deskport --share=network --share=ipc \
    --socket=wayland --socket=fallback-x11 --socket=pulseaudio --device=dri \
    --talk-name=org.freedesktop.ScreenSaver "$stage"
flatpak build-export --arch=x86_64 "$repo" "$stage" stable
flatpak build-bundle --arch=x86_64 \
    --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo \
    "$repo" "$output/DeskPort-$version-client-x86_64.flatpak" "$id" stable
sha256sum "$output/DeskPort-$version-client-x86_64.flatpak"
