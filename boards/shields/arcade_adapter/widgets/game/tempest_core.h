/*
 * The well dongle - game core (portable).
 *
 * A Tempest-shaped well: a tube drawn in perspective, sixteen lanes wide, with
 * a claw sliding round its near rim shooting at things climbing up it.  Unlike
 * the other games here there is no world laid out in pixels - everything has a
 * lane and a depth, and the panel position of anything is worked out from those
 * two by tp_at().  That is the whole trick of this game: the same core drives a
 * circle, a square, a cross and two open strips without knowing which it is on,
 * because the shape is sixteen points in a table and the perspective is one
 * division.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/* ------------------------------------------------------------------ */
/* the well                                                            */
/* ------------------------------------------------------------------ */

/*
 * Sixteen lanes, and seventeen points to bound them.  A closed well repeats
 * its first point as its last so that walking the spokes never has to test for
 * the wrap; an open one really does have two ends, and the claw stops at them.
 */
#define TP_SEGS 16

/*
 * Depth runs from 0 at the far rim to TP_DEPTH at the near one, and everything
 * that moves up or down the well is a number in these units.  256 is enough
 * that the slowest thing here still advances by a whole unit a frame, so no
 * subpixel accumulator is needed anywhere.
 */
#define TP_DEPTH 256

/*
 * How far the eye is in front of the near rim, in those same units, which is
 * the only thing that decides how deep the well looks.  A point at depth d is
 * drawn at TP_Z0 / (TP_Z0 + TP_DEPTH - d) of its rim offset, so the far rim
 * comes out at 89/345 - a bit over a quarter size - and the spacing between
 * equal steps of depth crowds together towards the bottom the way a real tube
 * does.  A linear scale instead of this division is the one change that makes
 * the well stop looking like a tunnel.
 */
#define TP_Z0 89

/* the readout's band, and the middle of the well below it */
#define TP_TOP 16
#define TP_CX 120
#define TP_CY 130

/*
 * Eighths of a lane.  The claw slides along the rim rather than snapping from
 * lane to lane, so its position is in these; a lane is TP_SUB of them and the
 * claw is exactly one lane wide, which is what makes "do these two overlap"
 * one subtraction.
 */
#define TP_SUB 8
#define TP_RIM (TP_SEGS * TP_SUB)

/*
 * A well shape: sixteen lanes' worth of near-rim points as offsets from
 * (TP_CX, TP_CY), and the point they all shrink towards as they go down the
 * tube.  A shape only has to be drawn once and in one size, because the far rim
 * is the same points pulled that fraction of the way to the vanishing point.
 *
 * The vanishing point is per shape and not simply the middle of the panel, and
 * that is what makes the open wells work.  A closed one surrounds its own
 * centre, so it vanishes into itself; a strip does not, and shrinking it
 * towards the middle of the panel leaves a band across the bottom third with
 * nothing above it.  Putting its vanishing point above the strip instead gives
 * back the whole panel and a tube that is actually going somewhere.
 */
typedef struct {
    int8_t x[TP_SEGS + 1];
    int8_t y[TP_SEGS + 1];
    int8_t vx, vy;
    uint8_t closed;
} tp_shape;

#define TP_SHAPES 5
extern const tp_shape TP_SHAPE[TP_SHAPES];

/* the fraction of its rim offset a point at depth d is drawn at, in 1/256 */
int tp_persp(int d);

/*
 * Where a point of the well lands on the panel.  `pos8` is in eighths of a
 * lane along the rim - so lane centres are odd multiples of TP_SUB/2 and the
 * spokes are even ones - and `d` is a depth.  Everything drawn here goes
 * through this one function, the claw and the enemies and the shots alike.
 */
void tp_at(const tp_shape *s, int pos8, int d, int *x, int *y);

/* ------------------------------------------------------------------ */
/* what is in the well                                                 */
/* ------------------------------------------------------------------ */

/*
 * Four kinds of thing climb the well, and they are four different problems
 * rather than four different sprites.  A flipper tumbles across the spokes and
 * grabs the claw at the rim; a tanker ignores the claw and breaks into two
 * flippers when it is shot, so shooting one late is worse than shooting it
 * early; a spiker never leaves its lane but builds a spike up it, which is
 * only a problem at the end of the level when the claw has to dive down one;
 * and a pulsar makes its whole lane lethal every few seconds, so the lane
 * rather than the enemy is what has to be avoided.
 */
typedef enum {
    TP_E_GONE = 0,
    TP_E_FLIPPER,
    TP_E_TANKER,
    TP_E_SPIKER,
    TP_E_PULSAR,
    TP_E_KINDS,
} tp_kind;

#define TP_ENEMIES 10

typedef struct {
    uint8_t kind;
    uint8_t lane;
    int16_t d;
    int8_t step;  /* depth units a frame, positive coming up the well */
    uint8_t flip; /* frames left in a tumble across a spoke; 0 when settled */
    int8_t turn;  /* which way that tumble is going */
    uint16_t t;   /* its own clock: when it may fire, or how long since it did */
    uint8_t rim;  /* frames spent at the near rim */
} tp_enemy;

/* the claw's, going down the well */
#define TP_SHOTS 6
/* theirs, coming up it */
#define TP_BOLTS 6
typedef struct {
    uint8_t on;
    uint8_t lane;
    int16_t d;
} tp_shot;

/*
 * How close to the rim something has to be to reach the claw.  A flipper grabs
 * at TP_GRAB and a bolt lands at TP_LAND, and the two differ because a flipper
 * is a body arriving and a bolt is a point - a shared number would make one of
 * them look wrong by a couple of pixels at the moment it kills.
 */
#define TP_GRAB 12
#define TP_LAND 6

/*
 * Frames a tumble across a spoke takes.  It is here rather than beside the
 * other speeds because the renderer needs it too - how far across the spoke an
 * enemy is drawn is what is left of this, and a renderer that guessed would
 * have flippers arriving in a lane they are not in yet.
 */
#define TP_FLIP_T 7

/* how far into the well the claw reaches, which is also how far a spike has to
 * be built before the dive at the end of a level runs into it */
#define TP_CLAW_D 22

/* the tallest a spiker will build, short of the rim so a spiked lane is
 * survivable to sit on and only fatal to dive down */
#define TP_SPIKE_MAX 216

typedef enum {
    TP_D_NONE = 0,
    TP_D_GRABBED,
    TP_D_SHOT,
    TP_D_PULSE,
    TP_D_SPIKE,
    TP_D_CAUSES,
} tp_death;

typedef enum { TP_PLAY = 0, TP_DYING, TP_CLEARED, TP_DIVE, TP_OVER } tp_phase;

typedef struct {
    tp_phase phase;
    uint16_t phase_timer;

    uint8_t shape; /* which of TP_SHAPE the level is on */
    uint8_t level;
    uint32_t score;
    uint8_t lives;

    int16_t pos;  /* the claw along the rim, in eighths of a lane */
    int8_t vpos;  /* and how fast it is sliding */
    int16_t dive; /* its depth while it is diving out of a finished level */
    uint8_t cool; /* frames before it may fire again */
    uint8_t zap;  /* superzappers left this level */
    uint8_t zap_t;

    tp_enemy foe[TP_ENEMIES];
    tp_shot shot[TP_SHOTS];
    tp_shot bolt[TP_BOLTS];
    uint8_t spike[TP_SEGS]; /* the depth each lane's spike has reached, 0 for none */

    uint16_t left; /* enemies this level has still to send */
    uint16_t spawn_t;
    uint16_t pulse; /* the pulsars' shared clock, so they all beat together */
    uint16_t clock;
    uint8_t why;

    int8_t aim; /* the lane the pilot is heading for */
    uint16_t patient;

    uint8_t speed;
    bool redraw;
    uint32_t rng;
} tp_game;

void tp_init(tp_game *g, uint32_t seed);
void tp_step(tp_game *g);
void tp_set_speed(tp_game *g, uint8_t gear);

/* the shape the game is on, for the renderer's benefit */
const tp_shape *tp_well(const tp_game *g);

/* the lane the claw is over, which is the one its shots go down */
int tp_claw_lane(const tp_game *g);
/* how far down the well the claw is: the rim, except during the dive */
int tp_claw_d(const tp_game *g);
bool tp_claw_visible(const tp_game *g);
/* how long the claw has been coming apart, for the renderer to size it */
int tp_spin_age(const tp_game *g);
/* whether a lane is lethal along its whole length this frame */
bool tp_lane_hot(const tp_game *g, int lane);

/* the notice across the middle, or NULL when there is nothing to say */
const char *tp_banner(const tp_game *g);
