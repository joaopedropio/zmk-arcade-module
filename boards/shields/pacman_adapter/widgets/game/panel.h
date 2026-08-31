/*
 * Pac-Man dongle - what the two games agree on about the panel.
 *
 * The maze and the shooter are separate games with separate cores, but they
 * share one square screen and one way of reaching it: the size of that screen,
 * the buffer a rectangle is staged in on its way out, and the call that pushes
 * it.  Those three live here rather than in either game's own header, so
 * neither game has to include the other's to know how big the panel is.
 *
 * Portable C, like everything else under game/.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* the dongle's square panel */
#define PM_PANEL 240

/*
 * One staging band, shared.  A renderer paints a rectangle into this and hands
 * it to pm_blit(); only one game is ever running, so a buffer each would be
 * ten kilobytes of a dongle's RAM spent on the game nobody is watching.
 *
 * The size is the larger of what the two renderers ask for, and each of them
 * asserts its own maximum against it rather than trusting this number - so
 * widening a sprite fails the build here instead of running off the end.
 */
#define PM_BAND_PX 5280
extern uint8_t pm_band[PM_BAND_PX * 2];

/* rgb888 down to the panel's 5-6-5 */
uint16_t pm_rgb565(uint32_t rgb888);

/* implemented by the platform: push w*h RGB565 (big endian) pixels at x,y */
extern void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels);
