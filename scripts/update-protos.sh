#!/usr/bin/env bash
# Refresh proto/meshtastic from upstream meshtastic/protobufs.
# Usage: scripts/update-protos.sh [git-ref]   (default: master)
set -euo pipefail
ref="${1:-master}"
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
git clone -q https://github.com/meshtastic/protobufs "$tmp/protobufs"
git -C "$tmp/protobufs" checkout -q "$ref"
sha="$(git -C "$tmp/protobufs" rev-parse HEAD)"
rm -rf "$root/proto/meshtastic"
mkdir -p "$root/proto/meshtastic"
cp "$tmp/protobufs/meshtastic/"*.proto "$tmp/protobufs/meshtastic/"*.options "$root/proto/meshtastic/"
cp "$tmp/protobufs/LICENSE" "$root/proto/LICENSE"
sed -i "s/commit \`[0-9a-f]*\`/commit \`$sha\`/" "$root/proto/README.md"
echo "Updated proto/ to $sha - rebuild and run ctest."
