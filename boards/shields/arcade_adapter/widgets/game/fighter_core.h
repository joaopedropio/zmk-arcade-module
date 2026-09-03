/*
 * Street Fighter dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The fourth thing the panel can play, and the first one with two of anything.
 * Two fighters on one stage, a health bar apiece and a clock over the top of
 * them; whoever empties the other's bar takes the round, whoever is ahead when
 * the clock runs out takes it instead, and two rounds take the match.  Then
 * both of them are drawn again with different tempers and it starts over.
 *
 * Nobody is holding either stick.  Both sides run the same pilot off different
 * numbers - how close it likes to stand, how readily it guards, how much it
 * wants the big attack - which is what stops a self-playing fight from being
 * the same exchange forty times.  The one thing the pilot is not allowed is
 * perfect reactions: it answers what the other one is *doing*, several frames
 * after it started doing it, so an attack that beats a guard is a thing that
 * happens rather than a thing that cannot.
 *
 * What makes the fight readable at arm's length is that the three attacks and
 * the three answers to them are a ring, and the ring is drawn in the geometry
 * rather than in a table: a punch is chest high, a kick is a sweep, and a
 * fireball travels at chest height.  So crouching goes under a punch and under
 * a fireball and eats the sweep, jumping clears all three and lands committed,
 * and guarding stops everything but costs a chip of health against the
 * fireball.  Nothing in here special-cases any of that - two rectangles either
 * overlap or they do not - which is why the heights below are load-bearing and
 * the reaches are not.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * Positions and speeds are in quarter pixels.  Walking is whole pixels a frame
 * and would not need them; knockback is what does, because a fighter shoved
 * out of a corner has to slow to a stop over a dozen frames and an integer
 * that decays by one is either a shove that never ends or one that stops dead.
 */
#define FG_SUB 4
#define FG_PX(v) ((int)((v) / FG_SUB))

/*
 * The stage.  The readout band is the top of the panel and the floor is far
 * enough below it that a jumping fighter passes in front of neither: a head
 * that goes behind the health bars reads as the bar being drawn over the
 * fighter, which is the one thing a fighting game's screen must never do.
 *
 * FG_WALL is how close to the edge a fighter may be pushed.  It is a fighter's
 * half width and no more, so a corner is a real corner - the whole of the
 * pilot's spacing is worth nothing if there is a pixel of slack behind it.
 */
#define FG_OY    36                 /* the readout band above the stage */
#define FG_FLOOR 206                /* where feet stand, in panel pixels */
#define FG_WALL  (FG_BODY_W / 2 + 2)

/*
 * A fighter's body, which is what an attack has to reach and what it can be
 * hit in.  Standing, crouching and the two heights an attack lands at are all
 * measured up from the floor, because that is what the ring above is made of:
 * FG_HIGH has to sit above FG_CROUCH_H so a crouch goes under it, and FG_LOW
 * has to sit below it so a crouch does not.  Move either and the game stops
 * having three answers.
 */
#define FG_BODY_W   20
#define FG_BODY_H   44
#define FG_CROUCH_H 26

#define FG_HIGH_Y 30 /* the bottom of a punch or a fireball, above the floor */
#define FG_HIGH_H 12
#define FG_LOW_Y  2  /* and of a sweep */
#define FG_LOW_H  14

_Static_assert(FG_HIGH_Y > FG_CROUCH_H, "a crouch has to go under a punch");
_Static_assert(FG_LOW_Y < FG_CROUCH_H, "and has to be caught by a sweep");

/*
 * How far each attack reaches out from the middle of the fighter, and what it
 * costs in frames: what it spends winding up, how long it is dangerous for,
 * and how long it is helpless afterwards.  Those three are the whole balance
 * of the thing - the sweep reaches further and hurts more, and pays for it by
 * leaving the fighter standing there for a third of a second.
 */
#define FG_PUNCH_REACH 30
#define FG_PUNCH_WIND  2
#define FG_PUNCH_HIT   3
#define FG_PUNCH_REST  4

#define FG_KICK_REACH 40
#define FG_KICK_WIND  4
#define FG_KICK_HIT   4
#define FG_KICK_REST  9

#define FG_FIRE_WIND 6 /* the fireball leaves on this frame */
#define FG_FIRE_REST 12
#define FG_FIRE_COOL 60 /* and no second one until this many frames later */

#define FG_HURT   6  /* frames of being unable to answer, after being hit */
#define FG_JUMP_V 36 /* quarter pixels a frame, upwards */
#define FG_FALL   3  /* and what gravity takes back off it each frame */

/*
 * How far off the floor a jump has got after k frames, and how far it gets at
 * all.  The pilot needs the first of those to know how early it has to leave
 * the ground to be above a fireball when the fireball arrives, and the stage
 * needs the second to be sure a jumping fighter stays out of the readout.
 * Both are the arc itself rather than a measured number, so raising the jump
 * cannot leave either of them saying what it used to.
 */
#define FG_RISE(k) (((k) * FG_JUMP_V - FG_FALL * (k) * ((k) - 1) / 2) / FG_SUB)
#define FG_APEX    ((FG_JUMP_V * FG_JUMP_V) / (2 * FG_FALL * FG_SUB))

_Static_assert(FG_FLOOR - FG_BODY_H - FG_APEX > FG_OY, "a jump would go behind the readout");

/*
 * Damage, and the bar it comes off.  96 is the width of a health bar in
 * pixels, so a point of damage is a pixel of bar and nothing has to be scaled
 * to draw it - which also means the bar cannot round to full while a fighter
 * is one hit from losing.
 */
#define FG_HEALTH 96
#define FG_D_PUNCH 4
#define FG_D_KICK  7
#define FG_D_FIRE  10
#define FG_D_CHIP  2 /* what a guarded fireball takes anyway */

/*
 * A round, and the match it belongs to.  Sixty seconds at fifteen frames a
 * second is long enough that a timeout is a verdict on two fighters who would
 * not commit rather than the usual way a round ends, and short enough that one
 * of those is over before anybody watching a dongle stops watching.
 */
#define FG_ROUND_CLOCK 900
#define FG_ROUNDS_WIN  2
/* and the most that can be played, so two fighters who keep drawing still
 * finish: the match goes to whoever is ahead on rounds, or to nobody */
#define FG_ROUNDS_MAX  5

/*
 * How long two fighters may stand and look at each other.  Both pilots hold a
 * preferred distance, and two that prefer the same one will hold it all round:
 * this closes the preference by a pixel every time it expires, so a stand-off
 * is always eventually a fight.  It is reset by anything landing.
 */
#define FG_PATIENCE 30

#define FG_SPARKS 3 /* hit flashes on the panel at once */

/*
 * How close two fighters may stand.  A hair inside the sweep, so that walking
 * all the way in is a way of taking the fireball off the table rather than a
 * way of standing inside somebody: it costs the near fighter the one attack
 * whose recovery it could not survive at that distance.
 */
#define FG_CLINCH 18

/* a fireball, and how fast it crosses the stage */
#define FG_BALL_V 24 /* quarter pixels a frame */
#define FG_BALL_R 5

enum {
    FG_S_IDLE = 0,
    FG_S_WALK,   /* towards the other one */
    FG_S_BACK,
    FG_S_CROUCH,
    FG_S_BLOCK,
    FG_S_JUMP,
    FG_S_PUNCH,
    FG_S_KICK,
    FG_S_FIRE,
    FG_S_HURT,
    FG_S_DOWN, /* out of health, on the floor until the round resets */
    FG_S_WIN,
    FG_STATES,
};

typedef enum {
    FG_READY = 0, /* "ROUND n", both fighters held still */
    FG_FIGHT,
    FG_KO,   /* somebody is down, or the clock went; the stage carries on */
    FG_OVER, /* the match is decided, about to start another */
} fg_phase;

/*
 * A fighter, and the three numbers that make it play differently from the one
 * opposite.  They are drawn once a match rather than once a round, so a match
 * has a character to it: a nervy long-range fighter against a guard-happy one
 * is a different three rounds from two brawlers, and both are worth the look.
 */
typedef struct {
    int16_t x;   /* quarter pixels, the middle of the fighter */
    int16_t h;   /* quarter pixels above the floor; 0 is standing on it */
    int16_t vy;  /* quarter pixels a frame, negative going up */
    int16_t push; /* knockback still to be spent, quarter pixels a frame */

    uint8_t state;
    uint8_t timer;  /* frames left in the state */
    uint8_t face;   /* 0 facing left, 1 facing right */
    uint8_t health;
    uint8_t stride; /* walking phase, so feet alternate */
    int8_t air;     /* quarter pixels a frame carried through a jump, so that
                     * jumping in is a way of closing and not only of dodging */
    uint8_t cool;   /* frames until another fireball */
    uint8_t poise;  /* frames until another attack of any kind */
    bool spent;     /* this swing has already landed, and cannot land twice */

    uint8_t nerve;  /* the distance it likes to hold, in pixels */
    uint8_t guard;  /* how much of the time it answers an attack by blocking */
    uint8_t temper; /* how much it prefers the sweep and the fireball */
} fg_fighter;

/* one each, so a fighter cannot bury the other under a wall of them */
typedef struct {
    int16_t x, y; /* quarter pixels; y is the middle of the ball */
    int8_t vx;
    uint8_t owner;
    bool live;
} fg_ball;

typedef struct {
    int16_t x, y;
    uint8_t age; /* counting down; 0 is not on the panel */
    bool guarded; /* a block sparks differently from a clean hit */
} fg_spark;

/*
 * What the last fg_step() would have made a noise about.  Nothing is
 * listening - every game on this dongle is silent - but the core says it
 * anyway, so the simulator can count exchanges without reaching into the
 * fighters.
 */
enum {
    FG_SFX_HIT = 1u << 0,
    FG_SFX_BLOCK = 1u << 1,
    FG_SFX_FIRE = 1u << 2,
    FG_SFX_KO = 1u << 3,
    FG_SFX_ROUND = 1u << 4, /* a round started */
    FG_SFX_MATCH = 1u << 5, /* and a match was decided */
};

/* how the last round ended, which is what a soak is watching */
enum {
    FG_E_NONE = 0,
    FG_E_KO,
    FG_E_TIME,
    FG_E_ENDS,
};

typedef struct {
    fg_fighter f[2];
    fg_ball ball[2];
    fg_spark sparks[FG_SPARKS];

    fg_phase phase;
    uint16_t phase_timer;

    uint16_t clock; /* frames left in the round */
    uint16_t idle;  /* frames since anything landed, which closes the distance */
    uint8_t squeeze; /* how much FG_PATIENCE has taken off both preferences */

    uint8_t round;   /* 1 upwards, within the match */
    uint8_t wins[2];
    uint8_t bar[2];  /* the trailing bar, catching up to the health under it */
    uint8_t winner;  /* 0, 1, or 2 for a draw; read in FG_KO and FG_OVER */
    uint8_t ended;   /* an FG_E_*, how the last round finished */
    uint32_t bouts;  /* matches finished, which is the only running total */

    uint8_t speed; /* the gear the words per minute put it in, 3 to 5 */
    uint16_t frame;

    uint8_t sfx;
    bool redraw;
    bool flash; /* the stage flashes on a knockout */

    uint32_t rng;
} fg_game;

void fg_init(fg_game *g, uint32_t seed);
void fg_step(fg_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast a fighter walks */
void fg_set_speed(fg_game *g, uint8_t gear);

/* helpers shared with the renderer */
/* how tall the fighter is standing right now, in pixels: a crouch is shorter */
int fg_height(const fg_fighter *f);
/* the reach of the limb that is out this frame, 0 for none; `high` says which
 * of the two heights it is at, which is the whole of what a crouch answers */
int fg_swing(const fg_fighter *f, int *high);
/* the notice across the middle of the stage, or NULL when there is none */
const char *fg_banner(const fg_game *g);
