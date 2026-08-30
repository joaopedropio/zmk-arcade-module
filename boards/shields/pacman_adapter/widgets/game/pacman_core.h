/*
 * Pac-Man dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The grid is odd-sized on purpose.  Walls and corridors are both exactly one
 * tile thick, which only works on a lattice: tiles on even rows and columns
 * are always corridor, tiles on an odd row AND an odd column are always wall,
 * and the ones in between are the links that may be either.  Every 2x2 block
 * then holds exactly one tile of each kind, so neither walls nor corridors
 * can ever double up.  An odd count also puts a corridor along all four edges
 * and a corridor tile dead centre, which is where the ghost house goes.
 *
 * 9 is the smallest odd grid that still holds a maze worth watching, and it
 * buys a big tile: at 24px the sprites run 28px, wider than the tile itself,
 * where the old 12x12 grid could only afford 18px.  On a 240px panel legibility beats layout - the animation
 * has to read at arm's length.
 *
 * A wall is one tile thick in the layout, so the tile size sets how heavy the
 * walls can look; PM_WALL_INSET then trims what is actually drawn, which is
 * how the walls get lighter without the corridors or the sprites following
 * them down.  9 * 24 also leaves an even margin, so the maze sits dead centre.
 */
#define PM_COLS   9
#define PM_ROWS   9
#define PM_TILE   24                     /* pixels per tile */
#define PM_WIDTH  (PM_COLS * PM_TILE)    /* 216 */
#define PM_HEIGHT (PM_ROWS * PM_TILE)    /* 216 */

/*
 * No tile is spent on a border: the maze is walled in by a line drawn round
 * the edge of the panel itself, so all PM_COLS x PM_ROWS tiles are playfield
 * and the outer ring of them is a corridor.  When the grid does not divide
 * the panel exactly the playfield is centred and the leftover margin becomes
 * that line; when it divides exactly the line is drawn over the outermost
 * pixels of the maze instead.
 */
/*
 * PM_MARGIN is the left and top margin, PM_MARGIN_END the right and bottom
 * one.  They are equal at 9x24, but differ by a pixel whenever the panel and
 * the grid leave an odd number over, and then the maze cannot sit dead centre.
 * Anything painting the margin has to use the right one of the two or it
 * leaves a line of the panel untouched down the far edge.
 */
#define PM_PANEL      240                                    /* the dongle's square panel */
#define PM_MARGIN     ((PM_PANEL - PM_WIDTH) / 2)            /* 12 at 9x24 */
#define PM_MARGIN_END (PM_PANEL - PM_MARGIN - PM_WIDTH)      /* 12 at 9x24 */

#define PM_GHOSTS 4
#define PM_ACTORS (PM_GHOSTS + 1)        /* ghosts + pac-man */

/*
 * The ghost house: the centre tile, the ring of eight around it, and the top
 * link of that ring left open as the door.  The renderer wants the block as a
 * whole rather than tile by tile - it draws one box across all nine, the way
 * the arcade's house is one chamber rather than a wall per tile - so the
 * extent lives here with the maze rather than with the drawing.
 */
#define PM_HOUSE_X0 (PM_COLS / 2 - 1)
#define PM_HOUSE_X1 (PM_COLS / 2 + 1)
#define PM_HOUSE_Y0 (PM_ROWS / 2 - 1)
#define PM_HOUSE_Y1 (PM_ROWS / 2 + 1)

/* tile contents */
enum {
    PM_T_WALL = 0,
    PM_T_PATH,
    PM_T_PELLET,
    PM_T_POWER,
    PM_T_DOOR,      /* ghost house door: ghosts only */
    PM_T_HOUSE,     /* ghost house inside: ghosts only */
    PM_T_HWALL,     /* ghost house wall */
};

typedef enum {
    PM_UP = 0,
    PM_RIGHT,
    PM_DOWN,
    PM_LEFT,
    PM_DIRS,
    PM_NODIR,
} pm_dir;

typedef enum {
    PM_G_HOUSE = 0, /* waiting inside / walking out */
    PM_G_OUT,       /* roaming the maze */
    PM_G_EYES,      /* eaten, eyes running back home */
    PM_G_ENTER,     /* eyes dropping back into the house */
} pm_ghost_state;

typedef enum {
    PM_READY = 0,   /* short hold before the round starts */
    PM_PLAY,
    PM_DYING,
    PM_CLEARED,     /* maze flashes, next level */
} pm_phase;

typedef struct {
    int16_t x, y;   /* top-left pixel of the actor's tile box */
    pm_dir dir;
} pm_actor;

typedef struct {
    pm_actor actor;
    pm_ghost_state state;
    uint16_t release;   /* frames left in the house */
    uint8_t bob;        /* idle bobbing phase */
} pm_ghost;

/*
 * What the last pm_step() would have made a noise about.  The core only says
 * that it happened - what it sounds like, and whether anything is listening,
 * is the caller's business.  The fright siren is not here because it is a
 * state rather than a moment: watch g->fright for that one.
 */
enum {
    PM_SFX_PELLET = 1u << 0, /* a pellet went */
    PM_SFX_POWER = 1u << 1,  /* and a power pellet, so the ghosts turned blue */
    PM_SFX_GHOST = 1u << 2,  /* one of them was caught */
    PM_SFX_DEATH = 1u << 3,  /* and one of them caught Pac-Man */
    PM_SFX_START = 1u << 4,  /* a round is about to begin */
    PM_SFX_CLEAR = 1u << 5,  /* the maze is empty */
};

typedef struct {
    uint8_t tiles[PM_ROWS][PM_COLS];

    pm_actor pac;
    pm_ghost ghosts[PM_GHOSTS];

    pm_phase phase;
    uint16_t phase_timer;

    uint32_t tunnel_rows;   /* bit y set: row y runs off the edge and wraps */
    uint16_t pellets_left;
    uint16_t hungry;        /* frames since the last pellet, breaks stalemates */
    uint16_t fright;        /* frames of frightened mode left */
    uint8_t fright_eaten;   /* ghosts eaten on the current power pellet */
    uint32_t score;
    uint8_t lives;
    uint8_t level;

    uint8_t pac_speed;      /* pixels per frame */
    uint8_t ghost_speed;

    uint8_t mouth;          /* pac-man mouth animation phase */
    uint8_t death;          /* death animation phase */
    uint16_t frame;

    bool scatter;
    uint16_t mode_timer;

    uint8_t sfx;            /* PM_SFX_* from the last step, for whoever plays them */

    bool redraw;            /* renderer must repaint the whole maze */
    bool flash;             /* level-cleared flash state */
    bool hide_ghosts;

    uint32_t rng;
} pm_game;

void pm_init(pm_game *g, uint32_t seed);
void pm_step(pm_game *g);

/* pixels per frame; 1 = slow, 2 = normal, 3 = quick */
void pm_set_speed(pm_game *g, uint8_t pac_px, uint8_t ghost_px);

/* helpers shared with the renderer */
/* false while a ghost waits its turn in the one-tile house, so the queue
 * behind the next one out is not drawn stacked on the same tile */
bool pm_ghost_visible(const pm_game *g, int i);
bool pm_power_visible(const pm_game *g);
bool pm_fright_flashing(const pm_game *g);
