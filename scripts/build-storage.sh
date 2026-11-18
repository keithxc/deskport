#!/usr/bin/env bash
# Source before output-producing work; printing/validation makes no directories.
_storage_adapter="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)/build_storage.py"
_storage_exports=$(python3 -B "$_storage_adapter" shell) || { return 2 2>/dev/null || exit 2; }
eval "$_storage_exports"
storage_validate() { python3 -B "$_storage_adapter" validate "$@"; }
storage_prepare() { python3 -B "$_storage_adapter" prepare; }
xcodebuild() { python3 -B "$_storage_adapter" xcode -- "$@"; }
