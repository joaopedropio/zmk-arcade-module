#!/bin/sh
# Build the host simulator.  Usage: tools/sim/build.sh [output]
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-/tmp/pacman-sim}
cc -O2 -Wall -Wextra -std=c11 -o "$out" \
    "$root/tools/sim/sim.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/panel.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/pacman_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/pacman_render.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/shooter_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/shooter_render.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/bomber_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/bomber_render.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/fighter_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/fighter_render.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/commando_core.c" \
    "$root/boards/shields/pacman_adapter/widgets/game/commando_render.c"
echo "$out"
