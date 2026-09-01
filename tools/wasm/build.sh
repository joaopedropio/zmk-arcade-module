#!/bin/sh
# Build the browser preview.  Usage: tools/wasm/build.sh [output.js]
#
# Compiles the firmware's own game core, renderer, splash and dashboard to
# WebAssembly, so the configurator page can draw exactly what the panel would.
# The dashboard widgets ask ZMK for state a browser has not got, so they are
# built against tools/uisim/stub, which makes the answers up - the same stubs
# the host preview uses.
#
# Needs emscripten (brew install emscripten); the committed output means
# nobody opening the page needs it.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$root/docs/configurator/preview.js}
widgets=$root/boards/shields/pacman_adapter/widgets
stub=$root/tools/uisim/stub

# the UI reads its defaults from Kconfig, so turn those into a forced include
defs=$(mktemp)
python3 - "$root/Kconfig" > "$defs" <<'PY'
import re, sys
print("/* generated from Kconfig defaults by tools/wasm/build.sh */")
print("#define CONFIG_PACMAN_USE_COMPLETE_CUSTOM_THEME 1")
name = None
for line in open(sys.argv[1]):
    m = re.match(r"^config (PACMAN_\S+)", line)
    if m:
        name = m.group(1)
        continue
    m = re.match(r"^    default (.+)$", line)
    if m and name:
        value = m.group(1).strip()
        if value not in ("y", "n"):
            print(f"#define CONFIG_{name} {value}")
        name = None
PY

# SINGLE_FILE embeds the wasm in the .js, so the page stays two files to copy
# rather than three, and no MIME type has to be right for it to load.
emcc -O2 -std=c11 -Wall \
    -include "$defs" \
    -I "$stub" -I "$widgets" -I "$widgets/game" \
    -o "$out" \
    "$root/tools/wasm/preview.c" \
    "$stub/settings_stub.c" \
    "$widgets/game/panel.c" \
    "$widgets/game/pacman_core.c" \
    "$widgets/game/pacman_render.c" \
    "$widgets/game/shooter_core.c" \
    "$widgets/game/shooter_render.c" \
    "$widgets/game/bomber_core.c" \
    "$widgets/game/bomber_render.c" \
    "$widgets/helpers/display.c" \
    "$widgets/splash.c" \
    "$widgets/logo.c" \
    "$widgets/frames.c" \
    "$widgets/theme.c" \
    "$widgets/battery_status.c" \
    "$widgets/output_status.c" \
    "$widgets/layer_status.c" \
    "$widgets/modifier.c" \
    "$widgets/wpm.c" \
    "$widgets/action_button.c" \
    "$widgets/progress.c" \
    --no-entry \
    -sSINGLE_FILE=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=PacmanPreview \
    -sENVIRONMENT=web,node \
    -sEXPORTED_FUNCTIONS=_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16,HEAPU32,cwrap,ccall,stringToNewUTF8 \
    -sALLOW_MEMORY_GROWTH=1

rm -f "$defs"

# The page and the module are two files that a browser caches separately, so a
# stale one of either is a page calling exports that are not there.  Stamping
# the tag with a hash of what was just built means the pair can only ever be
# fetched together.
page=$(dirname "$out")/index.html
if [ -f "$page" ]; then
    hash=$(shasum -a 256 "$out" | cut -c1-12)
    tmp=$(mktemp)
    sed -E "s|(<script src=\"preview\.js)(\?v=[0-9a-f]+)?(\")|\1?v=$hash\3|" "$page" > "$tmp"
    mv "$tmp" "$page"
    echo "stamped $page with preview.js?v=$hash"
fi

echo "$out"
