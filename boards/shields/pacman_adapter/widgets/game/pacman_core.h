/*
 * Pac-Man dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PM_COLS   12
#define PM_ROWS   12
#define PM_TILE   20                     /* pixels per tile */
#define PM_WIDTH  (PM_COLS * PM_TILE)    /* 240 */
#define PM_HEIGHT (PM_ROWS * PM_TILE)    /* 240 */

/*
 * No tile is spent on a border: the maze is walled in by a line drawn round
 * the edge of the panel itself, so all PM_COLS x PM_ROWS tiles are playfield
 * and the outer ring of them is a corridor.  When the grid does not divide
 * the panel exactly the playfield is centred and the leftover margin becomes
 * that line; when it divides exactly the line is drawn over the outermost
 * pixels of the maze instead.
 */
#define PM_PANEL  240                              /* the dongle's square panel */
#define PM_MARGIN ((PM_PANEL - PM_WIDTH) / 2)      /* 0 at 12x20 */

#define PM_GHOSTS 4
#define PM_ACTORS (PM_GHOSTS + 1)        /* ghosts + pac-man */

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
bool pm_power_visible(const pm_game *g);
bool pm_fright_flashing(const pm_game *g);
