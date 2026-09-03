/*
 * Arcade dongle - the speaker.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

void arcade_sound_init(void);

/* stop whatever is sounding */
void arcade_sound_quiet(void);

/*
 * A keyboard half arriving or dropping off, from the battery widget - which is
 * where the dongle finds out, since the split connection itself is only
 * announced on the peripheral side.
 */
void arcade_sound_connected(bool connected);

/*
 * The action button's longest press.  Muting silences whatever is sounding;
 * unmuting says so out loud, which is also how you find out the speaker is
 * still wired up.  The flag itself lives in
 * helpers/settings.h and survives a power cycle.
 */
void arcade_sound_set_mute(bool muted);

/* 0 to 100, and the lowest pitch worth sending to the speaker; both live */
void arcade_sound_set_volume(uint8_t volume);
void arcade_sound_set_bass_floor(uint16_t floor_hz);
