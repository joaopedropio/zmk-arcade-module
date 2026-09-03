/*
 * Bomberman dongle - game core (portable, no Zephyr/LVGL dependencies).
 *
 * The fourth thing the panel can play, and the first one where the board
 * itself is the thing that changes.  A bomber walks a lattice of pillars and
 * soft brick, drops bombs that burst in a cross a few cells long, and has to
 * be somewhere else when they go off; the brick they break is what opens the
 * board up, and under one of those bricks is the way out.  Clear the enemies,
 * find the door, walk through it.
 *
 * The maze's actors move on a lattice and the shooter's fly free; this one is
 * the maze's model - whole cells, turns only where the cells line up - but
 * what the lattice is made of is not fixed, which is the part neither of the
 * others has.  Every brick that goes changes what can be walked, what can be
 * seen and what a bomb reaches, so nothing here may cache a route.
 *
 * What is load-bearing is the geometry, the three times below, and the prices
 * the pilot pays for things.  Read the comments before changing a number.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "panel.h"

/*
 * The board is a lattice like the maze's, but the other way round: here the
 * fixed pillars sit on even/even and everything else is walkable, so every
 * corridor is one cell wide and no two pillars ever touch.  That is what makes
 * a bomb worth aiming - a blast running down a corridor is stopped by brick
 * rather than by the layout - and it is why both counts are odd.
 *
 * 16px cells put 15 across the panel exactly, so unlike the maze there is no
 * margin to paint and the board runs edge to edge.  13 rows leaves 32px at the
 * top for the readout, which is the only place there is room for one: the
 * board has no border to write in and a bomber standing on a caption would be
 * unreadable at arm's length.
 */
#define BB_CELL 16
#define BB_COLS 15
#define BB_ROWS 13
#define BB_W    (BB_COLS * BB_CELL) /* 240, the whole width of the panel */
#define BB_H    (BB_ROWS * BB_CELL) /* 208 */
#define BB_OY   (ARC_PANEL - BB_H)   /* 32: the readout band above the board */

_Static_assert(BB_W == ARC_PANEL, "the board has to fill the panel across");
_Static_assert((BB_COLS & 1) != 0 && (BB_ROWS & 1) != 0, "the pillar lattice needs odd counts");

/* what is in a cell.  A brick becomes floor when a blast reaches it */
enum {
    BB_C_FLOOR = 0,
    BB_C_SOLID, /* pillar or border: nothing ever moves it */
    BB_C_BRICK, /* soft: one blast takes it, and the blast stops there */
};

/*
 * What was hidden under a brick.  The door is not a pickup - it is the way out
 * and only works once every enemy is gone - but it is kept in the same array
 * because it is hidden the same way and revealed by the same blast.
 */
enum {
    BB_I_NONE = 0,
    BB_I_BOMB,  /* one more bomb out at a time */
    BB_I_FLAME, /* one more cell on every arm */
    BB_I_SPEED, /* a pixel a frame quicker */
    BB_I_DOOR,
    BB_I_KINDS,
};

typedef enum {
    BB_UP = 0,
    BB_RIGHT,
    BB_DOWN,
    BB_LEFT,
    BB_DIRS,
    BB_NODIR,
} bb_dir;

typedef enum {
    BB_READY = 0, /* a short hold so a new board can be read before it moves */
    BB_PLAY,
    BB_DYING,   /* the bomber is going up; the board carries on */
    BB_CLEARED, /* through the door, the board flashing */
    BB_OVER,    /* out of lives, about to start again */
} bb_phase;

/*
 * How long a fuse burns, and how long what it lights hangs about.  Two seconds
 * of fuse at fifteen frames a second is what makes a bomb a decision rather
 * than a reflex: it is four cells of walking, so the bomber has to be able to
 * get four cells away and there has to be somewhere four cells away to get to.
 * Shorten it and every bomb is a trap; lengthen it and the board never opens,
 * because the pilot spends the difference standing still.
 *
 * The flame is the other half of that: it is what a cell costs after the bomb
 * has gone off, and it is short because the pilot has to walk back through the
 * hole the blast just made.
 */
#define BB_FUSE  24
#define BB_FLAME 7

/*
 * How much of the board one bomber owns at once.  Both grow by pickup and
 * both are capped, because past the cap a blast reaches across the whole
 * corridor and there is nowhere to stand: the game stops being about where to
 * put a bomb and starts being about waiting for the flames to clear.
 */
#define BB_RANGE_0   1
#define BB_RANGE_MAX 4
#define BB_BOMBS_0   2
#define BB_BOMBS_MAX 4
#define BB_BOMBS     6 /* what fits on the board at once, over all growth */

/* enemies, and how many a level asks for */
#define BB_FOES    6
#define BB_FOES_0  2

#define BB_LIVES 3

/*
 * A board has a clock, for the same reason the crossing does: without one a
 * pilot that cannot find a way in has nothing to lose by waiting, and a dongle
 * shows the same frame for an hour.  It is generous - about four minutes -
 * because clearing a board honestly means breaking thirty bricks one bomb at a
 * time, and running out is meant to be a verdict on a bad board rather than on
 * a slow one.
 */
#define BB_CLOCK 3600

typedef struct {
    int16_t x, y; /* top-left pixel of the actor's cell box, in board pixels */
    uint8_t dir;
    uint8_t step; /* how far it has walked, for the legs */
} bb_actor;

/*
 * Two enemies, and the difference is only what they do at a junction.  A
 * drifter carries straight on until it cannot, which makes it something to
 * time rather than to chase; a hunter turns towards the bomber when it can
 * see it down a clear corridor, which is what stops the pilot from parking in
 * one place and bombing the same wall all level.
 */
enum {
    BB_F_DRIFT = 0,
    BB_F_HUNT,
    BB_F_KINDS,
};

typedef struct {
    bb_actor a;
    uint8_t kind;
    uint8_t speed; /* pixels per frame; slower than the bomber, always */
    bool alive;
} bb_foe;

typedef struct {
    uint8_t r, c;
    uint8_t fuse;  /* frames left, counting down */
    uint8_t range; /* the arms it was dropped with, not the arms it has now */
    bool live;
} bb_bomb;

/*
 * What the last bb_step() would have made a noise about, if anything were
 * listening.  Nothing is - every game on this dongle is silent by design -
 * but the core says it anyway, the way the maze does, so the simulator can
 * count what happened without reaching into the board.
 */
enum {
    BB_SFX_BOMB = 1u << 0,  /* one was dropped */
    BB_SFX_BLAST = 1u << 1, /* and one went off */
    BB_SFX_BRICK = 1u << 2, /* a brick came down */
    BB_SFX_FOE = 1u << 3,   /* an enemy was caught in a blast */
    BB_SFX_ITEM = 1u << 4,  /* something was picked up */
    BB_SFX_DEATH = 1u << 5,
    BB_SFX_CLEAR = 1u << 6, /* out through the door */
};

/* why the bomber last died, which is the only number a soak can act on */
enum {
    BB_D_NONE = 0,
    BB_D_BURNT, /* stood in its own blast, or somebody else's */
    BB_D_CAUGHT,
    BB_D_CLOCK,
    BB_D_CAUSES,
};

typedef struct {
    uint8_t cell[BB_ROWS][BB_COLS];
    uint8_t item[BB_ROWS][BB_COLS]; /* a BB_I_*, hidden while the cell is brick */
    uint8_t fire[BB_ROWS][BB_COLS]; /* frames of flame left, 0 for none */

    bb_actor bomber;
    bb_foe foes[BB_FOES];
    bb_bomb bombs[BB_BOMBS];

    bb_phase phase;
    uint16_t phase_timer;

    uint8_t bombs_max; /* how many may be out at once, BB_BOMBS_0 upwards */
    uint8_t range;     /* cells per arm, BB_RANGE_0 upwards */
    uint8_t boots;     /* speed pickups taken, which is a pixel a frame each */

    uint8_t door_r, door_c;
    bool door_open; /* its brick is gone, so it can be walked into */
    uint8_t foes_left;

    uint16_t clock; /* frames left on this board */
    uint16_t idle;  /* frames since anything was broken, breaks stalemates */

    uint32_t score;
    uint8_t lives;
    uint8_t level;
    uint8_t cause; /* a BB_D_*, what took the last life */

    uint8_t speed; /* the gear the words per minute put it in, 3 to 5 */
    uint16_t frame;

    uint8_t sfx;
    bool redraw; /* the renderer must repaint the whole panel */
    bool flash;  /* the board flashes on the way out */

    uint32_t rng;
} bb_game;

void bb_init(bb_game *g, uint32_t seed);
void bb_step(bb_game *g);

/* 3 = slow, 4 = normal, 5 = quick: how fast the bomber walks */
void bb_set_speed(bb_game *g, uint8_t gear);

/* helpers shared with the renderer */
/* false while the bomber is blinking after a death, so the state is visible */
bool bb_bomber_visible(const bb_game *g);
/* whether the cell holds something the bomber can pick up and see */
bool bb_item_visible(const bb_game *g, int r, int c);
/* the notice across the middle of the board, or NULL when there is none */
const char *bb_banner(const bb_game *g);
