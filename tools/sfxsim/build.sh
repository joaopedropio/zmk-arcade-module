#!/bin/sh
# Build the host renderer for the dongle's sounds.
#   tools/sfxsim/build.sh /tmp/sfxsim && /tmp/sfxsim /tmp/sounds
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-/tmp/sfxsim}
cc -O2 -Wall -Wextra -std=c11 -I "$root/boards/shields/pacman_adapter/widgets/game" \
    -o "$out" "$root/tools/sfxsim/sfxsim.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/pacman_sfx.c"
echo "$out"
