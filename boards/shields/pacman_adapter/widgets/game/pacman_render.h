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
 * The maze is walled in by a line drawn round it, one PM_WALL_LINE thick and
 * standing PM_WALL_INSET off the playfield - the same line a wall tile would
 * draw if the maze had a border row - so the corridor round the outside comes
 * out the width of the ones inside it, and the margin behind it is that wall's
 * fill.  PM_BORDER_GAP is the clearance kept round the playfield for the
 * sprites that hang over its edge.
 */
#define PM_BORDER_GAP 2                                 /* clear pixels round the playfield */
/*
 * The ghost house is drawn as one box across its nine tiles, not as nine wall
 * tiles: a ring of tubes reads as a fat blob at this size, where the arcade's
 * house is a wide chamber with a door in the top of it.  The box stands
 * PM_WALL_INSET off the block on the left and right, like any other wall, and
 * PM_HOUSE_SQUAT further in at the top and bottom - which is what makes it
 * wider than it is tall, and leaves the corridor round it that much deeper.
 * The door is PM_DOOR_W of the top line, drawn in the door colour rather than
 * left as a gap, since a gap in a one pixel line is just a hole.
 */
#define PM_HOUSE_SQUAT 12                               /* pulls the top and bottom in */
#define PM_DOOR_W      16                               /* about a quarter of the box */
#define PM_SPRITE     28                                /* sprite box, any size */
#define PM_SPRITE_OFF ((PM_TILE - PM_SPRITE) / 2)       /* centres it on the tile */

/*
 * What gets painted is the playfield plus the clearance ring round it: a
 * sprite is wider than its tile, so one in the outermost corridor hangs over
 * the edge of the maze, and clipping it there would flatten its side.  The
 * ring is background, and the border fills the margin outside it.
 */
#define PM_CANVAS_LO (-PM_BORDER_GAP)                   /* in maze coordinates */
#define PM_CANVAS_W  (PM_WIDTH + 2 * PM_BORDER_GAP)
#define PM_CANVAS_H  (PM_HEIGHT + 2 * PM_BORDER_GAP)

/* the widest run of pixels any one blit can carry; panel.h's band holds it */
#define PM_BLIT_MAX  (PM_CANVAS_W * PM_TILE)

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

void pm_render_set_palette(const pm_palette *p);
void pm_render_default_palette(pm_palette *p);

/* draws the frame; repaints the whole maze when the core asks for it */
void pm_render_frame(pm_game *g);
