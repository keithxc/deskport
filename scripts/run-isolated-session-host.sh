#!/usr/bin/env bash
# Explicit opt-in acceptance harness. Never install or launch the regular instance.
set -euo pipefail
if [ "$#" -ne 1 ]; then
    echo 'Usage: run-isolated-session-host.sh /absolute/path/to/DeskPort.app' >&2
    exit 2
fi
app=$1
[[ "$app" = /* && -x "$app/Contents/MacOS/DeskPort" ]] || { echo 'Expected an absolute app bundle path' >&2; exit 2; }
source "$(dirname "$0")/build-storage.sh"
storage_prepare
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/deskport-session-test.XXXXXX")
chmod 700 "$test_dir"
printf 'DeskPort isolated session test\n' > "$test_dir/SESSION_TEST_ONLY"
touch "$test_dir/portable.dat"
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj '/CN=DeskPort temporary test' \
    -keyout "$test_dir/test-key.pem" -out "$test_dir/test-cert.pem" >/dev/null 2>&1
chmod 600 "$test_dir/test-key.pem"
printf 'Temporary state and logs: %s\nClose the test window to stop only this temporary host.\n' "$test_dir"
cd "$test_dir"
exec "$app/Contents/MacOS/DeskPort" --host-session-test
