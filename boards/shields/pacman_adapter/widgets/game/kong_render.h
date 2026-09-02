/*
 * Girders dongle - renderer (portable, RGB565 big endian).
 *
 * The same bargain as the other renderers: no LVGL objects and no frame buffer
 * for the whole panel, only the rectangles that changed, painted into panel.h's
 * shared band and handed to pm_blit().  It works the crossing's way round - a
 * rectangle is cleared to whatever site it covers and then everything reaching
 * into it is stamped, in a fixed order.
 *
 * What is cheap here is that almost none of the panel moves.  The girders, the
 * ladders and the drum are drawn from the layout alone and never change, and
 * there are only ever a dozen things on top of them, so a frame costs about
 * what the maze's does despite the site being drawn from scratch every time a
 * barrel crosses it.  What that buys has to be paid for in one place: anything
 * that changes without moving - the ape winding up, a hammer being taken, the
 * flame in the drum - has to be in its `look` byte, or it goes stale.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "kong_core.h"

/*
 * The readout, all of it in the band above the site.  The score is double
 * size on the left where it is the one thing readable from across a desk, the
 * lives are little climbers beside it, the bonus runs down as a bar rather
 * than as a number - a bar shrinking is the one part of this that can be
 * understood without reading anything - and the level is two digits at the
 * far end.
 */
#define DK_HUD_X 4
#define DK_HUD_Y 1
#define DK_LIVES_X 82
#define DK_LIVES_Y 3
#define DK_CLOCK_X 112
#define DK_CLOCK_Y 5
#define DK_CLOCK_W 84
#define DK_CLOCK_H 6
#define DK_LEVEL_X 214
#define DK_LEVEL_Y 5

/* the game over and rescued notices, across the middle of the site */
#define DK_BANNER_Y 108
#define DK_BANNER_SCALE 3

/*
 * The climber, drawn from a ten by fourteen stencil in one of four poses.
 * Written out as rows of characters rather than as bit masks for the reason
 * the crossing's frog is: he is the one sprite here whose shape has to be
 * recognisable rather than merely blocky, and a shape nobody can read in the
 * source is a shape nobody will fix.  '#' is him, 'o' is his overalls and his
 * face, '.' is whatever is behind him.
 */
#define DK_ART_W DK_HERO_W
#define DK_ART_H DK_HERO_H

/*
 * Twelve colours and everything else derived from them.  The site is the ground
 * behind it all; the girder and the ladder are the board, and want to be told
 * apart at a glance because which of the two is under him is the whole of what
 * the pilot is deciding.  Shades between - the rivets, a rung, the far side of
 * a barrel, the flame in the drum - are half a step from one of these rather
 * than stored, so setting the girder colour sets what a girder looks like and
 * not merely what colour it is.
 */
typedef struct {
    uint16_t site;
    uint16_t girder;
    uint16_t ladder;
    uint16_t climber;
    uint16_t climber_trim;
    uint16_t barrel;
    uint16_t ape;
    uint16_t ape_trim;
    uint16_t lady;
    uint16_t hammer;
    uint16_t oil; /* the drum at the foot of the site; the flame over it is barrel */
    uint16_t hud;
} dk_palette;

void dk_render_set_palette(const dk_palette *p);
void dk_render_default_palette(dk_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void dk_render_frame(dk_game *g);
