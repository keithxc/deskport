#!/usr/bin/env bash
# Build the experimental Seamless lab server for a Linux SSH host on this
# machine (macOS uses its nix-darwin linux-builder) and copy it there.
#
#   bash scripts/deploy-seamless-lab-server.sh <ssh-host> [aarch64-linux|x86_64-linux]
#
# Reuse and cleanup: one --out-link per system under this checkout's build
# root keeps the dependency closure cached locally; the remote side keeps one
# GC root at ~/mygit/build/deskport/seamless-lab/server, so a new deploy
# releases the previous build to the remote Nix garbage collector.
#
# The remote nix-daemon must list the SSH user in trusted-users; otherwise it
# rejects the unsigned locally built paths even with --no-check-sigs.
set -euo pipefail

host=${1:?usage: deploy-seamless-lab-server.sh <ssh-host> [system]}
system=${2:-aarch64-linux}
case $system in
    aarch64-linux | x86_64-linux) ;;
    *) echo "error: unsupported system $system" >&2; exit 2 ;;
esac

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
out_dir="$(bash scripts/build-paths.sh checkout)/seamless-lab/$system"
mkdir -p "$out_dir/logs"
log="$out_dir/logs/deploy-$(date +%Y%m%d-%H%M%S).log"
# Keep the last few deploy logs only.
find "$out_dir/logs" -name 'deploy-*.log' | sort -r | tail -n +6 | xargs rm -f --

nix build --out-link "$out_dir/seamless-lab-server" \
    ".#packages.$system.seamless-lab-server" -L >"$log" 2>&1 || {
    tail -n 30 "$log" >&2
    exit 1
}
path=$(readlink "$out_dir/seamless-lab-server")

# Dependencies already present remotely are skipped; missing ones are
# fetched by the remote host from its own substituters when possible. The
# trusted remote user may waive signatures only when the client asks to.
nix copy --no-check-sigs --substitute-on-destination --to "ssh-ng://$host" "$path" >>"$log" 2>&1 || {
    tail -n 30 "$log" >&2
    exit 1
}

remote_root='mygit/build/deskport/seamless-lab/server'
# shellcheck disable=SC2029 # $path and $remote_root are expanded locally.
ssh -o BatchMode=yes "$host" \
    "mkdir -p \"\$HOME/$(dirname "$remote_root")\" &&
     nix-store --add-root \"\$HOME/$remote_root\" --realise '$path' >/dev/null &&
     '$path/bin/seamless-lab-server' --version"
echo "deployed: $host:~/$remote_root -> $path"
echo "log: $log"
