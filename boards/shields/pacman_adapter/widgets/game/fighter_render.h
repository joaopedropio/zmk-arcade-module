/*
 * Street Fighter dongle - renderer (portable, RGB565 big endian).
 *
 * The same bargain as the other four: no LVGL objects and no frame buffer for
 * the whole panel, only the rectangles that changed, painted into panel.h's
 * shared band and handed to pm_blit().
 *
 * This is the cheapest of the five to draw, and for one reason: the stage does
 * not move.  Nothing here scrolls, nothing here scatters, and the only things
 * that change are two fighters, at most two fireballs, three flashes and the
 * readout - so a frame is two sprite-sized rectangles and usually nothing
 * else.  What that buys is the room to draw the two of them properly: a
 * fighter is a dozen rectangles rather than a blob, because at forty-four
 * pixels tall the difference between a guard and a sweep has to be visible
 * from across a desk or the whole game is two colours bumping together.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "fighter_core.h"

/*
 * The readout: a health bar apiece with the clock between them, and under each
 * bar the rounds that fighter has taken.  The bar is FG_HEALTH pixels wide on
 * purpose - a point of damage is a pixel of bar, so nothing is scaled and a
 * fighter one hit from losing can never round up to a full-looking bar.
 */
#define FG_BAR_X 6
#define FG_BAR_Y 10
#define FG_BAR_W FG_HEALTH
#define FG_BAR_H 9
#define FG_PIP   5 /* a round taken, as a square under the bar */

_Static_assert(2 * (FG_BAR_X + FG_BAR_W) + 26 <= PM_PANEL,
               "the clock has to fit between the two bars");

/* the notice across the stage, clear of both fighters' heads */
#define FG_BANNER_Y     (FG_OY + 34)
#define FG_BANNER_SCALE 3

/*
 * Where the back wall meets the backdrop.  It sits above both fighters' heads
 * so that the line behind them is a line rather than something they walk
 * through, and low enough that the stage is mostly wall - a fight drawn
 * against an empty rectangle of sky reads as two sprites on a colour.
 */
#define FG_HORIZON (FG_FLOOR - 96)

_Static_assert(FG_HORIZON < FG_FLOOR - FG_BODY_H, "the wall would cut a head in half");

/*
 * Two fighters, the stage they are on and the four colours the readout needs.
 * The two bodies are separate settings rather than one and a shade of it: a
 * fight is the one thing on this dongle where telling two of the same shape
 * apart is the whole point, and a preset that got that wrong would be
 * unwatchable rather than merely off.
 */
typedef struct {
    uint16_t sky;    /* the backdrop above the wall */
    uint16_t crowd;  /* the wall behind the stage */
    uint16_t floor;  /* and the stage itself */
    uint16_t body[2];
    uint16_t trim[2]; /* headband, belt and whatever is guarding */
    uint16_t spark;
    uint16_t fireball;
    uint16_t health;      /* what is left of a bar */
    uint16_t health_lost; /* and what is on its way out of it */
    uint16_t hud;
} fg_palette;

void fg_render_set_palette(const fg_palette *p);
void fg_render_default_palette(fg_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void fg_render_frame(fg_game *g);
