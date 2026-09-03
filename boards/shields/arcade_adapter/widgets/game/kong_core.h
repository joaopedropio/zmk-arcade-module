/*
 * Girders dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The seventh thing the panel can show: the 1981 climb up a building site,
 * where an ape at the top left rolls barrels down a stack of sloped girders
 * and the climber has to get past them to the lady at the far end of the top
 * one.  Ladders are the only way up, barrels are the only way to die, and the
 * one thing that can be done about a barrel is to be in the air when it
 * arrives.
 *
 * It is the maze and the ridge at once.  The board is fixed, as the maze's is
 * - the same six girders and the same ten ladders every level, so a route
 * through it can be reasoned about rather than searched for - but nothing on
 * it is on a lattice: the girders slope, so how high the ground is depends on
 * where along it you are standing, and a barrel rolls downhill because that is
 * which way the girder tilts.  Every vertical position in here is therefore
 * derived from dk_floor_y() and never stored, which is the one rule that keeps
 * the climber's feet on the beam.
 *
 * Nobody plays it.  The pilot has one goal - the lady - and two questions
 * between it and her: which ladder to take up, and whether the next barrel has
 * to be jumped.  The first is priced like the crossing prices a cell; the
 * second is proved by winding the jump forward over the barrels, the way the
 * ridge proves a gap, because a jump taken on a guess is a jump that lands on
 * the barrel it was meant to clear.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * Subpixels, as everywhere else that something moves less than a pixel a
 * frame.  A barrel crosses the panel in about four seconds and the climber in
 * six, which at fifteen frames a second is four pixels a frame against two and
 * a half - and the difference between those two is the whole game, so neither
 * may be rounded to the same number.
 */
#define DK_SUB 8
#define DK_PX(v) ((int)((v) / DK_SUB))

/*
 * The site.  Six girders, the lowest at DK_BASE and each DK_RISE above the
 * one below, under a readout band the height of one row of letters and a
 * little air.  DK_RISE is the load-bearing one: it has to clear the climber's
 * head (DK_HERO_H) with enough left over that a barrel on the girder above is
 * plainly on a different floor, and it has to be short enough that six of them
 * and the band fit on the panel with the drum standing on the bottom one.
 */
#define DK_TOP 16
#define DK_FLOORS 6
#define DK_BASE 232
#define DK_RISE 35
#define DK_BEAM 4 /* how thick a girder is drawn, below its surface */

/*
 * How far a girder falls from one end to the other.  This is what makes a
 * barrel roll at all - it has no engine, it goes the way its girder tilts -
 * and the sign alternates floor by floor, which is what turns a stack of
 * ramps into the zigzag a barrel actually takes down the screen.  Eight
 * pixels over the panel is about a twenty-ninth, shallow enough that the
 * climber's feet never look wrong on it and steep enough to read as a slope.
 */
#define DK_DROP 8

/* which way a girder tilts, and so which way its barrels roll */
#define DK_DIR(f) (((f) & 1) ? 1 : -1)

/* the surface of girder f under x (panel pixels, both) */
int dk_floor_y(int f, int x);

/*
 * The ladders.  Nine of them, two or three to a gap, at columns chosen so
 * that no two consecutive gaps are climbed at the same end of the panel -
 * the climb is meant to be a zigzag across the site rather than a straight
 * line up one side, and a straight line up one side is what a pilot picks
 * when it is offered.
 *
 * Two are broken, which the climber may not use and a barrel may.  They are
 * here because a board where every ladder works is a board with one route,
 * and a pilot that only ever walks that route is one nobody has to think
 * about: with these, the way up from the second gap depends on which end of
 * the girder the climber arrived at.
 */
typedef struct {
    uint8_t gap;    /* joins girder gap and girder gap + 1 */
    uint8_t x;      /* the middle of it, in panel pixels */
    uint8_t broken; /* barrels only */
} dk_ladder;

#define DK_LADDERS 12
extern const dk_ladder DK_LADDER[DK_LADDERS];

/* how wide a ladder is, and how much of one has to be underfoot to mount it */
#define DK_LADDER_W 12
#define DK_MOUNT 4

/*
 * How far before a ladder a barrel makes up its mind about going down it.
 * The arcade's barrels wobble at the top of a ladder before they take it, and
 * this is that wobble written down: without it the decision happens at the
 * ladder itself, and a barrel appearing at the foot of the one the climber is
 * standing at - or halfway up - is a death he had no way of seeing coming.
 * It cost more lives than anything else here.  Four frames of warning is
 * enough to walk out from under it, and DK_TELL is that, at the speed a
 * barrel rolls.
 */
#define DK_TELL 14

/*
 * The climber.  Ten by fourteen, which is two pixels narrower than the
 * ladders he climbs and a little over a third of the gap between girders.
 * The hit box is narrower still: most of the width is arms, and a game that
 * ends because a barrel clipped a sleeve reads as unfair from across a room.
 */
#define DK_HERO_W 10
#define DK_HERO_H 14
#define DK_HIT 4

/* the barrel: how big it is drawn, and how much of it actually catches */
#define DK_BARREL_W 12
#define DK_BARREL_H 10
#define DK_BARREL_HIT 5

/*
 * The jump.  Twelve frames of air and fourteen pixels of it at the top, and
 * it is not a parabola: it rises in three frames, hangs, and comes down.
 *
 * The shape is the whole point.  What a jump has to do here is be higher than
 * a barrel for longer than the barrel takes to pass underneath, and the two
 * are not close.  A barrel closes on a standing climber at three pixels a
 * frame and the pair of them overlap for about five and a half; a barrel
 * overtaking him from behind while he walks away closes at one and a quarter
 * and takes fourteen.  A parabola of this height is over a barrel for seven
 * frames of its twelve, which leaves a frame and a half in which the jump may
 * be started and is the same thing as not being able to jump at all - and a
 * parabola tall enough to hang for ten would put his head through the girder
 * above.  So it hangs instead.  dk_jump_dy() is the whole of the physics.
 */
#define DK_JUMP_T 12
#define DK_JUMP_UP 14

/* how far above the girder the climber is, t frames into a jump */
int dk_jump_dy(int t);

_Static_assert(DK_JUMP_UP > DK_BARREL_H, "a jump does not clear a barrel");
_Static_assert(DK_RISE > DK_HERO_H + DK_JUMP_UP, "a jump reaches the girder above");

/*
 * The hammer.  Two of them hang over the site, and picking one up buys four
 * seconds in which barrels are broken rather than fatal - and in which no
 * ladder may be climbed, which is the trade the arcade made and the reason
 * this is a decision rather than a free bonus.
 *
 * Both halves of that were measured.  At eight seconds, and fetched whenever
 * there were two barrels about, the hammers were costing a third of every
 * climb: the pilot walked to one, and then had to spend the whole of it on a
 * girder it was not allowed to leave - on the busiest girder of the six,
 * because that is where the barrels that sent it after the hammer were.  It is
 * now only picked up when it is already on the way, which is what a player
 * does with it.
 */
#define DK_HAMMERS 2
#define DK_HAMMER_T 60
#define DK_SWING 6 /* frames per swing, for the drawing and the reach */

typedef struct {
    uint8_t floor;
    uint8_t x;
} dk_pickup;

extern const dk_pickup DK_HAMMER[DK_HAMMERS];

/* how far above the girder a hammer hangs, and how close is close enough */
#define DK_HAMMER_UP 16
#define DK_HAMMER_GRAB 6

/*
 * Barrels.  Eight is the most the ape can have in flight at once, which at the
 * throw rates below is never all of them on one girder - the board is meant to
 * be crossable, and a girder with four barrels abreast on it is not.
 */
#define DK_BARRELS 8

/* what a barrel is doing.  The two transitions are timed rather than moved:
 * both end somewhere known, and interpolating to it is what keeps a barrel
 * from arriving half a pixel off the girder it is landing on */
typedef enum {
    DK_B_GONE = 0,
    DK_B_ROLL,   /* along a girder, the way it tilts */
    DK_B_LADDER, /* down a ladder, at a fixed column */
    DK_B_DROP,   /* off the low end of a girder, onto the one below */
} dk_bstate;

typedef struct {
    int16_t x;      /* eighths; the middle of it */
    int16_t y;      /* eighths; the girder surface it is riding */
    uint8_t floor;  /* the girder it is on, or the one it is leaving */
    uint8_t state;  /* a dk_bstate */
    int8_t dir;     /* which way it is rolling */
    uint8_t t, n;   /* frames into a transition, and how long it takes */
    int16_t y0, y1; /* the surfaces that transition runs between, in eighths */
    uint8_t to;     /* the girder it lands on */
    uint8_t takes;  /* the ladder it has decided to go down, plus one */
} dk_barrel;

/* what the climber is doing */
typedef enum {
    DK_ST_WALK = 0,
    DK_ST_CLIMB,
    DK_ST_JUMP,
} dk_state;

typedef struct {
    int16_t x, y;    /* eighths; x is the middle of him and y is his feet */
    uint8_t floor;   /* the girder he is on, or the one a climb started from */
    uint8_t state;   /* a dk_state */
    int8_t facing;   /* -1 or 1, for the drawing */
    int8_t vx;       /* eighths a frame, while he is in the air */
    uint8_t t;       /* frames into the jump or the climb */
    uint8_t n;       /* how long that climb takes */
    int16_t y0, y1;  /* the surfaces it runs between, in eighths */
    uint8_t to;      /* the girder it ends on */
    uint16_t hammer; /* frames of hammer left, 0 when he has not got one */
    uint8_t step;    /* how far he has walked, for the legs */
    uint8_t cleared; /* the barrels this jump has already been paid for */
} dk_hero;

/*
 * What killed him, kept for the same reason the crossing keeps it: a soak
 * that only counts deaths cannot tell a climber that mistimed a jump from one
 * that stood at the foot of a ladder until the clock ran out, and those two
 * want opposite fixes.  The renderer never reads it.
 */
typedef enum {
    DK_D_NONE = 0,
    DK_D_BARREL, /* rolled over, or landed on */
    DK_D_TIME,
    DK_D_CAUSES,
} dk_death;

typedef enum {
    DK_PLAY = 0,
    DK_DYING, /* he is spinning; the barrels have stopped */
    DK_WON,   /* he reached her, and the hearts are up */
    DK_OVER,  /* out of lives, about to start again */
} dk_phase;

/*
 * Forty seconds a climb at fifteen frames a second.  The arcade counts a
 * bonus down instead and kills at zero, which is the same thing said the
 * other way round, and the number is chosen the same way the crossing's was:
 * long enough that a climber who waits for the gaps it wants gets there, short
 * enough that a board which has closed up is broken rather than watched.
 */
#define DK_TIME 600

typedef struct {
    dk_phase phase;
    uint16_t phase_timer;

    dk_hero hero;
    dk_barrel barrel[DK_BARRELS];
    bool hammer_up[DK_HAMMERS]; /* which are still hanging there */

    uint16_t throw_t; /* frames until the ape lets the next one go */
    uint8_t ape;      /* frames into a throw, for the drawing */

    uint16_t clock; /* frames left on this life */
    uint8_t reached;/* the highest girder this climb, for the ten a girder */
    uint8_t why;    /* a dk_death: what ended the last life */

    uint32_t score;
    uint8_t lives;
    uint8_t level;

    /* the ladder the pilot is walking to, or -1 when it is going to her */
    int8_t aim;
    uint8_t fetch;    /* the hammer it has decided to detour for, plus one */
    uint16_t patient; /* frames since it last got anywhere */

    uint8_t speed; /* the gear the words per minute put it in, 3 to 5 */

    bool redraw; /* the renderer must repaint the whole panel */

    uint32_t rng;
} dk_game;

void dk_init(dk_game *g, uint32_t seed);
void dk_step(dk_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast it all rolls and climbs */
void dk_set_speed(dk_game *g, uint8_t gear);

/* ------------------------------------------------------------------ */
/* what the renderer asks                                              */
/* ------------------------------------------------------------------ */

/*
 * Where the ape stands, and where she waits: both on the top girder.  She is
 * not at the far end of it, which would be the picture everybody has of this
 * game, because the top girder tilts to the right and the last ladder arrives
 * at the right of it - so the far end is a hundred pixels of walking with the
 * barrels behind him, and the two ladders would have to be a bad one and a
 * worse one.  Where she is, either of them is a short walk against the tilt.
 */
#define DK_APE_X 32
#define DK_LADY_X 132
#define DK_APE_W 34
#define DK_APE_H 28
#define DK_LADY_W 12
#define DK_LADY_H 15

/* how close to her counts as having got there */
#define DK_REACH 8

/* the drum the barrels end up in, at the low end of the bottom girder */
#define DK_DRUM_X 14
#define DK_DRUM_W 20
#define DK_DRUM_H 18

/* how many frames into the death spin, or -1 when he is not in one */
int dk_spin_age(const dk_game *g);
/* whether the climber is drawn at all - he blinks while the spin plays */
bool dk_hero_visible(const dk_game *g);
/* the notice across the middle, or NULL when there is none */
const char *dk_banner(const dk_game *g);
