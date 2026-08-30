/*
 * Pac-Man dongle - the speaker.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "game/pacman_core.h"

void pacman_sound_init(void);

/* called once a frame with the game as it now stands: plays what just
 * happened, and keeps the siren going while the ghosts are blue */
void pacman_sound_step(const pm_game *game);

/* silence, for when the game stops or the dashboard comes up */
void pacman_sound_quiet(void);
