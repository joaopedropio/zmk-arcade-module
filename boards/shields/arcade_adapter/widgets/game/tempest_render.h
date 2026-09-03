/*
 * The well dongle - renderer (portable, RGB565 big endian).
 *
 * The same bargain as the other renderers: no LVGL objects and no frame buffer
 * for the whole panel, only the rectangles that changed, painted into panel.h's
 * shared band and handed to arc_blit().  It works the crossing's way round - a
 * rectangle is cleared to the ground behind it and then everything reaching
 * into it is stamped, in a fixed order.
 *
 * This is the only game here drawn entirely in lines, which is what it was on
 * the tube it came off, and that changes what a rectangle costs.  A sprite is
 * cheap to stamp and expensive to clear; a well is the other way round - the
 * sixteen spokes and two rims cross every rectangle on the panel, so the
 * expensive part of a small repaint is redrawing the bits of the well behind
 * whatever moved.  line() clips to the band along its own major axis rather
 * than letting arc_put() throw the misses away, because a spoke is a hundred
 * pixels long and a rectangle is thirty across: without the clip a frame spends
 * most of its time on pixels it has already decided not to draw.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tempest_core.h"

/*
 * The readout, in the band above the well.  The score is double size on the
 * left where it is the one thing readable from across a desk, the lives are
 * little claws beside it, the superzapper is a mark that goes out when it is
 * spent, and the level is two digits at the far end.
 */
#define TP_HUD_X 4
#define TP_HUD_Y 1
#define TP_LIVES_X 86
#define TP_LIVES_Y 4
#define TP_ZAP_X 150
#define TP_ZAP_Y 4
#define TP_LEVEL_X 214
#define TP_LEVEL_Y 5

/* the notices, over the far rim - the emptiest part of the well, and the only
 * place a word can go without covering the lane the claw is working */
#define TP_BANNER_Y 123
#define TP_BANNER_SCALE 2

/*
 * How far up the well a thing in it reaches, in depth units.  It is a depth
 * rather than a number of pixels on purpose: a flipper at the far rim really is
 * three pixels tall and one at the near rim twenty, and sizing them in pixels
 * instead would give a well whose perspective the enemies do not obey.
 */
#define TP_BODY 40

/*
 * Twelve colours, and everything else half a step from one of them.  The well
 * and the rim are the board; everything in it is told apart by colour alone,
 * because at the far end of the tube a flipper and a tanker are four pixels
 * each and no shape survives that.
 */
typedef struct {
    uint16_t site;
    uint16_t well; /* the spokes, and dimmed, the far rim */
    uint16_t rim;  /* the near one, which is where the claw lives */
    uint16_t claw;
    uint16_t shot;
    uint16_t flipper;
    uint16_t tanker;
    uint16_t spiker;
    uint16_t pulsar;
    uint16_t spike;
    uint16_t bolt;
    uint16_t hud;
} tp_palette;

void tp_render_set_palette(const tp_palette *p);
void tp_render_default_palette(tp_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void tp_render_frame(tp_game *g);
