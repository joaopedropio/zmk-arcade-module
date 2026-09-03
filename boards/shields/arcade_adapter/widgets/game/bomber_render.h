/*
 * Bomberman dongle - renderer (portable, RGB565 big endian).
 *
 * The same bargain as the other three: no LVGL objects and no frame buffer for
 * the whole panel, only the rectangles that changed, painted into panel.h's
 * shared band and handed to arc_blit().
 *
 * It stamps sprites into a cleared rectangle the way the shooter and the
 * crossing do, rather than asking each pixel what is on it.  What is different
 * is that most of what moves here is the ground: a brick coming down, a flame
 * arriving and leaving, a pickup appearing under a wall that is no longer
 * there.  So the board is redrawn a cell at a time, and every cell keeps one
 * number saying what it looked like last frame - what is in it, what is
 * burning, which way the flame runs, how far the fuse on top of it has burnt.
 * Anything that can change a cell's appearance has to be in that number or the
 * cell is never repainted, which is exactly what the simulator's repaint check
 * catches.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bomber_core.h"

/*
 * The readout sits in the band above the board, which is the only part of the
 * panel the board does not use.  Score at double size on the left, because it
 * is the one thing read from across a desk; lives, bombs and reach along the
 * right at single size, each behind its own little icon so three numbers in a
 * row can still be told apart.
 */
#define BB_HUD_Y 6
#define BB_HUD_X 5
#define BB_ICON  10 /* the box each of the three right-hand icons is drawn in */

/* the notice across the middle of the board */
#define BB_BANNER_Y (BB_OY + BB_H / 2 - 10)
#define BB_BANNER_SCALE 3

/*
 * Solid ground, soft ground, the things standing on it and the two colours a
 * flame needs.  Fire is the only thing here that is two colours on purpose: a
 * blast drawn in one reads as a coloured rectangle, where a hot core inside a
 * cooler arm reads as fire even at sixteen pixels.
 */
typedef struct {
    uint16_t floor;      /* what a cleared cell is */
    uint16_t solid;      /* the pillars and the border */
    uint16_t brick;      /* soft wall, the thing a bomb is for */
    uint16_t brick_edge; /* its courses, which is what stops it reading as a tile */
    uint16_t bomb;
    uint16_t flame;
    uint16_t flame_hot; /* the core of a blast, and the spark on a fuse */
    uint16_t bomber;
    uint16_t bomber_trim; /* its visor, which is which way it is facing */
    uint16_t foe;
    uint16_t foe_eye;
    uint16_t pickup; /* whatever was under a brick */
    uint16_t door;   /* the way out, once something has uncovered it */
    uint16_t hud;    /* score, lives and the notice */
} bb_palette;

void bb_render_set_palette(const bb_palette *p);
void bb_render_default_palette(bb_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void bb_render_frame(bb_game *g);
