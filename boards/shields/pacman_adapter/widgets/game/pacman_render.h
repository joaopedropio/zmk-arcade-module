/*
 * Pac-Man dongle - renderer (portable, RGB565 big endian).
 *
 * Everything is drawn straight into small pixel buffers and handed to
 * pm_blit(), which the platform maps onto its display.  No LVGL objects,
 * no frame buffer for the whole screen: only the tiles that changed are
 * pushed out, which is what keeps this cheap enough for a dongle.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "pacman_core.h"

/*
 * Proportions.  The maze grid is PM_TILE, but the walls are drawn as thin
 * tubes hugging the corridors rather than filled blocks, and the sprites are
 * bigger than their tile - both the way the arcade does it, where a 16px
 * Pac-Man runs down an 8px corridor.  Matching PM_TILE's parity centres the
 * sprite on a whole pixel; an odd sprite in an even tile (or the reverse) is
 * fine too, it just sits half a pixel towards the tile's top-left.
 */
#define PM_WALL_LINE  1                                 /* wall outline thickness */
/*
 * How far the drawn wall stops short of its tile on every side that faces open
 * space.  A wall is one tile thick in the layout, but drawing it in by this
 * much on each side makes it 2 * PM_WALL_INSET thinner on screen and the
 * corridor beside it that much wider, without touching the maze itself or the
 * size of the sprites running down it.  Walls that meet stay joined: only
 * open-facing sides are pulled in.
 */
#define PM_WALL_INSET 4                                 /* wall drawn 8px thinner */
/*
 * Corner radius where two open sides of a wall meet.  It reaches PM_WALL_INSET
 * further in than the tile edge, so the two together must be no more than half
 * a tile or opposite corners would overlap - a _Static_assert in the renderer
 * holds that.  When the radius reaches half the drawn wall, the end of a
 * one-tile-wide wall becomes a semicircular cap, which is where 8 puts it now.
 */
#define PM_WALL_R     (PM_TILE / 3)                     /* rounded wall corners */
/*
 * Thickness of the outer wall, the frame drawn round the playfield.  The
 * margin it lives in is whatever PM_COLS * PM_TILE leaves over, which grows as
 * the tile shrinks; without a cap the outer wall would get heavier every time
 * the inner ones got lighter.  Whatever margin is left outside the frame is
 * background, so the maze stays centred in the panel either way.
 */
#define PM_BORDER_LINE 3                                /* outer wall thickness */
#define PM_SPRITE     26                                /* sprite box, any size */
#define PM_SPRITE_OFF ((PM_TILE - PM_SPRITE) / 2)       /* centres it on the tile */

typedef struct {
    uint16_t bg;
    uint16_t wall_fill;
    uint16_t wall_edge;
    uint16_t wall_flash;
    uint16_t house_fill;
    uint16_t house_edge;
    uint16_t door;
    uint16_t pellet;
    uint16_t pac;
    uint16_t ghost[PM_GHOSTS];
    uint16_t fright_body;
    uint16_t fright_face;
    uint16_t flash_body;
    uint16_t flash_face;
    uint16_t eye;
    uint16_t pupil;
} pm_palette;

uint16_t pm_rgb565(uint32_t rgb888);
void pm_render_set_palette(const pm_palette *p);
void pm_render_default_palette(pm_palette *p);

/* draws the frame; repaints the whole maze when the core asks for it */
void pm_render_frame(pm_game *g);

/* implemented by the platform: push w*h RGB565 (big endian) pixels at x,y */
extern void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels);
