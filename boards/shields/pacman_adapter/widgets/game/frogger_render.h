/*
 * Crossing dongle - renderer (portable, RGB565 big endian).
 *
 * Same bargain the other two renderers make: no LVGL objects and no frame
 * buffer for the whole panel, only the rectangles that changed, painted into
 * panel.h's shared band and handed to pm_blit().  It works the shooter's way
 * round rather than the maze's - a rectangle is cleared to whatever ground it
 * covers and then every sprite reaching into it is stamped, in a fixed order.
 *
 * What is different here is how much of the panel is moving.  The maze has
 * five sprites on a board that holds still and the shooter has a dozen; this
 * has thirty-odd, and every one of them moves every frame, so the frame costs
 * around twice what either of theirs does.  The two things that keep it to
 * that are the ground, which is drawn from the row layout and never changes,
 * and the rule that a sprite whose box and appearance both match last frame is
 * not repainted at all.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "frogger_core.h"

/*
 * The readout, in the two sixteen pixel bands the thirteen rows leave over.
 * The score goes top left at double size, where it is the one thing readable
 * from across a desk; the clock runs along the bottom as a bar rather than a
 * number, because a bar shrinking is the one part of this that can be
 * understood without reading anything.
 */
#define FR_HUD_X 6
#define FR_HUD_Y 1
#define FR_CLOCK_X 6
#define FR_CLOCK_Y (FR_FOOT + 5)
#define FR_CLOCK_W 150
#define FR_CLOCK_H 6

/* the game over and level notices, across the middle of the board */
#define FR_BANNER_Y 100
#define FR_BANNER_SCALE 3

/*
 * The frog is drawn from a twelve by twelve stencil turned to face the way it
 * last hopped, which is two pixels of air either side of its cell.  Written
 * out as rows of characters rather than as bit masks: it is the one sprite
 * here whose shape has to be recognisable rather than merely round, and a
 * shape nobody can read in the source is a shape nobody will fix.
 */
#define FR_ART FR_FROG_W

/*
 * The ground is four colours and everything on it is one more.  Water and road
 * are the two halves of the board and want to be told apart at a glance; the
 * bank is both safe strips; the hedge is what the bays are cut into.  A
 * darkened shade of each is derived rather than stored, so ripples, grain and
 * windows come for free with whatever the four are set to.
 */
typedef struct {
    uint16_t water;
    uint16_t road;
    uint16_t bank;
    uint16_t hedge;
    uint16_t frog;
    uint16_t frog_eye;
    uint16_t log;
    uint16_t turtle;
    uint16_t car;   /* and, lightened, the racing cars */
    uint16_t truck;
    uint16_t splat;
    uint16_t fly;
    uint16_t hud;
} fr_palette;

void fr_render_set_palette(const fr_palette *p);
void fr_render_default_palette(fr_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void fr_render_frame(fr_game *g);
