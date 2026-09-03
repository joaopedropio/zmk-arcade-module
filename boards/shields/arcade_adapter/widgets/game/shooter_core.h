/*
 * Space Shooter dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The other half of what the panel can show.  Where the maze is a grid, this
 * is free flight: a triangle that turns either way and thrusts along whatever
 * direction it is pointing, meteors drifting across from every edge, and shots
 * that go where the nose goes.  Nobody is holding the stick - the ship picks
 * its own target, leads it, and boosts out of the way of whatever it cannot
 * shoot in time.
 *
 * There are no waves and nothing to count.  Meteors are kept coming as fast as
 * they are destroyed, so the panel is always carrying about the same amount of
 * rock; what changes as the score climbs is how much that is and how quickly
 * it moves.
 *
 * Everything is one flat playfield the size of the panel, so there is no
 * geometry to keep in step the way pacman_core.h's lattice has.  What is
 * load-bearing here is the fixed point, the angle units and the counts.
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
 * Angles are 1/256 of a turn, which is why they fit in a uint8_t and why the
 * ship can never be at an illegal one: it wraps by overflowing.  0 points
 * along +x, and 64 along +y - down the panel, since y grows downwards - so
 * turning by a positive amount is turning clockwise on screen.
 */
/* sin(a) and cos(a) scaled by 1024, for whoever needs to point at something */
int ss_sin(uint8_t angle);
int ss_cos(uint8_t angle);
/* the angle from the origin to x, y, in the same units */
uint8_t ss_angle_of(int x, int y);

/*
 * The ship, in ship-local pixels with +x out of the nose.  A triangle with a
 * notch cut deep into its back, which is the shape this game has had since it
 * was drawn on a vector tube: nose, two rear corners, and a point between them
 * the fill stops at.  The notch reaching almost to the middle is what makes it
 * a swept pair of wings rather than a wedge - a wedge at this size reads as an
 * arrowhead, and an arrowhead does not have a front.
 *
 * The exhaust comes out of the notch, so its base sits inside the part that
 * was cut away and it appears from between the wings rather than across them.
 * The two radii are what the dirty rectangle is sized from.
 */
#define SS_NOSE       15
#define SS_REAR       -8
#define SS_REAR_SIDE  9
#define SS_NOTCH      -1
#define SS_FLAME_BASE -4                  /* where the cone starts, inside the notch */
#define SS_FLAME_HALF 3
#define SS_HULL_R     16                  /* everything solid fits inside this */
#define SS_FLAME_R    19                  /* and the flame reaches this far back */

/*
 * How much of the ship a meteor actually has to reach.  Most of a triangle is
 * the thin corners either side of the tail, and a game that ends on one of
 * those reads as unfair from across a room - so the hit box is the middle of
 * the hull and nothing else.  It is the one number to move if the ship starts
 * looking lucky.
 */
#define SS_SHIP_R 7

/*
 * Meteors come in three sizes and each one breaks into two of the size below.
 * What bounds the count is not a wave any more but the drawing: a big meteor
 * costs about five times the pixels of a small one, so the spawner works in
 * that weight rather than in rocks, and the panel carries roughly the same
 * number of pixels whatever the mix happens to be.  SS_ROCKS is then only the
 * ceiling the lightest possible mix could reach.
 */
#define SS_ROCKS   18
#define SS_SHOTS   6
#define SS_BLASTS  5

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
    SS_FLY = 0,
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
    SS_P_RAPID,  /* shots three times as often */
    SS_P_SPREAD, /* three shots, fanned either side of the nose */
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
    int16_t x, y, vx, vy;
    uint8_t life; /* frames before it fizzles, so a miss does not orbit */
    bool alive;
} ss_shot;

typedef struct {
    int16_t x, y; /* whole pixels: a blast never moves */
    uint8_t age;
    bool alive;
} ss_blast;

typedef struct {
    uint8_t x, y;   /* whole pixels; a star never moves either */
    uint8_t period; /* frames between blinks, 0 for one that never blinks */
    uint8_t phase;
} ss_star;

typedef struct {
    int16_t x, y, vx, vy;
    uint8_t kind; /* an ss_power */
    bool alive;
} ss_drop;

typedef struct {
    int16_t x, y;    /* centre of the hull, in eighths */
    int16_t vx, vy;  /* it carries its momentum; only drag takes it away */
    uint8_t angle;
    bool thrusting;  /* what the pilot did this frame, so the flame can show it */
} ss_ship;

typedef struct {
    ss_phase phase;
    uint16_t phase_timer;

    ss_ship ship;
    uint16_t invuln; /* frames of blinking after a respawn */
    uint8_t cooldown;

    /*
     * The way out the pilot committed to, and how long it will hold it for.
     * Recomputing a dodge every frame looks like indecision and is worse than
     * that: near the line a meteor is travelling on, the direction to step
     * aside flips between one side and the other, and a ship that keeps
     * changing its mind stands still while the meteor arrives.
     */
    uint8_t evade;
    uint8_t evade_left;

    ss_power power;
    uint16_t power_left;

    ss_rock rocks[SS_ROCKS];
    ss_shot shots[SS_SHOTS];
    ss_blast blasts[SS_BLASTS];
    ss_star stars[SS_STARS];
    ss_drop drop;

    uint32_t score;
    uint8_t lives;
    uint16_t frame;

    uint8_t speed;    /* the gear the words per minute put it in, 3 to 5 */
    uint16_t patient; /* frames since anything was hit, breaks stalemates */

    bool redraw; /* the renderer must repaint the whole panel */

    uint32_t rng;
} ss_game;

void ss_init(ss_game *g, uint32_t seed);
void ss_step(ss_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast it turns, thrusts and fires */
void ss_set_speed(ss_game *g, uint8_t gear);

/* helpers shared with the renderer */
bool ss_ship_visible(const ss_game *g);
bool ss_star_lit(const ss_game *g, int i);
/* the word to print for whatever is running, or NULL */
const char *ss_power_name(const ss_game *g);
/* the notice across the middle, or NULL when there is none */
const char *ss_banner(const ss_game *g);
