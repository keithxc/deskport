#!/usr/bin/env bash
# Materialize pinned Windows host sources; fetch only missing verified build inputs.
set -euo pipefail
source "$(dirname "$0")/env.sh"

FULL="$WB/full"
HOST_SOURCE="${DESKPORT_HOST_SOURCE_DIR:-$FULL/sunshine-prepared}"
HOST_PREPARE="$SRC_ROOT/scripts/prepare-host-source.py"

python3 "$HOST_PREPARE" "$HOST_SOURCE"
python3 "$WB/scripts/fetch-host-vendored-deps.py" "$HOST_SOURCE"

apply_patch() {
  local tree="$1" patch="$2"
  # --recount tolerates reviewed patches whose final hunk line count drifted
  # while the vendored source itself remains unchanged.
  if git -C "$tree" apply --recount --check "$patch"; then
    git -C "$tree" apply --recount "$patch"
  elif git -C "$tree" apply --recount --reverse --check "$patch"; then
    : # This invocation already has the overlay.
  else
    echo "Cannot apply Windows host overlay: $patch" >&2
    return 1
  fi
}

# These two patches are Windows-specific. In particular, do not apply the
# Linux display or macOS capture patches to this source tree.
apply_patch "$HOST_SOURCE" "$SRC_ROOT/host/windows/patches/sunshine-cross-build.patch"
apply_patch "$HOST_SOURCE/third-party/libvirtualhid" \
  "$SRC_ROOT/host/windows/patches/libvirtualhid-win32-only.patch"

# The following overlays are platform-neutral parts of the DeskPort host
# protocol and streaming policy. Their scripts select stable source anchors;
# no platform-specific display implementation is pulled into Windows.
python3 "$SRC_ROOT/scripts/patch-host-windows-display.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-windows-desktop-lifetime.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-windows-pointer-lifetime.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-enet-windows-qos.py" "$HOST_SOURCE/third-party/moonlight-common-c/enet/win32.c"
python3 "$SRC_ROOT/scripts/patch-host-diagnostics.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-network.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-smart-stream.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-session-settings.py" "$HOST_SOURCE" --revert
python3 "$SRC_ROOT/scripts/patch-host-session-settings.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-session-takeover.py" "$HOST_SOURCE" --revert
python3 "$SRC_ROOT/scripts/patch-host-session-takeover.py" "$HOST_SOURCE"

mkdir -p "$HOST_SOURCE/src/deskport/common"
cp "$SRC_ROOT/host/common/"{smartstream,inputactivity,framecadence,encoderpolicy}.h \
  "$HOST_SOURCE/src/deskport/common/"
python3 "$SRC_ROOT/scripts/patch-host-encoder-policy.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-input-activity.py" "$HOST_SOURCE"
python3 "$SRC_ROOT/scripts/patch-host-sync-cadence.py" "$HOST_SOURCE"

printf '%s\n' "$HOST_SOURCE"
