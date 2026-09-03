/*
 * Metal Slug dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The fifth thing the panel can play, and the only one where the world is
 * bigger than the screen.  A trooper runs right along a ridge that never ends,
 * shooting what comes the other way, lobbing a grenade at whatever is standing
 * on a ledge its rifle cannot reach, and jumping the holes.  One hit is one
 * life, which is what this kind of game has always cost.
 *
 * The ground is generated a chunk at a time as the camera reaches it and
 * thrown away behind, so there is no level and nothing to run out of - and
 * three rules keep what is generated playable rather than merely random: a
 * hole is never next to a hole, the ground either side of a hole is the same
 * height, and no two chunks differ by more than one step.  Together those mean
 * every gap can be jumped and every wall can be climbed, so the pilot never
 * has to deal with terrain it cannot pass and the run only ever ends because
 * something shot it.
 *
 * The trooper holds one screen column and the world moves instead.  That is
 * not a drawing convenience - it is what makes the game cheap enough to run on
 * this panel at all, and commando_render.h explains why.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * Heights are quarter pixels, the same as the ring's, and for the same reason:
 * a jump has to come down in a curve rather than in four equal steps, and an
 * integer that loses one a frame is either a hop or a moon landing.  Distances
 * along the world stay whole pixels - the world is thousands of them long, and
 * a quarter of a pixel of scroll is not a thing anybody could see.
 */
#define CM_SUB 4
#define CM_PX(v) ((int)((v) / CM_SUB))

/*
 * The band the readout is written on, and the ridge below it.  Three heights
 * of ground a step apart, the lowest of them far enough up the panel to leave
 * a hole worth falling into underneath.  The horizon is where the hills behind
 * stop; it sits above the highest ground so that the two layers never overlap,
 * which is what lets each of them be redrawn without the other.
 */
#define CM_OY      28
#define CM_BASE    190 /* the lowest ground there is */
#define CM_STEP    24
#define CM_LEVELS  3
#define CM_HORIZON 108

_Static_assert(CM_BASE - (CM_LEVELS - 1) * CM_STEP > CM_HORIZON,
               "the ground would climb into the hills");

/*
 * The world in chunks, and how many of them are kept.  A chunk is two of the
 * trooper's own widths, which is the coarsest a hole can be and still be worth
 * jumping rather than stepping over; the span is three screens, which is the
 * screen itself plus the lookahead the pilot needs to decide about a hole
 * before it is on top of it, plus a screen of slack so nothing is generated in
 * front of the camera.
 */
#define CM_CHUNK 32
#define CM_SPAN  24
#define CM_PIT   255 /* what stands in for a chunk's level where there is none */

_Static_assert(CM_SPAN * CM_CHUNK > 3 * ARC_PANEL, "the lookahead is shorter than a screen");

/* where the trooper stands on the panel, and how big it is */
#define CM_HERO_X 72
#define CM_BODY_W 14
#define CM_BODY_H 26

/*
 * The jump, and what it has to be able to do: clear a chunk-wide hole at a
 * run, and get over a step.  Both are checked below out of the arc itself
 * rather than taken on trust, so raising the ground or widening a chunk fails
 * the build instead of quietly making a hole that cannot be crossed.
 */
#define CM_JUMP_V 40
#define CM_FALL   4
#define CM_RISE(k) (((k) * CM_JUMP_V - CM_FALL * (k) * ((k) - 1) / 2) / CM_SUB)
#define CM_HANG    (2 * CM_JUMP_V / CM_FALL) /* frames from the ground and back */

/* how fast it runs in each of the three gears the words per minute set */
#define CM_RUN(gear) ((gear) - 1)


_Static_assert(CM_RISE(CM_HANG / 2) > CM_STEP + 8, "a jump has to clear a step");
_Static_assert(CM_HANG * CM_RUN(3) > CM_CHUNK, "and clear a hole in the slowest gear");

/* how much of a step up can be walked up rather than jumped over */
#define CM_CLIMB 6

/*
 * What is on the ridge.  The counts are what the panel can carry rather than
 * what the game wants: every one of these is a rectangle that moves with the
 * scroll and so is repainted on every frame it is on screen, and four enemies
 * and six bullets is already most of what a frame can afford.
 */
#define CM_FOES   4
#define CM_SHOTS  6
#define CM_NADES  2
#define CM_BOOMS  3
#define CM_CRATES 2

/*
 * Two enemies, and the difference is what they do about the ground.  A rifle
 * shot travels flat, so anything standing on a higher ledge cannot be shot at
 * all - that is what the grenades are for, and what makes a gunner up on a
 * step a problem to be solved rather than another thing to walk into.
 */
enum {
    CM_F_GRUNT = 0, /* comes at the trooper along the ground */
    CM_F_GUNNER,    /* holds its ground and fires more often */
    CM_F_KINDS,
};

#define CM_GUNNER_HP 2

typedef enum {
    CM_READY = 0, /* a moment to read the panel before it starts moving */
    CM_RUNNING,
    CM_DOWN, /* the trooper is going up; the ridge carries on */
    CM_OVER, /* out of lives, about to start again */
} cm_phase;

/* how the last life went, which is the only number a soak can act on */
enum {
    CM_D_NONE = 0,
    CM_D_SHOT,
    CM_D_TOUCHED,
    CM_D_FELL,
    CM_D_CAUSES,
};

/*
 * Nothing but the trooper ever leaves the ground, so an enemy is only a place
 * along the world and what it is: where it stands follows from the ridge under
 * it, and one that walks into a wall or up to a hole simply stops and shoots
 * from there.  That is a choice rather than a saving - an enemy that jumped
 * would need the arc, the landing check and a reason, and what it would add to
 * a panel this size is one more thing moving.
 */
typedef struct {
    int32_t wx;   /* world pixels; the middle of the actor */
    uint8_t kind;
    uint8_t hp;
    uint8_t cool; /* frames until it can fire again */
    uint8_t step; /* walking phase, for the legs */
    bool alive;
} cm_foe;

typedef struct {
    int32_t wx;
    int16_t y;  /* panel pixels; a shot travels flat, so this never changes */
    int8_t vx;  /* whole pixels a frame */
    bool mine;  /* the trooper's, or something shooting back */
    bool live;
} cm_shot;

typedef struct {
    int32_t wx;
    int16_t y, vy; /* quarter pixels: a grenade is the one thing that arcs */
    int8_t vx;
    bool live;
} cm_nade;

typedef struct {
    int32_t wx;
    int16_t y;
    uint8_t age; /* counting down; 0 is not on the panel */
    bool big;    /* a grenade's, rather than something small being hit */
} cm_boom;

typedef struct {
    int32_t wx;
    uint8_t level;
    bool live;
} cm_crate;

/*
 * What the last cm_step() would have made a noise about.  Nothing is
 * listening - every game on this dongle is silent - but the core says it
 * anyway, so the simulator can count what happened without walking the ridge.
 */
enum {
    CM_SFX_SHOT = 1u << 0,
    CM_SFX_NADE = 1u << 1,
    CM_SFX_KILL = 1u << 2,
    CM_SFX_PICKUP = 1u << 3,
    CM_SFX_DEATH = 1u << 4,
    CM_SFX_JUMP = 1u << 5,
};

typedef struct {
    /*
     * The ridge, as a ring of chunk levels.  ground[k % CM_SPAN] is the level
     * of chunk k for every k the camera can still see or is about to, and
     * `chunk0` is the oldest of those - so generating the world is writing one
     * byte and forgetting one, and the whole of it costs twenty-four.
     */
    uint8_t ground[CM_SPAN];
    uint32_t chunk0;
    uint8_t last_level; /* what the generator left off at */
    uint8_t hold_level; /* and what to come back to on the far side of a hole */
    uint8_t since_pit;  /* chunks since the last hole, so they stay apart */

    int32_t scroll; /* world x of the left edge of the panel */

    /*
     * The trooper, as where its feet are on the panel rather than as a height
     * above whatever it is standing on.  Ground that changes height under an
     * actor makes the second of those a running argument about which ground it
     * meant; an absolute y is landed on, walked off and fallen through with
     * one comparison apiece.
     */
    int16_t hero_y;  /* quarter pixels down the panel: the feet */
    int16_t hero_vy; /* quarter pixels a frame, downwards */
    uint8_t hero_step;
    bool airborne;
    uint8_t reload;
    uint8_t invuln; /* frames of blinking after a life is lost */
    uint8_t nades;  /* grenades in hand */

    cm_foe foes[CM_FOES];
    cm_shot shots[CM_SHOTS];
    cm_nade bombs[CM_NADES];
    cm_boom booms[CM_BOOMS];
    cm_crate crates[CM_CRATES];

    cm_phase phase;
    uint16_t phase_timer;

    uint32_t score;
    uint8_t lives;
    uint8_t cause; /* a CM_D_*, what took the last life */

    uint8_t speed; /* the gear the words per minute put it in, 3 to 5 */
    uint16_t frame;

    uint8_t sfx;
    bool redraw;

    uint32_t rng;
} cm_game;

void cm_init(cm_game *g, uint32_t seed);
void cm_step(cm_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast the trooper runs */
void cm_set_speed(cm_game *g, uint8_t gear);

/* helpers shared with the renderer */
/* the top of the ground at a world x, or CM_PIT where there is none */
int cm_surface(const cm_game *g, int32_t wx);
/* the skyline behind it, which scrolls at a quarter of the speed */
int cm_hill(const cm_game *g, int screen_x);
/* true on the frame the rifle went off, so the renderer can put a flash on
 * the muzzle without knowing what the reload happens to be */
bool cm_firing(const cm_game *g);
/* where an enemy's feet are on the panel; it stands on whatever is under it */
int cm_foe_y(const cm_game *g, const cm_foe *f);
/* false while the trooper is blinking, so a life just lost is visible */
bool cm_hero_visible(const cm_game *g);
/* the notice across the middle of the ridge, or NULL when there is none */
const char *cm_banner(const cm_game *g);
