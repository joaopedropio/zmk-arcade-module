/*
 * Space Shooter dongle - renderer (portable, RGB565 big endian).
 *
 * Same bargain the maze's renderer makes: no LVGL objects and no frame buffer
 * for the whole panel, only the rectangles that changed, painted into
 * panel.h's shared band and handed to arc_blit().
 *
 * What differs is where those rectangles come from.  The maze is a grid and
 * only its actors move, so its renderer follows five sprites; here everything
 * on screen moves at once, so each thing that can be drawn keeps the box it
 * was last drawn in and the frame repaints the union of that and the box it
 * wants now.  Anything painted is composed from the game state alone - never
 * from what happened to be on the panel - which is what lets the simulator
 * check an incremental frame against a full repaint and expect them equal.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "shooter_core.h"

/*
 * Where the readout sits.  The score runs along the top at double size,
 * because it is the one thing anybody reads from across a desk, with the lives
 * left beside it; whatever pickup is running goes along the bottom at single
 * size, where it is there if you look but does not compete with the game.
 * Both bands are drawn last and repainted when their words change, so the ship
 * may fly straight through either without them having to move.
 */
#define SS_HUD_X 6
#define SS_HUD_Y 4
#define SS_FOOT_Y 228

/* the game over notice, across the middle of the panel */
#define SS_BANNER_Y 104
#define SS_BANNER_SCALE 3

/*
 * The shield bubble stands off the hull far enough to read as a field around
 * it rather than as an outline of it, which is what sets how wide the ship's
 * dirty rectangle gets while one is running.
 */
#define SS_SHIELD_R 17

/*
 * The starfield is behind everything and the readout in front of it; the rest
 * is drawn in the order a collision would happen in - meteors, then the ship
 * that is dodging them, then the blast that says it did not.  The ship is two
 * colours and its exhaust a third, which is what lets a triangle turning on
 * the spot still read as a ship pointing somewhere.
 */
typedef struct {
    uint16_t space;     /* the ground everything is drawn on */
    uint16_t star;
    uint16_t ship;      /* the hull */
    uint16_t trim;      /* the cockpit down the middle of it */
    uint16_t thruster;  /* the flame under it, and the shield round it */
    uint16_t bullet;
    uint16_t rock;      /* a meteor's fill */
    uint16_t rock_edge; /* and the rim that gives it its shape */
    uint16_t blast;
    uint16_t power;     /* a pickup, and the shield it grants */
    uint16_t hud;       /* score, lives, and the game over notice */
} ss_palette;

void ss_render_set_palette(const ss_palette *p);
void ss_render_default_palette(ss_palette *p);

/* draws the frame; repaints the whole panel when the core asks for it */
void ss_render_frame(ss_game *g);
