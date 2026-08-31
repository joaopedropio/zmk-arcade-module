/*
 * Space Shooter dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The other half of what the panel can show.  Where the maze is a grid, this
 * is free movement: a ship along the bottom, meteors drifting down out of a
 * fixed starfield, and shots going the other way.  Nobody is holding the
 * stick - the ship picks its own target, leads it, and gets out of the way of
 * whatever is about to land on it.
 *
 * Everything is one flat playfield the size of the panel, so there is no
 * geometry to keep in step the way pacman_core.h's lattice has: what is
 * load-bearing here is the fixed point, the counts, and where the ship sits.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * Positions and speeds are in eighths of a pixel.  A meteor crossing the panel
 * in eight seconds moves a third of a pixel per frame, and rounding that up to
 * one would make everything on screen travel at the same speed; rounding it
 * down would stop it dead.  Eighths are the coarsest step that still tells a
 * big rock from a small one, and 240 * 8 still fits an int16_t with room for a
 * sprite hanging off either edge.
 */
#define SS_SUB 8
#define SS_PX(v) ((int)((v) / SS_SUB))    /* eighths to whole pixels */

/*
 * The ship is drawn from a 25x17 stencil with a flame under it, and it never
 * leaves its row: this is a shooter that scrolls towards you, not one you fly
 * around in.  An odd width gives it a centre column to put the nose and the
 * shot on.
 */
#define SS_SHIP_W    25
#define SS_SHIP_H    17
#define SS_SHIP_Y    198                          /* top row of the hull */
#define SS_FLAME_H   7                            /* the exhaust below it */
#define SS_SHIP_MID  (SS_SHIP_Y + SS_SHIP_H / 2)  /* what a meteor has to hit */

/*
 * How much of the ship a meteor actually has to reach.  The stencil is 25
 * wide, almost all of it wing, and a game that ends on a wingtip graze reads
 * as unfair from across a room - so the hit box is the cockpit and nothing
 * else.  It is the one number to move if the ship starts looking lucky.
 */
#define SS_SHIP_R 7

/*
 * Meteors come in three sizes and each one breaks into two of the size below,
 * so a wave of SS_WAVE_MAX big ones ends up as four times that many small
 * ones.  SS_ROCKS is exactly that number: any less and the last split would
 * quietly drop a rock, and a wave could never be cleared because the count
 * that ends it was never spawned.
 */
#define SS_WAVE_MAX 4
#define SS_ROCKS    (SS_WAVE_MAX * 4)
#define SS_SHOTS    6
#define SS_BLASTS   5

/*
 * The starfield does not scroll.  Moving all of it would repaint the whole
 * panel every frame - 57600 pixels down an SPI bus that comfortably carries a
 * few thousand - so the stars hold still and blink instead, and what gives the
 * screen its motion is the meteors.  Each star that does blink costs one blit
 * of one pixel.
 */
#define SS_STARS 34

enum { SS_SMALL = 0, SS_MEDIUM, SS_BIG, SS_SIZES };

/* radius in pixels, by size */
extern const uint8_t ss_rock_r[SS_SIZES];

typedef enum {
    SS_READY = 0, /* the wave banner is up and the meteors are drifting in */
    SS_FLY,
    SS_DEAD, /* the ship went; its blast is still playing */
    SS_OVER, /* out of lives, about to start again */
} ss_phase;

/*
 * What a pickup grants.  Three of them, because the store page makes a point
 * of there being several to find and one would read as a bug rather than a
 * bonus; more than three and the ship spends the whole animation flashing.
 */
typedef enum {
    SS_P_NONE = 0,
    SS_P_RAPID,  /* shots four times as often */
    SS_P_SPREAD, /* three shots, fanned */
    SS_P_SHIELD, /* the next meteor bounces off */
    SS_POWERS,
} ss_power;

typedef struct {
    int16_t x, y;   /* centre, in eighths */
    int16_t vx, vy; /* eighths per frame */
    uint8_t size;
    uint8_t shape; /* which set of bites is taken out of its disc */
    uint8_t spin;  /* quarter turn of those bites, so it tumbles */
    bool alive;
} ss_rock;

typedef struct {
    int16_t x, y, vx;
    bool alive;
} ss_shot;

typedef struct {
    int16_t x, y; /* whole pixels: a blast never moves */
    uint8_t age;
    bool alive;
} ss_blast;

typedef struct {
    uint8_t x, y;    /* whole pixels; a star never moves either */
    uint8_t period;  /* frames between blinks, 0 for one that never blinks */
    uint8_t phase;
} ss_star;

typedef struct {
    int16_t x, y, vy;
    uint8_t kind; /* an ss_power */
    bool alive;
} ss_drop;

typedef struct {
    ss_phase phase;
    uint16_t phase_timer;

    int16_t ship_x; /* centre of the hull, in eighths */
    uint16_t invuln; /* frames of blinking after a respawn */
    uint8_t cooldown;

    ss_power power;
    uint16_t power_left;

    ss_rock rocks[SS_ROCKS];
    ss_shot shots[SS_SHOTS];
    ss_blast blasts[SS_BLASTS];
    ss_star stars[SS_STARS];
    ss_drop drop;

    uint32_t score;
    uint8_t lives;
    uint16_t wave;
    uint16_t frame;

    uint8_t speed;    /* pixels per frame the ship may cross */
    uint16_t patient; /* frames since anything was hit, breaks stalemates */

    bool redraw; /* the renderer must repaint the whole panel */

    uint32_t rng;
} ss_game;

void ss_init(ss_game *g, uint32_t seed);
void ss_step(ss_game *g);

/* pixels per frame the ship may travel; 3 = slow, 4 = normal, 5 = quick */
void ss_set_speed(ss_game *g, uint8_t px);

/* helpers shared with the renderer */
bool ss_ship_visible(const ss_game *g);
bool ss_star_lit(const ss_game *g, int i);
/* the word to print for whatever is running, or NULL */
const char *ss_power_name(const ss_game *g);
/* the banner across the middle, or NULL when there is none */
const char *ss_banner(const ss_game *g, char *buf, int len);
