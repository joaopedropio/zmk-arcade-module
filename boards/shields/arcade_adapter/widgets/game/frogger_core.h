/*
 * Crossing dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The third thing the panel can show, and the oldest: the 1981 arcade game
 * where a frog crosses a road and then a river, five lanes of each, and the
 * round is over when all five bays at the top have a frog in them.  The road
 * kills by touching; the river kills by not touching, since the only way over
 * it is to ride the logs and the turtles that drift across it.
 *
 * Where the maze is a grid and the shooter is free flight, this is both at
 * once.  The frog lives on a lattice - it hops a whole cell at a time and
 * every lane is one cell tall - but what it hops onto is sliding underneath
 * it, so its position along a row is a subpixel number like the shooter's and
 * only its row is ever exact.  That split is the thing to keep straight while
 * reading this: rows are counted, columns are measured.
 *
 * Nobody plays it.  The pilot in here looks a fixed number of frames ahead,
 * asks of every cell it could hop to whether the traffic will be off it and
 * whether something will still be floating under it, and takes the best of
 * them - which is usually forwards and is sometimes waiting.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * The board.  Thirteen rows of sixteen pixels is the arcade's own layout -
 * home, five of river, the median, five of road, the bank you start on - and
 * on a 240 pixel panel it leaves exactly two sixteen pixel bands over, one at
 * each end, which is where the score and the clock go.  Fifteen columns fall
 * out of the same number and are what the five home bays are spaced on.
 */
#define FR_CELL 16
#define FR_COLS (ARC_PANEL / FR_CELL)
#define FR_ROWS 13
#define FR_TOP  FR_CELL                              /* first row of the board */
#define FR_FOOT (FR_TOP + FR_ROWS * FR_CELL)         /* and the clock band below it */

_Static_assert(FR_FOOT + FR_CELL == ARC_PANEL, "the board and its two bands are not the panel");

/* which row is what.  Everything between the two banks is lethal */
#define FR_ROW_HOME   0
#define FR_ROW_RIVER  1                              /* .. FR_ROW_MEDIAN - 1 */
#define FR_ROW_MEDIAN 6
#define FR_ROW_ROAD   7                              /* .. FR_ROW_START - 1 */
#define FR_ROW_START  12

/*
 * The five bays, three columns apart with a block of hedge between each pair.
 * Three is what makes them fit: five bays and four gaps at one column apiece
 * is fifteen columns exactly, and any wider spacing would drop a bay.
 */
#define FR_BAYS 5
#define FR_BAY_COL(i) (1 + 3 * (i))

/*
 * Subpixels, as in the shooter and for the same reason: a log crossing the
 * panel in ten seconds moves a pixel and a half a frame, and rounding that
 * either way would make every lane the same speed or no speed at all.
 */
#define FR_SUB 8
#define FR_PX(v) ((int)((v) / FR_SUB))

/*
 * Traffic runs on a loop rather than being spawned and forgotten.  Everything
 * in a lane moves at one speed, so the gaps between them never change: they
 * are set once at the start of a level and then the whole lane slides, wrapping
 * round a track FR_LOOP long of which the panel is the middle.  FR_RUNOFF is the
 * run-off at each end, and it has to be at least the longest sprite or one
 * would reappear at the far edge while its tail was still on screen.
 *
 * What this buys is that a lane is always as full as it was drawn to be, and
 * that a gap the frog is waiting for is a gap that actually arrives.
 */
#define FR_LOOP 288
#define FR_RUNOFF 48
#define FR_LANE_MAX 4                                /* movers in any one lane */

/* what a lane carries.  The first three kill on contact; the last two float */
typedef enum {
    FR_K_NONE = 0,
    FR_K_CAR,
    FR_K_TRUCK,
    FR_K_RACER,
    FR_K_LOG,
    FR_K_TURTLE,
} fr_kind;

/* how wide each of them is drawn, in pixels */
#define FR_W_CAR   24
#define FR_W_TRUCK 34
#define FR_W_RACER 20
#define FR_W_TURT  16                                /* one turtle of a group */

_Static_assert(3 * FR_W_TURT <= FR_RUNOFF, "a three turtle raft is longer than the run-off");
_Static_assert(FR_W_TRUCK <= FR_RUNOFF, "a truck is longer than the run-off");

/*
 * How tall anything in a lane is drawn.  Six pixels short of the cell, so
 * there is a strip of road or water above and below every sprite: without it
 * a lane of cars reads as a solid band and the frog appears to hop from
 * bumper to bumper rather than between them.
 */
#define FR_SPRITE_H 10

/*
 * Turtles go under.  A raft dives on its own cycle - down for FR_DIVE_DOWN
 * frames out of FR_DIVE_CYCLE - and a frog standing on one when it goes is a
 * drowned frog, which is the one hazard in this game that cannot be seen
 * coming from the shape of the board.  It can be seen coming from the raft,
 * though: the last FR_DIVE_WARN frames before it goes are drawn sinking.
 */
#define FR_DIVE_CYCLE 150
#define FR_DIVE_DOWN  20
#define FR_DIVE_WARN  16

typedef struct {
    int16_t speed;                  /* eighths a frame; negative runs left */
    uint8_t kind;                   /* an fr_kind; FR_K_NONE for a safe row */
    uint8_t count;
    uint8_t dives;                  /* whether this lane's turtles go under */
    uint16_t pos[FR_LANE_MAX];      /* along the loop, in eighths */
    uint8_t span[FR_LANE_MAX];      /* how wide each one is, in pixels */
    uint8_t phase[FR_LANE_MAX];     /* its place in the dive cycle, and its grain */
} fr_lane;

/*
 * Forty seconds a trip at fifteen frames a second.  The arcade gives thirty,
 * which is generous for somebody who can see a gap and take it; this pilot
 * waits for gaps it is sure of, and on thirty it ran out of time more often
 * than it was run over.  The clock is not there to be beaten but to break the
 * case where every lane the frog wants is busy at once: it runs out, the frog
 * starts again from the bank, and the board it faces is a different one
 * because the lanes have moved on.  The renderer scales the bar against it.
 */
#define FR_TIME 600

/*
 * What killed it, kept because a soak that only counts deaths cannot tell a
 * frog that misjudged a lane from one that was carried off the panel, and
 * those two want opposite fixes.  The renderer never reads it.
 */
typedef enum {
    FR_D_NONE = 0,
    FR_D_CAR,    /* run over */
    FR_D_WATER,  /* landed on the river */
    FR_D_SUNK,   /* the raft under it went down */
    FR_D_EDGE,   /* carried off the side of the panel */
    FR_D_HEDGE,  /* hopped at the hedge, or at a full bay */
    FR_D_TIME,
} fr_death;

typedef enum {
    FR_PLAY = 0,
    FR_DYING,   /* the frog went; the splat is still on the board */
    FR_HOMED,   /* it made a bay, and is about to start again from the bank */
    FR_LEVEL,   /* all five are full; the bays are flashing */
    FR_OVER,    /* out of lives, about to start again */
} fr_phase;

/*
 * How big the frog is drawn, and how much of it a car actually has to reach.
 * Most of the sprite is legs, and a game that ends because a bumper caught a
 * toe reads as unfair from across a room - so the hit box is the middle of it.
 * The drawn width is here rather than in the renderer because it is a rule as
 * well as a size: the frog may not be carried off the side of the panel, and
 * where the panel ends for it is where its sprite ends.
 */
#define FR_FROG_W 12
#define FR_HIT    5

/*
 * The frog.  Its row is exact and its column is not: on the banks and the road
 * it lands snapped to a cell, and in the river it lands wherever the log it
 * caught happens to be and then travels with it.
 */
typedef struct {
    int16_t x, y;    /* centre, in eighths */
    int16_t tx, ty;  /* where the hop being made ends */
    uint8_t row;     /* the row it is on, or the one it is hopping to */
    uint8_t hop;     /* frames left of that hop, 0 when it is sitting */
    uint8_t facing;  /* 0 up, 1 right, 2 down, 3 left - for the drawing only */
} fr_frog;

typedef struct {
    fr_phase phase;
    uint16_t phase_timer;

    fr_frog frog;
    fr_lane lanes[FR_ROWS];

    bool bay[FR_BAYS];      /* which of the five are full */
    int8_t fly;             /* the bay a fly is sitting in, or -1 */
    uint16_t fly_left;

    uint16_t clock;         /* frames left on this life */
    uint8_t dive_t;         /* where the turtles are in their cycle */
    uint8_t reached;        /* the furthest row this trip, for the ten a row */
    uint8_t why;            /* an fr_death: what ended the last life */

    uint32_t score;
    uint8_t lives;
    uint8_t level;

    /*
     * The bay this trip is aimed at.  Picked on the median and held all the
     * way across, because the only way to line up on a bay sixteen pixels wide
     * is to spend the whole river drifting towards it - a frog that changed
     * its mind halfway would arrive between two of them every time.
     */
    int8_t aim;
    uint8_t think;          /* frames before it may hop again */
    uint16_t patient;       /* frames since it last got anywhere */

    uint8_t speed;          /* the gear the words per minute put it in, 3 to 5 */

    bool redraw;            /* the renderer must repaint the whole panel */

    uint32_t rng;
} fr_game;

void fr_init(fr_game *g, uint32_t seed);
void fr_step(fr_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast the traffic runs and it thinks */
void fr_set_speed(fr_game *g, uint8_t gear);

/* ------------------------------------------------------------------ */
/* what the renderer and the pilot both ask                            */
/* ------------------------------------------------------------------ */

/* the top of a row, in panel pixels */
#define FR_ROW_Y(row) (FR_TOP + (row) * FR_CELL)

/* where mover i of a lane will be t frames from now: its left edge, in pixels */
int fr_mover_x(const fr_game *g, const fr_lane *l, int i, int t);
/* how many frames into the splat, or -1 when the frog is not in one */
int fr_splat_age(const fr_game *g);
/* how far under a diving raft is, 0 when it is up and FR_DIVE_WARN when gone */
int fr_turtle_sunk(const fr_game *g, const fr_lane *l, int i, int t);
/* whether the frog is drawn at all - it blinks away while the splat plays */
bool fr_frog_visible(const fr_game *g);
/* the notice across the middle, or NULL when there is none */
const char *fr_banner(const fr_game *g);
