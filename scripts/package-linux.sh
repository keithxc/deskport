#!/bin/bash
# Build portable Linux packages without modifying the host distribution.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export DESKPORT_TARGET=linux-portable
source "$repo/scripts/build-paths.sh"
format=${DESKPORT_LINUX_FORMAT:-all}
case "$format" in all|appimage) ;; *) echo "DESKPORT_LINUX_FORMAT must be all or appimage" >&2; exit 2 ;; esac
# Keep the array nonempty: Bash 3.2 treats an empty array as unset under -u.
resources=(--rm)
if [ -n "${DESKPORT_LINUX_CPUSET:-}" ]; then
    [[ "$DESKPORT_LINUX_CPUSET" =~ ^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$ ]] || {
        echo "DESKPORT_LINUX_CPUSET must be a CPU list such as 0-3" >&2; exit 2;
    }
    resources+=(--cpuset-cpus "$DESKPORT_LINUX_CPUSET")
fi
if [ -n "${DESKPORT_LINUX_CPU_LIMIT:-}" ]; then
    [[ "$DESKPORT_LINUX_CPU_LIMIT" =~ ^[0-9]+([.][0-9]+)?$ ]] &&
        [[ ! "$DESKPORT_LINUX_CPU_LIMIT" =~ ^0+([.]0+)?$ ]] || {
        echo "DESKPORT_LINUX_CPU_LIMIT must be a positive number" >&2; exit 2;
    }
    resources+=(--cpus "$DESKPORT_LINUX_CPU_LIMIT")
fi
work=$DESKPORT_LINUX_WORK
mkdir -p "$work"
work=$(cd "$work" && pwd)
mounts=(-v "$repo:/src:ro" -v "$work:/work")
if [ -n "${DESKPORT_RUN_ID:-}" ]; then
    dist=$(deskport_output_path "$DESKPORT_DIST_DIR")
    mkdir -p "$DESKPORT_DEPENDENCY_CACHE_DIR" "$dist"
    cache=$(cd "$DESKPORT_DEPENDENCY_CACHE_DIR" && pwd -P)
    # Preserve the container's established /work/cache and /work/output API.
    # Rootless Podman needs plain, empty destinations for these nested mounts.
    # Refuse legacy data and links instead of moving or hiding their contents.
    for entry in cache output; do
        if [ -L "$work/$entry" ]; then
            echo "Refusing managed-run $entry symlink: $work/$entry" >&2; exit 2
        elif [ -e "$work/$entry" ]; then
            [ -d "$work/$entry" ] || {
                echo "Refusing existing managed-run $entry: $work/$entry" >&2; exit 2;
            }
            contents=$(find "$work/$entry" -mindepth 1 -maxdepth 1 -print -quit)
            [ -z "$contents" ] || {
                echo "Refusing nonempty managed-run $entry: $work/$entry" >&2; exit 2;
            }
        else
            mkdir "$work/$entry"
        fi
    done
    # Dependency helpers update extracted sources and ABI-specific prefixes.
    # Serialize this cache without guessing whether an existing lock is stale.
    cache_lock="$cache/.deskport-build.lock"
    mkdir "$cache_lock" 2>/dev/null || {
        echo "Dependency cache is locked; inspect $cache_lock before retrying." >&2; exit 2;
    }
    printf '%s\n' "$$" > "$cache_lock/pid"
    trap 'rm -f "$cache_lock/pid"; rmdir "$cache_lock"' EXIT
    mounts+=(-v "$cache:/work/cache" -v "$dist:/work/output")
fi
podman run \
    "${resources[@]}" \
    "${mounts[@]}" \
    -e "DESKPORT_JOBS=${DESKPORT_JOBS:-4}" \
    -e "DESKPORT_LINUX_FORMAT=$format" \
    docker.io/library/ubuntu@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 \
    bash /src/scripts/package-linux-container.sh
