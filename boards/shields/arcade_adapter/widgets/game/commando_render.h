/*
 * Metal Slug dongle - renderer (portable, RGB565 big endian).
 *
 * The same bargain as the other four - dirty rectangles into panel.h's shared
 * band, no LVGL and no frame buffer - and by some way the hardest of the five
 * to keep inside it, because here the whole world moves every frame.
 *
 * The panel carries a few thousand pixels a frame.  A scrolling background
 * painted the obvious way is fifty-seven thousand, every frame, for ever.  So
 * the ridge is not painted as a picture that slides; it is painted as two
 * hundred and forty columns, each of which knows what it looked like last
 * frame, and only the columns whose silhouette actually moved are redrawn -
 * and of those, only the rows between where the outline was and where it is
 * now.  Over a flat stretch of ground, or under a flat hilltop, a column looks
 * exactly the same after the scroll as before it and costs nothing at all.
 *
 * That is what makes every flat-topped thing in this game load-bearing.  A
 * hill with a rounded top, grass drawn at world positions, a rock lying on the
 * ground, a gradient in the sky - any of them would give every column on the
 * panel a different look on every frame, and the game would cost twenty times
 * what it costs.  Detail here has to be locked to the panel (the readout) or
 * to a sprite (everything that moves), and never to the world.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "commando_core.h"

/*
 * The readout band, and the three things in it: the score at double size on
 * the left because it is what is read from across a desk, and the lives and
 * the grenades along the right behind their own marks, because two numbers
 * side by side say nothing about which is which.
 */
#define CM_HUD_X 5
#define CM_HUD_Y 7
#define CM_ICON  9
/* the grenade count sits left of the lives, clear of the longest score */
#define CM_GRENADE_X 148

/* the notice across the ridge */
#define CM_BANNER_Y     78
#define CM_BANNER_SCALE 3

/* how thick the lit top of the ground is; it is what makes a ridge a surface */
#define CM_EDGE 3

/*
 * And how far below the surface a column stops being plain ground.  Everything
 * drawn between the lip and here - the lit edge and the seam under it - moves
 * with the surface, so it is also how far past a moved outline the incremental
 * redraw has to reach.  The two uses are the same number on purpose: drawing
 * one pixel further down than this says was the whole of a run of stale
 * pixels the first time round.
 */
#define CM_SKIN 10

/*
 * Twelve colours, and the split between them is what the panel has to make
 * readable: what can be walked on, what is behind it and cannot, who is
 * shooting, and what is in the air.  The ground and its edge are two settings
 * rather than one and a shade, because a preset that wants a night raid needs
 * the lit edge to be the thing that separates the ridge from the dark it is
 * standing in.
 */
typedef struct {
    uint16_t sky;
    uint16_t hill;   /* the skyline behind, at a quarter of the scroll */
    uint16_t ground;
    uint16_t edge;   /* the lit top of it */
    uint16_t hero;
    uint16_t hero_trim; /* helmet band and rifle */
    uint16_t grunt;
    uint16_t grunt_trim;
    uint16_t shot;
    uint16_t grenade;
    uint16_t boom;
    uint16_t crate;
    uint16_t hud;
} cm_palette;

void cm_render_set_palette(const cm_palette *p);
void cm_render_default_palette(cm_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void cm_render_frame(cm_game *g);
