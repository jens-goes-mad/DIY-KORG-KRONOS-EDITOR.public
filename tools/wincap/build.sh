#!/bin/bash
# Builds wincap (see wincap.swift) into the given directory (default: this directory).
#
#   tools/wincap/build.sh            -> tools/wincap/wincap
#   tools/wincap/build.sh ~/bin      -> ~/bin/wincap
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
out="${1:-$here}"
mkdir -p "$out"

args=(-swift-version 5 -O)

# Some Command Line Tools releases ship both module.modulemap and bridging.modulemap
# in usr/include/swift, each defining SwiftBridging, which breaks every Foundation
# import ("redefinition of module 'SwiftBridging'"). Hide the duplicate via a VFS
# overlay instead of deleting a system file.
inc="$(dirname "$(xcrun --find swiftc)")/../include/swift"
if [[ -f "$inc/module.modulemap" && -f "$inc/bridging.modulemap" ]] \
   && grep -q 'module SwiftBridging' "$inc/module.modulemap" "$inc/bridging.modulemap" 2>/dev/null; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    : > "$tmp/empty.modulemap"
    inc="$(cd "$inc" && pwd -P)"
    cat > "$tmp/overlay.yaml" <<EOF
{ "version": 0, "case-sensitive": "false", "roots": [ { "type": "directory", "name": "$inc",
  "contents": [ { "type": "file", "name": "module.modulemap", "external-contents": "$tmp/empty.modulemap" } ] } ] }
EOF
    args+=(-vfsoverlay "$tmp/overlay.yaml")
fi

swiftc "${args[@]}" "$here/wincap.swift" -o "$out/wincap"
echo "built $out/wincap"
