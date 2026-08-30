/*
 * Pac-Man dongle - the speaker.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

void pacman_sound_init(void);

/* stop whatever is sounding */
void pacman_sound_quiet(void);

/*
 * A keyboard half arriving or dropping off, from the battery widget - which is
 * where the dongle finds out, since the split connection itself is only
 * announced on the peripheral side.
 */
void pacman_sound_connected(bool connected);

/*
 * The action button's longest press.  Muting silences whatever is sounding;
 * unmuting says so out loud, which is also how you find out the speaker is
 * still wired up.  The flag itself lives in
 * helpers/settings.h and survives a power cycle.
 */
void pacman_sound_set_mute(bool muted);
