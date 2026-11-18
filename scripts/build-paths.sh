#!/bin/bash
# Resolve checkout-specific build paths without creating files or directories.
# Source this file from Bash, or pass a path name to print it for other callers.
source "$(dirname "${BASH_SOURCE[0]}")/build-storage.sh" || { return 2 2>/dev/null || exit 2; }
deskport_checkout=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
deskport_checkout_name=$(printf '%s' "${deskport_checkout##*/}" | LC_ALL=C tr -c '[:alnum:]_.-' '-')
deskport_checkout_hash=$(printf '%s' "$deskport_checkout" | cksum | awk '{print $1}')
# Resolve existing symlink ancestors and lexical components without requiring
# the output directory to exist. In particular, ../ and a symlink cannot hide
# a generated tree inside the source checkout or the Nix store.
deskport_output_path() {
    local prefix= part input=$1
    local -a parts
    case "$input" in *$'\n'*|*$'\r'*) echo 'Build paths must not contain line breaks.' >&2; return 2 ;; esac
    case "$input" in /*) ;; *) input="$deskport_checkout/$input" ;; esac
    IFS=/ read -r -a parts <<< "$input"
    for part in "${parts[@]}"; do
        case "$part" in
            ''|.) continue ;;
            ..) prefix=${prefix%/*} ;;
            *) prefix="$prefix/$part" ;;
        esac
        if [ -d "${prefix:-/}" ]; then
            prefix=$(cd "${prefix:-/}" && pwd -P)
            [ "$prefix" != / ] || prefix=
        elif [ -e "$prefix" ] || [ -L "$prefix" ]; then
            echo "Build path has a non-directory or unresolved ancestor: $prefix" >&2
            return 2
        fi
    done
    prefix=${prefix:-/}
    case "$prefix" in
        "$deskport_checkout"|"$deskport_checkout"/*|/nix/store|/nix/store/*)
            echo 'Build outputs must be outside the source checkout and /nix/store.' >&2
            return 2 ;;
    esac
    storage_validate "$prefix"
}
DESKPORT_BUILD_ROOT=${DESKPORT_BUILD_ROOT:-$MYGIT_ROOT_DIR}
DESKPORT_BUILD_ROOT=$(deskport_output_path "$DESKPORT_BUILD_ROOT") || { return 2 2>/dev/null || exit 2; }
DESKPORT_CHECKOUT_BUILD_ROOT="$DESKPORT_BUILD_ROOT/$deskport_checkout_name-$deskport_checkout_hash"
if [ "${DESKPORT_RUN_ID+x}" = x ]; then
    case "$DESKPORT_RUN_ID" in
        ''|.|..|*[!A-Za-z0-9_.-]*)
            echo 'DESKPORT_RUN_ID must be one non-empty path component (letters, digits, _, . or -).' >&2
            return 2 2>/dev/null || exit 2 ;;
    esac
    case "${DESKPORT_TARGET:-}" in
        macos|linux-nix|linux-portable|windows) ;;
        *) echo 'A managed run requires DESKPORT_TARGET=macos|linux-nix|linux-portable|windows.' >&2
           return 2 2>/dev/null || exit 2 ;;
    esac
    DESKPORT_RUN_ROOT="$DESKPORT_CHECKOUT_BUILD_ROOT/runs.noindex/$DESKPORT_RUN_ID"
    DESKPORT_TARGET_ROOT="$DESKPORT_RUN_ROOT/$DESKPORT_TARGET"
    DESKPORT_WORK_DIR="$DESKPORT_TARGET_ROOT/work"
    DESKPORT_DIST_DIR="$DESKPORT_TARGET_ROOT/dist"
    DESKPORT_LOG_DIR="$DESKPORT_TARGET_ROOT/logs"
    DESKPORT_TEST_DIR="$DESKPORT_TARGET_ROOT/tests"
    export DESKPORT_RUN_ROOT DESKPORT_TARGET_ROOT DESKPORT_WORK_DIR
    export DESKPORT_DIST_DIR DESKPORT_TEST_DIR
fi
if [ "${DESKPORT_NIX_DEPS:-0}" = 1 ]; then
    deskport_macos_provider=nix
else
    deskport_macos_provider=homebrew
fi
deskport_macos_root="$DESKPORT_CHECKOUT_BUILD_ROOT/macos-arm64.noindex/$deskport_macos_provider"
if [ -n "${DESKPORT_RUN_ID:-}" ]; then
    # Managed runs have one canonical layout. Legacy per-path overrides remain
    # supported only for callers which do not opt into a managed run.
    DESKPORT_MACOS_BUILD_DIR="$DESKPORT_RUN_ROOT/macos/work"
    DESKPORT_MACOS_DIST_DIR="$DESKPORT_RUN_ROOT/macos/dist"
    DESKPORT_LINUX_WORK="$DESKPORT_RUN_ROOT/linux-portable/work"
else
    DESKPORT_MACOS_BUILD_DIR=${DESKPORT_MACOS_BUILD_DIR:-$deskport_macos_root/build}
    DESKPORT_MACOS_DIST_DIR=${DESKPORT_MACOS_DIST_DIR:-$deskport_macos_root/dist}
    DESKPORT_LINUX_WORK=${DESKPORT_LINUX_WORK:-$DESKPORT_CHECKOUT_BUILD_ROOT/linux-x86_64.noindex/portable}
    DESKPORT_LOG_DIR="$DESKPORT_CHECKOUT_BUILD_ROOT/logs"
fi
DESKPORT_MACOS_BUILD_DIR=$(deskport_output_path "$DESKPORT_MACOS_BUILD_DIR") || { return 2 2>/dev/null || exit 2; }
DESKPORT_MACOS_DIST_DIR=$(deskport_output_path "$DESKPORT_MACOS_DIST_DIR") || { return 2 2>/dev/null || exit 2; }
DESKPORT_LINUX_WORK=$(deskport_output_path "$DESKPORT_LINUX_WORK") || { return 2 2>/dev/null || exit 2; }
DESKPORT_DEPENDENCY_CACHE_DIR=${DESKPORT_DEPENDENCY_CACHE_DIR:-$DESKPORT_CHECKOUT_BUILD_ROOT/dependencies.noindex/${DESKPORT_TARGET:-macos}}
DESKPORT_DEPENDENCY_CACHE_DIR=$(deskport_output_path "$DESKPORT_DEPENDENCY_CACHE_DIR") || { return 2 2>/dev/null || exit 2; }
DESKPORT_RELEASE_ROOT="$DESKPORT_CHECKOUT_BUILD_ROOT/releases.noindex"
export DESKPORT_BUILD_ROOT DESKPORT_CHECKOUT_BUILD_ROOT
export DESKPORT_MACOS_BUILD_DIR DESKPORT_MACOS_DIST_DIR
export DESKPORT_LINUX_WORK DESKPORT_RELEASE_ROOT DESKPORT_LOG_DIR DESKPORT_DEPENDENCY_CACHE_DIR

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    case "${1:-checkout}" in
        root) printf '%s\n' "$DESKPORT_BUILD_ROOT" ;;
        checkout) printf '%s\n' "$DESKPORT_CHECKOUT_BUILD_ROOT" ;;
        macos-build) printf '%s\n' "$DESKPORT_MACOS_BUILD_DIR" ;;
        macos-dist) printf '%s\n' "$DESKPORT_MACOS_DIST_DIR" ;;
        macos-host-source) printf '%s\n' "${DESKPORT_HOST_SOURCE_DIR:-$DESKPORT_MACOS_BUILD_DIR/sunshine-vendored-source}" ;;
        linux-work) printf '%s\n' "$DESKPORT_LINUX_WORK" ;;
        run) printf '%s\n' "${DESKPORT_RUN_ROOT:-}" ;;
        target) printf '%s\n' "${DESKPORT_TARGET_ROOT:-}" ;;
        work) printf '%s\n' "${DESKPORT_WORK_DIR:-}" ;;
        dist) printf '%s\n' "${DESKPORT_DIST_DIR:-}" ;;
        tests) printf '%s\n' "${DESKPORT_TEST_DIR:-}" ;;
        dependency-cache) printf '%s\n' "$DESKPORT_DEPENDENCY_CACHE_DIR" ;;
        releases) printf '%s\n' "$DESKPORT_RELEASE_ROOT" ;;
        logs) printf '%s\n' "$DESKPORT_LOG_DIR" ;;
        *) echo 'Usage: build-paths.sh [root|checkout|run|target|work|dist|tests|dependency-cache|macos-build|macos-dist|macos-host-source|linux-work|releases|logs]' >&2; exit 2 ;;
    esac
fi
