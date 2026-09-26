#!/bin/bash
# Run inside the pinned Ubuntu build container, with sources and work mounted.
set -euo pipefail
repo=${DESKPORT_SOURCE:-/src}
work=${DESKPORT_WORK:-/work}
format=${DESKPORT_LINUX_FORMAT:-all}
case "$format" in all|appimage) ;; *) echo "DESKPORT_LINUX_FORMAT must be all or appimage" >&2; exit 2 ;; esac
version=$(cat "$repo/app/version.txt")
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    build-essential git ca-certificates curl file patchelf squashfs-tools zstd python3 \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-tools-dev-tools qt6-wayland \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    libssl-dev libsdl2-dev libsdl2-ttf-dev libopus-dev libavcodec-dev libavutil-dev \
    libswscale-dev libva-dev libvdpau-dev libdrm-dev libegl1-mesa-dev libgl1-mesa-dev \
    libwayland-dev libx11-dev libxkbcommon-dev libxcb-cursor0 libfuse2t64 \
    libpipewire-0.3-0t64 libpipewire-0.3-dev desktop-file-utils \
    cmake ninja-build pkg-config libcap-dev libcurl4-openssl-dev libevdev-dev \
    libgbm-dev libminiupnpc-dev libnuma-dev libpulse-dev libsystemd-dev libudev-dev \
    libxcb-shm0-dev libxcb-xfixes0-dev libxfixes-dev libxrandr-dev libxtst-dev \
    libvulkan-dev glslang-tools nodejs npm python3-jinja2
mkdir -p "$work/build" "$work/cache" "$work/output"
# qmake compiles embedded translations into the source tree. Keep the caller's
# checkout read-only and use a disposable writable source snapshot instead.
input_repo=$repo
repo=$(mktemp -d "$work/source.XXXXXX")
tar -C "$input_repo" --exclude-vcs --exclude='./winbuild' \
    --exclude='./build*' --exclude='./dist*' --exclude='./result*' \
    --exclude='*.noindex' -cf - . | tar -xf - -C "$repo"
chmod -R u+w "$repo"
export DESKPORT_SOURCE="$repo"
python3 - "$repo/scripts/linux-tools.json" "$work/cache" <<'PY'
import hashlib,json,pathlib,subprocess,sys
for name,item in json.load(open(sys.argv[1])).items():
    path=pathlib.Path(sys.argv[2])/name
    if not path.exists():
        subprocess.run(['curl','-fL','--retry','3','-o',str(path),item['url']],check=True)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit(f'Checksum mismatch: {name}; review upstream tool changes before updating lock')
    path.chmod(0o755)
PY
bash "$repo/scripts/build-linux-host.sh"
cd "$work/build"
qmake6 -r "$repo/moonlight-qt.pro" CONFIG+=release CONFIG+=disable-cuda \
    CONFIG+=disable-libplacebo PREFIX=/usr
make -j"${DESKPORT_JOBS:-4}"
appdir="$work/DeskPort.AppDir"
rm -rf "$appdir"
mkdir -p "$appdir"
make INSTALL_ROOT="$appdir" install
for tool in linuxdeploy linuxdeploy-plugin-qt appimagetool; do
    if [ ! -d "$work/cache/$tool.dir" ]; then
        python3 "$repo/scripts/extract-appimage.py" "$work/cache/$tool" "$work/cache/$tool.dir"
    fi
done
mkdir -p "$work/cache/bin"
ln -sf "$work/cache/linuxdeploy-plugin-qt.dir/AppRun" "$work/cache/bin/linuxdeploy-plugin-qt"
export QMAKE=/usr/bin/qmake6
export QML_SOURCES_PATHS="$repo/app/gui"
export EXTRA_QT_MODULES='waylandcompositor;svg'
export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so;libqminimal.so'
export PATH="$work/cache/bin:$PATH"
"$work/cache/linuxdeploy.dir/AppRun" --appdir "$appdir" --plugin qt \
    --desktop-file "$appdir/usr/share/applications/io.github.keithxc.DeskPort.desktop" \
    --icon-file "$repo/app/res/deskport.svg"
# Keep the host's dependency tree separate from the viewer's Qt/media libraries.
mkdir -p "$appdir/usr/libexec"
python3 "$repo/scripts/extract-appimage.py" "$work/cache/sunshine.AppImage" "$appdir/usr/libexec/sunshine"
# Keep upstream assets, but replace the executable with our patched host.
install -m755 "$work/host-build/sunshine" "$appdir/usr/libexec/sunshine/usr/bin/sunshine"
"$work/cache/linuxdeploy.dir/AppRun" --appdir "$appdir/usr/libexec/sunshine" \
    --executable "$appdir/usr/libexec/sunshine/usr/bin/sunshine"
cat > "$appdir/usr/libexec/deskport-host" <<'SH'
#!/bin/sh
root=$(CDPATH= cd -- "$(dirname -- "$0")/sunshine" && pwd)
unset APPIMAGE APPDIR LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH
export APPDIR="$root"
export LD_LIBRARY_PATH="$root/usr/lib"
export QT_PLUGIN_PATH="$root/usr/plugins"
unset CONFIGURATION_DIRECTORY
export SUNSHINE_MIGRATE_CONFIG=0
case "${1:-}" in
    /*.conf) export XDG_CONFIG_HOME="$(dirname -- "$1")/runtime" ;;
    *) export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}/DeskPort/host-runtime" ;;
esac
mkdir -p "$XDG_CONFIG_HOME"
cd "$root"
# Upstream AppRun creates ~/.config/sunshine and exposes --install service changes.
# Call its executable directly, with the AppDir library/assets environment only.
exec "$root/usr/bin/sunshine" "$@"
SH
chmod +x "$appdir/usr/libexec/deskport-host"
mkdir -p "$appdir/usr/share/doc/deskport"
cp "$repo/LICENSE" "$repo/docs/BUNDLED_COMPONENTS.md" "$appdir/usr/share/doc/deskport/"
python3 "$repo/scripts/check-linux-package.py" "$appdir" "$version"
ARCH=x86_64 "$work/cache/appimagetool.dir/AppRun" --runtime-file "$work/cache/appimage-runtime" "$appdir" "$work/output/DeskPort-$version-x86_64.AppImage"
if [ "$format" = all ]; then
    python3 "$repo/scripts/package-linux-native.py" "$appdir" "$work" "$version"
fi
dpkg-query -W > "$work/output/build-packages.txt"
cd "$work/output"
if [ "$format" = appimage ]; then
    sha256sum "DeskPort-$version-x86_64.AppImage" > SHA256SUMS-linux.txt
else
    sha256sum *.AppImage *.deb *.rpm *.pkg.tar.zst > SHA256SUMS-linux.txt
fi
