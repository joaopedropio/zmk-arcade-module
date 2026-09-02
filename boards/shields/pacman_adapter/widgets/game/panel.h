/*
 * Pac-Man dongle - what the games agree on about the panel.
 *
 * The maze, the shooter, the brick field, the ring, the ridge, the crossing,
 * the girders and the well are separate games with separate cores, but they
 * share one square screen and one way of reaching it: the size of that screen,
 * the buffer a rectangle is staged in on its way out, and the call that pushes
 * it.  Those live here rather than in any one game's header, so no game has to
 * include another's to know how big the panel is.
 *
 * Seven of the eight stamp their contents into a rectangle instead of asking
 * each pixel what is on it, and all seven have a readout to write, so the
 * rectangle being painted and the letters that go in it are here as well.  The
 * maze fills its band its own way and uses none of that.
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
 * sixty kilobytes of a dongle's RAM spent on the games nobody is watching.
 *
 * The size is the largest of what the renderers ask for, and each of them
 * asserts its own maximum against it rather than trusting this number - so
 * widening a sprite fails the build here instead of running off the end.
 */
#define PM_BAND_PX 5280
extern uint8_t pm_band[PM_BAND_PX * 2];

/* rgb888 down to the panel's 5-6-5 */
uint16_t pm_rgb565(uint32_t rgb888);

/* implemented by the platform: push w*h RGB565 (big endian) pixels at x,y */
extern void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels);

/* ------------------------------------------------------------------ */
/* the rectangle being painted                                         */
/* ------------------------------------------------------------------ */

/*
 * A renderer says which rectangle of the panel it is filling and then stamps
 * into it in panel coordinates, letting everything outside fall on the floor.
 * The rectangle is state rather than an argument because it is the same for
 * every stamp in a band and threading it through each of them would be four
 * more arguments on every call in two renderers.
 *
 * It is four globals and an inlined store for exactly one reason: this is the
 * inner loop of the whole shield.  A frame is a few thousand pixels, each one
 * a bounds check and two bytes, and a call across a translation unit for each
 * of them is a millisecond of a sixty-six millisecond frame spent on function
 * prologues.
 */
extern int pm_bx, pm_by, pm_bw, pm_bh;

static inline void pm_band_begin(int x, int y, int w, int h) {
    pm_bx = x;
    pm_by = y;
    pm_bw = w;
    pm_bh = h;
}

static inline void pm_put(int x, int y, uint16_t c) {
    if (x < pm_bx || x >= pm_bx + pm_bw || y < pm_by || y >= pm_by + pm_bh) {
        return;
    }
    uint8_t *p = pm_band + 2 * ((y - pm_by) * pm_bw + (x - pm_bx));
    p[0] = (uint8_t)(c >> 8);
    p[1] = (uint8_t)(c & 0xFF);
}

void pm_fill(int x, int y, int w, int h, uint16_t c);

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

/*
 * Five by seven, one byte per column with the top row in bit 0 - the shape
 * every small display font has had since they were burnt into character ROMs.
 * The dashboard's fonts live in helpers/, which is Zephyr's side of the fence;
 * nothing under game/ may reach over it, and a score and a word or two do not
 * need more than the alphabet and the digits.
 */
#define PM_GLYPH_W 5
#define PM_GLYPH_H 7
#define PM_GLYPH_ADV 6 /* one column of air between letters */

/* how wide a string comes out, so it can be centred or right aligned */
int pm_text_w(const char *s, int scale);
/* uppercase letters, digits and space; anything else is drawn as a space */
void pm_text(int x, int y, int scale, uint16_t c, const char *s);
/* a number zero padded to n digits, so a readout never changes width */
void pm_digits(uint32_t value, int digits, char *buf);
