#!/bin/bash
# Regenerates the documentation screenshots listed in docs/screenshots/scenes.json
# (see docs/screenshots/README.md). The PCG files the scenes use are private and
# not in this repo -- point KRONOS_SCENE_DATA at the directory holding them.
#
#   KRONOS_SCENE_DATA=~/KRONOS-SOUNDS tools/update-screenshots.sh [scene-id,...]
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
: "${KRONOS_SCENE_DATA:?set KRONOS_SCENE_DATA to the directory holding the scene PCG files}"

cmake --build "$repo/build" --target kronos_editor

args=(--scene "$repo/docs/screenshots/scenes.json" --scene-data "$KRONOS_SCENE_DATA")
if [[ $# -gt 0 ]]; then args+=(--scene-only "$1"); fi

"$repo/build/kronos_editor" "${args[@]}"

git -C "$repo" status --short -- docs/content README-*.png
