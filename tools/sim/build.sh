#!/bin/sh
# Build the host simulator.  Usage: tools/sim/build.sh [output]
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-/tmp/pacman-sim}
cc -O2 -Wall -Wextra -std=c11 -o "$out" \
    "$root/tools/sim/sim.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/pacman_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/pacman_render.c"
echo "$out"
