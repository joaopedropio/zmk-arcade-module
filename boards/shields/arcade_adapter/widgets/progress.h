/*
 * Arcade dongle - the modal shown while a profile is being applied.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* the buffers it draws through; called once, beside the other widget inits */
void progress_init(void);

/* the box, the word and an empty bar, for the profile about to be loaded */
void progress_open(uint8_t slot);

/*
 * How far along, as a fraction.  Only the slice that is newly full is painted,
 * so calling this for every setting a profile carries costs nothing for the
 * ones that do not move the bar a whole pixel.
 */
void progress_draw(uint16_t done, uint16_t total);
