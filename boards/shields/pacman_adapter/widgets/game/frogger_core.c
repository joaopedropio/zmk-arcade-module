/*
 * Crossing dongle - game core (portable).
 *
 * Five lanes of traffic and five of river, running on fixed loops so the gaps
 * a frog waits for are gaps that actually arrive.  Nobody is playing it: every
 * few frames the pilot asks of the five cells it could be on next - forward,
 * either side, back, or the one it is on - whether the traffic will be off it
 * and whether something will still be floating under it, and takes the best
 * answer.  Forward outscores everything, waiting costs nothing, and going back
 * is what it does when the lane it is in is about to be occupied.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "frogger_core.h"

/* ------------------------------------------------------------------ */
/* how long everything takes                                           */
/* ------------------------------------------------------------------ */

/*
 * A hop is two frames of travel and then three of sitting still.  Both halves
 * matter: at one frame of travel the frog teleports a cell, and with no pause
 * after it the pilot re-decides so often that a lane looks like something
 * being crossed by a skimming stone.  Together they are three hops a second at
 * fifteen frames, which is about what a person manages on the machine - and
 * the pause is also what FR_COMMIT below is measured from, so shortening it
 * makes the frog quicker and braver at once.
 */
#define FR_HOP   2
#define FR_THINK 3

/*
 * How far ahead the pilot looks at traffic, in frames.  It is counted from the
 * end of whatever the frog is committed to rather than from now, so this is
 * the margin on top of the hop and the pause - at these lane speeds, most of a
 * car length either side.  The collision the frog cannot avoid by looking is
 * the one it takes mid-hop: a frog halfway between two rows is in both of
 * them, and the car it just dodged is still going past.
 */
#define FR_LOOK 7

/*
 * And how far ahead the slow hazards are followed.  A raft going under and a
 * ride running out of panel both take seconds rather than frames, and both are
 * things the frog has to leave early or not at all - a horizon short enough
 * for traffic sees them when it is already too late to be anywhere else.
 */
#define FR_AHEAD 24

/* the phases that are not play, in frames */
#define FR_DYING_FRAMES 24
#define FR_HOMED_FRAMES 14
#define FR_LEVEL_FRAMES 50
#define FR_OVER_FRAMES  75

#define FR_LIVES 3

/* how much of the frog has to be over a log for it to be standing on one */
#define FR_GRIP 4

/*
 * How far off the middle of a bay the frog may be and still drop into it.  A
 * bay is sixteen pixels and the frog is twelve, so seven is nearly all of the
 * gap - deliberately, because the only way to line up on a bay is to drift
 * onto it, and a tighter window means a frog that rides past every one of them
 * waiting for a pixel that never comes.  It snaps to the middle on landing.
 */
#define FR_BAY_GRAB 7

/* how close to the edge of the panel a raft may carry it before it must move */
#define FR_EDGE 14

/*
 * What a lane running the right way is worth against the row it costs.  Bigger
 * than the value of a row forward, so the frog will hold a useful current and
 * even drop back into one; small enough that it never outweighs a danger.
 */
#define FR_STEER 60

/*
 * Frames of no progress after which the pilot starts taking gaps it would
 * otherwise refuse.  The same bargain the maze's `hungry` and the shooter's
 * SS_PATIENCE make: the animation must never settle into a frog that has
 * decided nothing is safe and a road that never empties.
 */
#define FR_PATIENCE 150

/* how much faster each level runs, and where that stops */
#define FR_LEVEL_STEP 12
#define FR_LEVEL_CAP  60

/* ------------------------------------------------------------------ */
/* arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t rnd(fr_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int range(fr_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

/* the frog is drawn, not clipped, at the edges: it may not be carried past
 * the point where its own sprite would leave the panel */
static bool off_panel(int mid) {
    return mid - FR_FROG_W / 2 < 0 || mid + FR_FROG_W / 2 >= PM_PANEL;
}

/* the middle of a column, and of a bay, in panel pixels */
static int col_mid(int col) { return col * FR_CELL + FR_CELL / 2; }
static int bay_mid(int bay) { return col_mid(FR_BAY_COL(bay)); }

/* ------------------------------------------------------------------ */
/* the lanes                                                           */
/* ------------------------------------------------------------------ */

/*
 * The board, top to bottom.  Directions alternate the whole way down, which is
 * the arcade's layout and is what makes the river readable: two lanes running
 * the same way look like one wide lane, and a frog crossing them appears to
 * stand still.  The speeds are eighths of a pixel a frame - a log at 8 takes
 * sixteen seconds to cross the panel and a racing car at 26 takes five - and
 * they are what the level multiplier scales.
 *
 * Occupancy is the number that decides whether this is playable at all.  Each
 * lane is drawn as its movers spread evenly round a track FR_LOOP long, so
 * count * span against FR_LOOP is the fraction of the lane that is solid: a
 * quarter for the road, which leaves gaps a frog can sit in, and a half for
 * the river, which leaves gaps it must not.  Half is also about as sparse as
 * the river can be before the frog spends the whole clock on the median
 * waiting for something to stand on.
 */
static const struct {
    uint8_t row, kind, count, dives, span;
    int16_t speed;
} LANES[] = {
    {1,  FR_K_LOG,    3, 0, 48,   8},
    {2,  FR_K_TURTLE, 3, 0, 48, -12},
    {3,  FR_K_LOG,    3, 0, 48,  14},
    {4,  FR_K_LOG,    3, 0, 32, -11},
    {5,  FR_K_TURTLE, 3, 1, 48,  12},
    {7,  FR_K_RACER,  3, 0, FR_W_RACER,  26},
    {8,  FR_K_TRUCK,  2, 0, FR_W_TRUCK, -14},
    {9,  FR_K_RACER,  3, 0, FR_W_RACER,  22},
    {10, FR_K_CAR,    3, 0, FR_W_CAR,   -16},
    {11, FR_K_CAR,    3, 0, FR_W_CAR,    13},
};

#define FR_LANE_ROWS ((int)(sizeof(LANES) / sizeof(LANES[0])))

/*
 * Laying a lane out is spacing its movers evenly round the loop and then
 * pushing each one off that mark by up to a third of the gap.  Even spacing on
 * its own is what a conveyor looks like; the jitter is what makes it traffic.
 * Whatever the offsets are they never change again - the whole lane moves as
 * one - so a gap wide enough to sit in at the start is wide enough forever.
 */
static void build_lanes(fr_game *g) {
    int mult = 100 + FR_LEVEL_STEP * (int)(g->level - 1);
    if (mult > 100 + FR_LEVEL_CAP) {
        mult = 100 + FR_LEVEL_CAP;
    }

    for (int r = 0; r < FR_ROWS; r++) {
        g->lanes[r].kind = FR_K_NONE;
        g->lanes[r].count = 0;
        g->lanes[r].speed = 0;
        g->lanes[r].dives = 0;
    }

    for (int i = 0; i < FR_LANE_ROWS; i++) {
        fr_lane *l = &g->lanes[LANES[i].row];
        int step = FR_LOOP * FR_SUB / LANES[i].count;
        /* no jitter at all rather than negative jitter, for a lane packed so
         * tightly that there is no room to push anything off its mark */
        int slack = (step - LANES[i].span * FR_SUB) / 3;
        if (slack < 0) {
            slack = 0;
        }

        l->kind = LANES[i].kind;
        l->count = LANES[i].count;
        l->dives = LANES[i].dives;
        l->speed = (int16_t)(LANES[i].speed * mult / 100);
        for (int m = 0; m < l->count; m++) {
            l->pos[m] = (uint16_t)((m * step + range(g, 0, slack)) % (FR_LOOP * FR_SUB));
            l->span[m] = LANES[i].span;
            /* rafts dive out of step with each other; logs use it for grain */
            l->phase[m] = (uint8_t)range(g, 0, FR_DIVE_CYCLE - 1);
        }
    }
}

static int lane_speed(const fr_game *g, const fr_lane *l) {
    return (int)l->speed * (int)g->speed / 4;
}

/*
 * Where a mover's left edge is t frames from now, in eighths.  The frog is
 * carried by whatever it stands on at exactly this speed, so the two have to
 * be compared in eighths and not in pixels: rounded to pixels, a frog and its
 * log sit a pixel further apart on some frames than on others, and a frog
 * standing on the very end of a log would be shaken off by the rounding.
 */
static int mover_x8(const fr_game *g, const fr_lane *l, int i, int t) {
    int p = ((int)l->pos[i] + lane_speed(g, l) * t) % (FR_LOOP * FR_SUB);

    if (p < 0) {
        p += FR_LOOP * FR_SUB;
    }
    return p - FR_RUNOFF * FR_SUB;
}

int fr_mover_x(const fr_game *g, const fr_lane *l, int i, int t) {
    return (mover_x8(g, l, i, t) + FR_RUNOFF * FR_SUB) / FR_SUB - FR_RUNOFF;
}

/*
 * How far under a raft has gone: 0 while it is up, then one step per frame
 * through FR_DIVE_WARN, at which point it is water.  The count is a lane-wide
 * clock plus the raft's own phase rather than the frame number, so nothing
 * jumps when the frame counter wraps - a raft that vanished under a frog's
 * feet once an hour would be the least explicable death in the game.
 */
int fr_turtle_sunk(const fr_game *g, const fr_lane *l, int i, int t) {
    if (l->kind != FR_K_TURTLE || !l->dives) {
        return 0;
    }
    int c = ((int)g->dive_t + t + (int)l->phase[i]) % FR_DIVE_CYCLE;
    int down = FR_DIVE_CYCLE - FR_DIVE_DOWN;

    if (c >= down) {
        return FR_DIVE_WARN;
    }
    if (c < down - FR_DIVE_WARN) {
        return 0;
    }
    return c - (down - FR_DIVE_WARN);
}

/* ------------------------------------------------------------------ */
/* what is where                                                       */
/* ------------------------------------------------------------------ */

/* the band a lane's sprites occupy, which is the cell less a strip either side */
static int lane_top(int row) { return FR_ROW_Y(row) + (FR_CELL - FR_SPRITE_H) / 2; }

/*
 * Whether anything in a road lane is over a box t frames from now.  Sampled a
 * frame at a time rather than swept: to pass through between two samples a car
 * would have to cross its own length plus the frog's in one frame, which is
 * thirty pixels against the seven the quickest lane manages at the top gear on
 * a late level.  Sampling also keeps the arithmetic the same for a lane that
 * wraps round the loop during the window as for one that does not.
 */
static bool road_hit(const fr_game *g, int row, int x0, int x1, int t) {
    const fr_lane *l = &g->lanes[row];

    if (l->kind != FR_K_CAR && l->kind != FR_K_TRUCK && l->kind != FR_K_RACER) {
        return false;
    }
    for (int i = 0; i < l->count; i++) {
        int mx = fr_mover_x(g, l, i, t);
        if (x1 >= mx && x0 <= mx + (int)l->span[i] - 1) {
            return true;
        }
    }
    return false;
}

/*
 * Which float is under a point, or -1 for water.  A raft that has started to
 * sink still holds the frog up - it is only water once it is all the way
 * down - which is what makes the sinking frames a warning rather than a
 * decoration.
 */
static int float_at(const fr_game *g, int row, int x8, int t) {
    const fr_lane *l = &g->lanes[row];

    if (l->kind != FR_K_LOG && l->kind != FR_K_TURTLE) {
        return -1;
    }
    for (int i = 0; i < l->count; i++) {
        int mx = mover_x8(g, l, i, t);
        int far = mx + ((int)l->span[i] - 1) * FR_SUB;
        if (x8 - FR_GRIP * FR_SUB < mx || x8 + FR_GRIP * FR_SUB > far) {
            continue;
        }
        if (fr_turtle_sunk(g, l, i, t) >= FR_DIVE_WARN) {
            continue;
        }
        return i;
    }
    return -1;
}

static bool is_river(int row) { return row >= FR_ROW_RIVER && row < FR_ROW_MEDIAN; }
static bool is_road(int row) { return row >= FR_ROW_ROAD && row < FR_ROW_START; }
/* the two rows nothing can happen on, which are the only ones worth walking */
static bool is_bank(int row) { return row == FR_ROW_MEDIAN || row == FR_ROW_START; }

/* the bay a point is over, or -1 for hedge */
static int bay_at(int mid) {
    for (int b = 0; b < FR_BAYS; b++) {
        if (iabs(mid - bay_mid(b)) <= FR_BAY_GRAB) {
            return b;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* the frog                                                            */
/* ------------------------------------------------------------------ */

static void place_frog(fr_game *g) {
    fr_frog *f = &g->frog;

    f->row = FR_ROW_START;
    f->x = f->tx = (int16_t)(col_mid(FR_COLS / 2) * FR_SUB);
    f->y = f->ty = (int16_t)((FR_ROW_Y(FR_ROW_START) + FR_CELL / 2) * FR_SUB);
    f->hop = 0;
    f->facing = 0;
    g->reached = FR_ROW_START;
    g->clock = FR_TIME;
    g->aim = -1;
    g->think = FR_THINK;
    g->patient = 0;
}

static void die(fr_game *g, fr_death why) {
    g->why = (uint8_t)why;
    g->phase = FR_DYING;
    g->phase_timer = FR_DYING_FRAMES;
    g->frog.hop = 0;
}

static void start_hop(fr_game *g, int dcol, int drow) {
    fr_frog *f = &g->frog;

    f->tx = (int16_t)(f->x + dcol * FR_CELL * FR_SUB);
    f->ty = (int16_t)((FR_ROW_Y(f->row + drow) + FR_CELL / 2) * FR_SUB);
    f->row = (uint8_t)(f->row + drow);
    f->hop = FR_HOP;
    if (drow < 0) {
        f->facing = 0;
    } else if (drow > 0) {
        f->facing = 2;
    } else {
        f->facing = dcol > 0 ? 1 : 3;
    }
}

/* ten a row, and only for ground it has not stood on this trip */
static void credit_row(fr_game *g) {
    if (g->frog.row < g->reached) {
        g->score += 10u * (uint32_t)(g->reached - g->frog.row);
        g->reached = g->frog.row;
        g->patient = 0;
    }
}

static void reach_home(fr_game *g, int bay) {
    g->bay[bay] = true;
    g->score += 50u + g->clock / 5u;
    if (g->fly == bay) {
        g->score += 200u;
        g->fly = -1;
    }

    for (int b = 0; b < FR_BAYS; b++) {
        if (!g->bay[b]) {
            g->phase = FR_HOMED;
            g->phase_timer = FR_HOMED_FRAMES;
            return;
        }
    }
    g->score += 1000u;
    g->phase = FR_LEVEL;
    g->phase_timer = FR_LEVEL_FRAMES;
}

/*
 * What the frog is standing on, checked every frame rather than only on
 * landing: a car reaching it halfway through a hop is a car that hit it, since
 * a frog between two rows is in both.  The river is the other way round - in
 * the air it is over nothing and that is fine, so drowning is only ever
 * decided once it is down.
 */
static void check_ground(fr_game *g, bool landed) {
    fr_frog *f = &g->frog;
    int mid = FR_PX(f->x);
    int fy = FR_PX(f->y);

    for (int r = FR_ROW_ROAD; r < FR_ROW_START; r++) {
        if (fy + FR_HIT < lane_top(r) || fy - FR_HIT >= lane_top(r) + FR_SPRITE_H) {
            continue;
        }
        if (road_hit(g, r, mid - FR_HIT, mid + FR_HIT, 0)) {
            die(g, FR_D_CAR);
            return;
        }
    }

    if (f->hop > 0) {
        return;
    }

    if (is_river(f->row)) {
        /*
         * It travels with what it is standing on, and off the panel with it.
         * The drift goes on before the check and only on a frame it did not
         * land: a landing was aimed at where the log would be by now, so
         * carrying it another frame would put the frog past its own target.
         */
        if (!landed) {
            f->x = (int16_t)(f->x + lane_speed(g, &g->lanes[f->row]));
            f->tx = f->x;
            mid = FR_PX(f->x);
        }
        if (off_panel(mid)) {
            die(g, FR_D_EDGE);
        } else if (float_at(g, f->row, f->x, 0) < 0) {
            die(g, landed ? FR_D_WATER : FR_D_SUNK);
        }
        return;
    }

    if (f->row == FR_ROW_HOME) {
        int b = bay_at(mid);
        if (b < 0 || g->bay[b]) {
            die(g, FR_D_HEDGE);
            return;
        }
        f->x = f->tx = (int16_t)(bay_mid(b) * FR_SUB);
        reach_home(g, b);
    }
}

/* ------------------------------------------------------------------ */
/* the pilot                                                           */
/* ------------------------------------------------------------------ */

/*
 * Which bay this trip is for, worked out afresh every time it decides: the
 * river keeps moving it, so the bay that was nearest when it set off is often
 * not the one it can reach.  A bay the current is already carrying it towards
 * is worth three columns of detour, because the river will do that work for
 * nothing while reaching one upstream costs a hop against the drift for every
 * cell of it.
 */
static void pick_aim(fr_game *g) {
    int mid = FR_PX(g->frog.x);
    int drift = lane_speed(g, &g->lanes[is_river(g->frog.row) ? g->frog.row : FR_ROW_RIVER]);
    int best = -1, best_cost = 0;

    for (int b = 0; b < FR_BAYS; b++) {
        if (g->bay[b]) {
            continue;
        }
        int cost = iabs(bay_mid(b) - mid);
        if ((bay_mid(b) - mid) * drift < 0) {
            cost += 3 * FR_CELL;
        }
        if (g->fly == b) {
            cost -= 2 * FR_CELL;
        }
        if (best < 0 || cost < best_cost) {
            best = b;
            best_cost = cost;
        }
    }
    g->aim = (int8_t)best;
}

/*
 * What a cell costs to be on.  Danger comes in grades and the grades are the
 * whole behaviour of this thing.  A cell that is death on arrival - the hedge,
 * a full bay, open water, the edge of the panel - is ruled out absolutely;
 * everything else is priced by how soon it goes wrong, so a frog on a raft
 * that is sinking now will take a lane that might be trouble in twenty frames,
 * and one with nothing safe left picks the option that kills it latest rather
 * than the one that scores highest.  Without the grades, forward always scores
 * highest, and a board that has closed up is a frog in the river.
 */
#define FR_RISK  1000
#define FR_FATAL 100000

/*
 * How long the frog is stuck with a cell once it commits to it: the hop in,
 * the pause before it may decide again, and the hop out.  Anything that
 * happens to that cell inside this window happens to the frog, whatever it
 * decides in the meantime - which is why a hazard is timed from here rather
 * than from now, and why staying put is cheaper than moving: a frog that is
 * already standing somewhere re-decides every frame, so all it is committed to
 * is the hop it would make.
 */
#define FR_COMMIT (FR_HOP + FR_THINK + FR_HOP)

/*
 * And how long it is stuck with the cell it is already on, which is only the
 * hop out of it and a frame's grace, since a frog that is sitting still
 * re-decides every frame.  That difference is what makes waiting the cheap
 * option and moving the considered one.
 */
#define FR_HOLD (FR_HOP + 2)

/*
 * What a hazard t frames away is worth, and the shape of this is the whole
 * temperament of the pilot.  Anything that arrives while the frog is still
 * committed to the cell is full price and effectively refused.  Everything
 * past that is worth less than a single row of progress however close it is,
 * because by then the frog will have moved on - so the tail only ever breaks
 * ties between cells that are both safe for as long as it matters, and never
 * argues the frog out of getting on with it.  Making that tail expensive was
 * a frog that stood on the bank waiting for a river with nothing wrong with
 * it anywhere, and ran out of clock.
 */
#define FR_TAIL 80

static int hazard(int t, int commit, int look) {
    int left = t - commit;

    if (left <= 0) {
        return FR_RISK;
    }
    if (left >= look) {
        return 0;
    }
    return FR_TAIL * (look - left) * (look - left) / (look * look);
}

/* how many frames of ride are left before the panel runs out under it */
static int ride_left(const fr_game *g, int row, int mid) {
    int drift = lane_speed(g, &g->lanes[row]);

    if (drift == 0) {
        return FR_AHEAD;
    }
    int slack = drift > 0 ? PM_PANEL - 1 - FR_FROG_W / 2 - mid : mid - FR_FROG_W / 2;
    return slack * FR_SUB / (drift > 0 ? drift : -drift);
}

/*
 * The two ways a ride ends: the raft goes under, or the river runs out of
 * panel.  Only where the frog lands is checked for something under it - after
 * that it is carried by whatever it landed on, so the question stops being
 * "will a log be here" and becomes "will this log still be up".  Asking the
 * first of a frog that is moving with the river is how a pilot talks itself
 * out of a log it is standing on quite safely.
 */
static int river_cost(const fr_game *g, int row, int mid, int i, int t0, int commit) {
    int cost = hazard(ride_left(g, row, mid), commit, FR_AHEAD);

    for (int t = t0; t <= t0 + commit + FR_AHEAD; t++) {
        if (fr_turtle_sunk(g, &g->lanes[row], i, t) < FR_DIVE_WARN) {
            continue;
        }
        int sinking = hazard(t - t0, commit, FR_AHEAD);
        return sinking > cost ? sinking : cost;
    }
    return cost;
}

static int road_cost(const fr_game *g, int row, int mid, int t0, int commit) {
    for (int t = t0; t <= commit + FR_LOOK; t++) {
        if (road_hit(g, row, mid - FR_HIT, mid + FR_HIT, t)) {
            return hazard(t, commit, FR_LOOK);
        }
    }
    return 0;
}

static int cell_cost(const fr_game *g, int row, int x8, int commit) {
    int mid = FR_PX(x8);

    if (off_panel(mid)) {
        return FR_FATAL;
    }
    if (row == FR_ROW_HOME) {
        int b = bay_at(mid);
        return (b >= 0 && !g->bay[b]) ? 0 : FR_FATAL;
    }
    if (is_road(row)) {
        if (road_hit(g, row, mid - FR_HIT, mid + FR_HIT, FR_HOP)) {
            return FR_FATAL; /* it would land under the wheels */
        }
        return road_cost(g, row, mid, 1, commit);
    }
    if (is_river(row)) {
        int i = float_at(g, row, x8, FR_HOP);
        return i < 0 ? FR_FATAL : river_cost(g, row, mid, i, FR_HOP, commit);
    }
    return 0;
}

/* staying put, by the same grades, and committed only to the hop out */
static int stay_cost(const fr_game *g) {
    int row = g->frog.row;
    int mid = FR_PX(g->frog.x);

    if (is_road(row)) {
        return road_cost(g, row, mid, 1, FR_HOLD);
    }
    if (is_river(row)) {
        int i = float_at(g, row, g->frog.x, 0);
        return i < 0 ? FR_FATAL : river_cost(g, row, mid, i, 0, FR_HOLD);
    }
    return 0;
}

/*
 * One move's worth.  Forward is worth more than anything else can take away,
 * so the frog only ever waits or steps aside when forward is unsafe; the
 * alignment term is the exception and it only applies in the top half, where
 * being over the right bay is the whole job.  Going back is a real cost rather
 * than a forbidden move, because the road is occasionally crossed best by
 * dropping into the lane behind.
 */
/*
 * What a bank cell with a clear way out of it is worth.  Written as a bonus
 * for the good columns rather than a penalty on the bad ones, and the two are
 * not the same thing: a penalty makes standing on a bad column worse than
 * stepping back off the bank altogether, so the frog drops onto the road
 * rather than wait.  It sits between the eight a sideways hop costs and the
 * hundred a row forward is worth, which is exactly the behaviour wanted: the
 * gap between the two is the same whatever the column, so taking a column that
 * opens always beats standing on it, and walking to one always beats standing
 * on a column that does not.
 */
#define FR_OPENS 90

static int move_value(const fr_game *g, int dcol, int drow, int x8, int row, int commit) {
    int mid = FR_PX(x8);
    int value = 0;

    if (drow < 0) {
        value += 100;
    } else if (drow > 0) {
        value -= 70;
    } else if (dcol != 0) {
        value -= 8;
    }

    /*
     * The sides of the panel are a slow death in the river - there is no bank
     * to land on and the current does not turn round - so a cell is worth less
     * the closer to one it is.  This is what makes the frog work its way back
     * towards the middle while it still has somewhere to go, rather than
     * discovering the edge when it is against it.
     */
    if (is_river(row)) {
        int slack = mid < PM_PANEL / 2 ? mid : PM_PANEL - 1 - mid;
        if (slack < 3 * FR_EDGE) {
            value -= (3 * FR_EDGE - slack) * 2;
        }
    }

    /*
     * Lining up on the aimed-at bay, which is only worth anything in the river
     * itself.  Doing it on the median as well looked sensible and cost a third
     * of the clock: the frog walked the width of the panel before setting off,
     * and then the current put it somewhere else anyway.
     */
    /*
     * What a cell on a bank opens onto.  Standing where the river is about to
     * be boardable, or where the next lane of traffic is empty, is worth
     * walking a few columns for - and it is the only reason there is to walk
     * along a bank at all, so without this the frog picks a column on the
     * bank it starts from and waits there for the whole board to come to it.
     *
     * Only the two banks, and deliberately: it is an argument for waiting
     * somewhere better, and the only rows where waiting is free are the ones
     * nothing can run the frog over on.
     */
    if (is_bank(row)) {
        int above = cell_cost(g, row - 1, x8, commit);
        value += above >= FR_OPENS ? 0 : FR_OPENS - above;
    }

    /*
     * Lining up, which is only a question once it is in the river.  On a bank
     * the frog cannot drift, so the aim never changes while it stands there -
     * and a bank where waiting scores better than setting off is a frog that
     * waits for ever.  Both of the terms below can do that, so neither of them
     * is allowed to weigh on the hop that gets it off dry land.
     */
    if (g->aim >= 0 && is_river(row) && is_river(g->frog.row)) {
        int want = bay_mid(g->aim) - mid;
        value -= iabs(want) / 2;
        /*
         * And the lane itself is worth more than the row it is in.  The river
         * is the only steering there is: a frog that climbs out of the lane
         * carrying it towards the bay it wants, into one running the other
         * way, arrives at the top of the river on the wrong side of the last
         * bay and cannot get back - which is how a level ends with four bays
         * full and the clock out.  Beyond a cell of travel this outweighs the
         * row it would gain, so it will drop back a lane to catch a current
         * going its way; inside a cell it stops mattering and forward wins.
         */
        if (iabs(want) > FR_CELL) {
            int drift = lane_speed(g, &g->lanes[row]);
            value += (want > 0) == (drift > 0) ? FR_STEER : -FR_STEER;
        }
    }
    /* a bay lined up is worth taking over another row of river; a hop at the
     * hedge is ruled out by its cost rather than by this */
    if (row == FR_ROW_HOME) {
        value += 60;
    }
    return value;
}

static void think(fr_game *g) {
    fr_frog *f = &g->frog;
    static const int8_t TRY[][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}, {0, 0}};
    /*
     * How much of a cell it insists on having to itself.  A frog that has been
     * waiting, or is running out of clock, settles for a gap it would rather
     * not have taken - which is what stops the animation from a board that has
     * closed up and a frog that has decided to sit out the rest of the round.
     */
    int commit = FR_COMMIT;
    if (g->patient > FR_PATIENCE || g->clock < FR_TIME / 4) {
        commit = FR_COMMIT - FR_HOP; /* it will land with no room to hop out */
    }

    pick_aim(g);

    int best_i = 4, best = -(1 << 30);
    for (int i = 0; i < 5; i++) {
        int drow = TRY[i][1], dcol = TRY[i][0];
        int row = f->row + drow;
        /* the same arithmetic start_hop() will do, so the cell being judged is
         * exactly the cell being hopped to - down to the eighth of a pixel */
        int x8 = f->x + dcol * FR_CELL * FR_SUB;

        if (row < 0 || row > FR_ROW_START) {
            continue;
        }
        int cost = (drow == 0 && dcol == 0) ? stay_cost(g) : cell_cost(g, row, x8, commit);
        int value = move_value(g, dcol, drow, x8, row, commit) - cost;
        if (value > best) {
            best = value;
            best_i = i;
        }
    }

    if (TRY[best_i][0] == 0 && TRY[best_i][1] == 0) {
        return;
    }
    start_hop(g, TRY[best_i][0], TRY[best_i][1]);
    g->think = (uint8_t)(FR_HOP + FR_THINK * 4 / g->speed);
}

/* ------------------------------------------------------------------ */
/* the frame                                                           */
/* ------------------------------------------------------------------ */

/*
 * The fly.  It is the one thing on the board worth going out of the way for,
 * and it is on a timer, so it is also the only reason the pilot ever aims at
 * anything but the nearest bay.  One at a time and never in a full one.
 */
#define FR_FLY_FRAMES 150
#define FR_FLY_ODDS   400

static void tick_fly(fr_game *g) {
    if (g->fly >= 0) {
        if (--g->fly_left == 0) {
            g->fly = -1;
        }
        return;
    }
    if (rnd(g) % FR_FLY_ODDS != 0) {
        return;
    }
    int free_bays = 0;
    for (int b = 0; b < FR_BAYS; b++) {
        free_bays += g->bay[b] ? 0 : 1;
    }
    if (free_bays == 0) {
        return;
    }
    int pick = range(g, 0, free_bays - 1);
    for (int b = 0; b < FR_BAYS; b++) {
        if (g->bay[b]) {
            continue;
        }
        if (pick-- == 0) {
            g->fly = (int8_t)b;
            g->fly_left = FR_FLY_FRAMES;
            return;
        }
    }
}

static void advance_lanes(fr_game *g) {
    for (int r = 0; r < FR_ROWS; r++) {
        fr_lane *l = &g->lanes[r];
        int v = lane_speed(g, l);

        for (int i = 0; i < l->count; i++) {
            int p = (int)l->pos[i] + v;
            if (p < 0) {
                p += FR_LOOP * FR_SUB;
            } else if (p >= FR_LOOP * FR_SUB) {
                p -= FR_LOOP * FR_SUB;
            }
            l->pos[i] = (uint16_t)p;
        }
    }
    g->dive_t = (uint8_t)((g->dive_t + 1) % FR_DIVE_CYCLE);
}

/* true on the frame a hop ends, which is the one frame the river is not
 * allowed to carry the frog: it was aimed at where the log is now */
static bool advance_hop(fr_game *g) {
    fr_frog *f = &g->frog;

    if (f->hop == 0) {
        return false;
    }
    f->x = (int16_t)(f->x + (f->tx - f->x) / f->hop);
    f->y = (int16_t)(f->y + (f->ty - f->y) / f->hop);
    if (--f->hop == 0) {
        f->x = f->tx;
        f->y = f->ty;
        /* only the river lets it stand between columns; everywhere else it
         * lands on the lattice, which is what keeps the road hops square and
         * is how a frog coming off a log gets back onto it */
        if (!is_river(f->row)) {
            int col = FR_PX(f->x) / FR_CELL;
            f->x = f->tx = (int16_t)(col_mid(col) * FR_SUB);
        }
        credit_row(g);
        return true;
    }
    return false;
}

static void next_life(fr_game *g) {
    if (--g->lives == 0) {
        g->phase = FR_OVER;
        g->phase_timer = FR_OVER_FRAMES;
        return;
    }
    place_frog(g);
    g->phase = FR_PLAY;
}

static void next_level(fr_game *g) {
    /* the speed stops climbing long before this; the cap is so that a dongle
     * left running for days keeps printing a level number two digits can hold */
    if (g->level < 99) {
        g->level++;
    }
    for (int b = 0; b < FR_BAYS; b++) {
        g->bay[b] = false;
    }
    g->fly = -1;
    build_lanes(g);
    place_frog(g);
    g->phase = FR_PLAY;
    g->redraw = true;
}

void fr_init(fr_game *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed | 1u;
    g->speed = 4;
    g->level = 1;
    g->lives = FR_LIVES;
    g->fly = -1;
    build_lanes(g);
    place_frog(g);
    g->phase = FR_PLAY;
    g->redraw = true;
}

void fr_set_speed(fr_game *g, uint8_t gear) {
    g->speed = gear < 3 ? 3 : (gear > 5 ? 5 : gear);
}

void fr_step(fr_game *g) {
    if (g->phase != FR_PLAY) {
        if (--g->phase_timer > 0) {
            /*
             * The board keeps running underneath a splat or a filled bay, so a
             * frog that starts again is looking at traffic that moved on
             * without it.  Game over is the exception: everything stopping is
             * what makes that notice read as an ending rather than a caption.
             */
            if (g->phase != FR_OVER) {
                advance_lanes(g);
            }
            return;
        }
        switch (g->phase) {
        case FR_DYING:
            next_life(g);
            break;
        case FR_HOMED:
            place_frog(g);
            g->phase = FR_PLAY;
            break;
        case FR_LEVEL:
            next_level(g);
            break;
        default:
            fr_init(g, g->rng);
            break;
        }
        return;
    }

    advance_lanes(g);
    tick_fly(g);

    bool landed = advance_hop(g);
    check_ground(g, landed);
    if (g->phase != FR_PLAY) {
        return;
    }

    if (g->clock > 0 && --g->clock == 0) {
        die(g, FR_D_TIME);
        return;
    }

    g->patient++;
    if (g->think > 0) {
        g->think--;
    } else if (g->frog.hop == 0) {
        think(g);
    }
}

/* ------------------------------------------------------------------ */
/* what the renderer asks                                              */
/* ------------------------------------------------------------------ */

int fr_splat_age(const fr_game *g) {
    return g->phase == FR_DYING ? FR_DYING_FRAMES - (int)g->phase_timer : -1;
}

bool fr_frog_visible(const fr_game *g) {
    return g->phase == FR_PLAY || g->phase == FR_HOMED;
}

const char *fr_banner(const fr_game *g) {
    static char word[10];

    if (g->phase == FR_OVER) {
        return "GAME OVER";
    }
    if (g->phase == FR_LEVEL) {
        word[0] = 'L';
        word[1] = 'E';
        word[2] = 'V';
        word[3] = 'E';
        word[4] = 'L';
        word[5] = ' ';
        pm_digits(g->level, 2, word + 6);
        return word;
    }
    return NULL;
}
