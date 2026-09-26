#!/bin/bash
# Build portable Linux packages without modifying the host distribution.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
format=${DESKPORT_LINUX_FORMAT:-all}
case "$format" in all|appimage) ;; *) echo "DESKPORT_LINUX_FORMAT must be all or appimage" >&2; exit 2 ;; esac
work=${DESKPORT_LINUX_WORK:-$repo/build-linux.noindex}
mkdir -p "$work"
work=$(cd "$work" && pwd)
podman run --rm \
    -v "$repo:/src:ro" -v "$work:/work" \
    -e "DESKPORT_JOBS=${DESKPORT_JOBS:-4}" \
    -e "DESKPORT_LINUX_FORMAT=$format" \
    docker.io/library/ubuntu@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 \
    bash /src/scripts/package-linux-container.sh
