#!/usr/bin/env bash
# Export committed public source into a new artifact, without clearing outputs.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$repo"

fail() { echo "$1" >&2; exit 1; }
[ "$#" -le 1 ] || fail 'Usage: generate-src.sh [OUTPUT.tar.gz]'

if [ -z "${DESKPORT_RUN_ID:-}" ]; then
    export DESKPORT_RUN_ID="source-$(date -u +%Y%m%dT%H%M%SZ)-$$"
    export DESKPORT_TARGET="${DESKPORT_TARGET:-linux-nix}"
fi
source "$repo/scripts/build-paths.sh"
version=$(cat app/version.txt)
case "$version" in
    ''|*[!A-Za-z0-9_.+-]*) fail 'Version cannot be used as a source archive name.' ;;
esac
prefix="DeskPort-$version"
output=${1:-$DESKPORT_DIST_DIR/$prefix-source.tar.gz}
storage_validate "$DESKPORT_WORK_DIR" "$output" >/dev/null
case "$output" in
    *.tar.gz) ;;
    *) fail 'Source output must end in .tar.gz.' ;;
esac

# Resolve existing parent symlinks and keep both scratch and artifacts outside
# source checkouts. The explicit output is still constrained to the build root.
python3 - "$repo" "$DESKPORT_BUILD_ROOT" "$DESKPORT_WORK_DIR" "$output" <<'PY'
from pathlib import Path
import sys
repo, build, work, output = (Path(arg).resolve() for arg in sys.argv[1:])
for path in (work, output):
    if path == build or build not in path.parents or path == repo or repo in path.parents:
        raise SystemExit('Source scratch and output must be inside the external build root.')
if output.exists() or output.is_symlink():
    raise SystemExit('Source output already exists; choose a new run or filename.')
PY

python3 "$repo/scripts/check-build-privacy.py" --repo "$repo"
[ -z "$(git status --porcelain --untracked-files=all --ignore-submodules=none)" ] ||
    fail 'Source archives require a clean committed main checkout.'
core="$repo/shared/deskport-core"
[ "$(git -C "$core" rev-parse --show-toplevel 2>/dev/null)" = "$core" ] ||
    fail 'Initialize the pinned shared/deskport-core submodule before exporting.'
[ -z "$(git -C "$core" status --porcelain --untracked-files=all)" ] ||
    fail 'Source archives require a clean committed shared core checkout.'
expected_core=$(git ls-tree HEAD shared/deskport-core | awk '$1 == "160000" && $2 == "commit" {print $3}')
[ -n "$expected_core" ] && [ "$expected_core" = "$(git -C "$core" rev-parse HEAD)" ] ||
    fail 'The shared core checkout does not match the committed gitlink.'

mkdir -p "$DESKPORT_WORK_DIR" "$(dirname "$output")"
stage=$(mktemp -d "$DESKPORT_WORK_DIR/.source-stage.XXXXXX")
cleanup() { rm -rf -- "$stage"; }
trap cleanup EXIT
mkdir -p "$stage/$prefix"
git archive HEAD | tar -x -C "$stage/$prefix"
mkdir -p "$stage/$prefix/shared/deskport-core"
git -C "$core" archive HEAD | tar -x -C "$stage/$prefix/shared/deskport-core"
python3 "$repo/scripts/check-build-privacy.py" --source-dir "$stage/$prefix"
tar -czf "$stage/source.tar.gz" -C "$stage" "$prefix"

# O_EXCL prevents an output created concurrently from being overwritten. Remove
# only this newly created output if copying fails; previous artifacts are intact.
python3 - "$stage/source.tar.gz" "$output" <<'PY'
from pathlib import Path
import shutil
import sys
source, output = map(Path, sys.argv[1:])
try:
    destination = output.open('xb')
except FileExistsError:
    raise SystemExit('Source output already exists; refusing to overwrite it.')
try:
    with source.open('rb') as origin, destination:
        shutil.copyfileobj(origin, destination, 1024 * 1024)
except BaseException:
    output.unlink(missing_ok=True)
    raise SystemExit('Source output could not be completed.')
PY
printf 'Source archive: %s\n' "$output"
