#!/bin/sh
# Build the browser preview.  Usage: tools/wasm/build.sh [output.js]
#
# Compiles the firmware's own game core and renderer to WebAssembly, so the
# configurator page can draw exactly what the panel would.  Needs emscripten
# (brew install emscripten); the committed output means nobody opening the
# page needs it.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$root/docs/configurator/preview.js}
game=$root/boards/shields/pacman_adapter/widgets/game

# SINGLE_FILE embeds the wasm in the .js, so the page stays two files to copy
# rather than three, and no MIME type has to be right for it to load.
emcc -O2 -std=c11 -Wall -Wextra \
    -I "$game" \
    -o "$out" \
    "$root/tools/wasm/preview.c" \
    "$game/pacman_core.c" \
    "$game/pacman_render.c" \
    --no-entry \
    -sSINGLE_FILE=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=PacmanPreview \
    -sENVIRONMENT=web,node \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16,HEAPU32,cwrap

echo "$out"
