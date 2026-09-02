/*
 * Girders dongle - game core (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include "kong_core.h"

/* ------------------------------------------------------------------ */
/* how fast everything is                                              */
/* ------------------------------------------------------------------ */

/*
 * Eighths a frame, at the middle gear.  The two that matter are the first
 * two, and they matter against each other rather than on their own: a barrel
 * is half again as quick as the climber, so he cannot outrun one along a
 * girder and has to either be somewhere else when it arrives or be in the air.
 * Making them equal turns every barrel into a chase he wins, and the game into
 * a walk.
 */
#define DK_WALK 16
#define DK_ROLL 26
#define DK_CLIMB 12

/* how long a barrel takes to go down a ladder, and to fall off an end */
#define DK_LADDER_T 11
#define DK_DROP_T 6

/*
 * How often the ape lets one go, and how much quicker that gets each level.
 * The floor is what stops the board from becoming impassable rather than
 * merely hard: below about twenty frames there is a barrel on every girder at
 * once and no gap in any of them, which is not a harder climb but a shorter
 * one.
 */
#define DK_THROW 52
#define DK_THROW_STEP 4
#define DK_THROW_MIN 24
#define DK_THROW_ANIM 10 /* how long the ape is drawn winding up */

/* the phases that are not play, in frames */
#define DK_DYING_T 45
#define DK_WON_T 60
#define DK_OVER_T 60

/* how close to an end of a girder a barrel gets before it goes over */
#define DK_EDGE 6

/* what a barrel and a girder are worth */
#define DK_SCORE_JUMP 100
#define DK_SCORE_SMASH 300
#define DK_SCORE_GIRDER 100
#define DK_SCORE_HOME 800

/* ------------------------------------------------------------------ */
/* arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t rnd(dk_game *g) {
    g->rng = g->rng * 1103515245u + 12345u;
    return g->rng >> 16;
}

static int range(dk_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

/* the gear the words per minute put it in, applied to a distance a frame */
static int geared(const dk_game *g, int v) { return v * g->speed / 4; }

/* ------------------------------------------------------------------ */
/* the site                                                            */
/* ------------------------------------------------------------------ */

/*
 * A girder's surface under a column.  Written as a rise from the left end
 * rather than as a tilt about the middle because integer division truncates
 * towards zero, and a slope that changes its rounding at the middle of the
 * panel is a slope with a step in it - which a barrel rolls over and a
 * climber's feet stand a pixel inside.
 */
int dk_floor_y(int f, int x) {
    if (x < 0) {
        x = 0;
    }
    if (x > PM_PANEL - 1) {
        x = PM_PANEL - 1;
    }
    return DK_BASE - f * DK_RISE + DK_DIR(f) * ((x * DK_DROP) / PM_PANEL - DK_DROP / 2);
}

/*
 * The board, which is the same every level.  Two ladders to a gap and never
 * at the same end two gaps running, so the way up is a zigzag across the site
 * - and two of them broken, so which ladder is worth walking to depends on
 * which end the climber came up at rather than on which is nearer.
 */
const dk_ladder DK_LADDER[DK_LADDERS] = {
    {0, 112, 0}, {0, 196, 0}, {1, 36, 0},  {1, 140, 0},
    {1, 208, 1}, {2, 96, 0},  {2, 200, 0}, {3, 40, 0},
    {3, 152, 0}, {3, 216, 1}, {4, 112, 0}, {4, 192, 0},
};

/* one over each of the two girders the climb spends longest on */
const dk_pickup DK_HAMMER[DK_HAMMERS] = {{1, 76}, {3, 68}};

/* the arc, written down rather than worked out - see the header for why it is
 * flat in the middle, and note that it is over a barrel for ten of its twelve
 * frames, which is what makes the jump a move rather than a coin toss */
static const uint8_t DK_ARC[DK_JUMP_T + 1] = {0, 8, 13, 14, 14, 14, 14, 14, 13, 11, 8, 4, 0};

int dk_jump_dy(int t) {
    if (t <= 0 || t >= DK_JUMP_T) {
        return 0;
    }
    return DK_ARC[t];
}

/* the ladder in this gap nearest a column, or -1 - broken ones optional */
static int ladder_at(int floor, int x, bool climbable) {
    for (int i = 0; i < DK_LADDERS; i++) {
        if (DK_LADDER[i].gap != floor) {
            continue;
        }
        if (climbable && DK_LADDER[i].broken) {
            continue;
        }
        if (iabs((int)DK_LADDER[i].x - x) <= DK_MOUNT) {
            return i;
        }
    }
    return -1;
}

/* the climber may not walk off either end of a girder */
static int clamp_x(int x8) {
    if (x8 < DK_EDGE * DK_SUB) {
        return DK_EDGE * DK_SUB;
    }
    if (x8 > (PM_PANEL - 1 - DK_EDGE) * DK_SUB) {
        return (PM_PANEL - 1 - DK_EDGE) * DK_SUB;
    }
    return x8;
}

/* ------------------------------------------------------------------ */
/* barrels                                                             */
/* ------------------------------------------------------------------ */

static void put_on_floor(dk_barrel *b, int floor, int x8) {
    b->floor = (uint8_t)floor;
    b->x = (int16_t)x8;
    b->y = (int16_t)(dk_floor_y(floor, DK_PX(x8)) * DK_SUB);
    b->dir = (int8_t)DK_DIR(floor);
    b->state = DK_B_ROLL;
    b->t = b->n = 0;
    b->takes = 0;
}

static void throw_barrel(dk_game *g) {
    for (int i = 0; i < DK_BARRELS; i++) {
        dk_barrel *b = &g->barrel[i];
        if (b->state != DK_B_GONE) {
            continue;
        }
        put_on_floor(b, DK_FLOORS - 1, (DK_APE_X + DK_APE_W / 2) * DK_SUB);
        g->ape = DK_THROW_ANIM;
        return;
    }
}

/* down a ladder, or off the low end: both end on a known surface, so both are
 * interpolated to it rather than moved towards it */
static void begin_fall(dk_barrel *b, int to, int x8, int frames) {
    b->state = (uint8_t)(frames == DK_DROP_T ? DK_B_DROP : DK_B_LADDER);
    b->to = (uint8_t)to;
    b->x = (int16_t)x8;
    b->y0 = b->y;
    b->y1 = (int16_t)(dk_floor_y(to, DK_PX(x8)) * DK_SUB);
    b->t = 0;
    b->n = (uint8_t)frames;
}

static void step_barrel(dk_game *g, dk_barrel *b) {
    if (b->state == DK_B_GONE) {
        return;
    }

    if (b->state != DK_B_ROLL) {
        b->t++;
        b->y = (int16_t)(b->y0 + (b->y1 - b->y0) * (int)b->t / (int)b->n);
        if (b->t >= b->n) {
            put_on_floor(b, b->to, b->x);
        }
        return;
    }

    int v = geared(g, DK_ROLL);
    int was = DK_PX(b->x);
    b->x = (int16_t)(b->x + b->dir * v);
    int now = DK_PX(b->x);

    /*
     * The ladders.  A barrel makes its mind up DK_TELL pixels before one and
     * goes down it when it gets there, rather than deciding on the spot - see
     * the header: the decision is the one thing on this board nobody can
     * predict, so it happens where it can still be got out from under.  Both
     * are tested as the column is crossed rather than while it is over it: at
     * three pixels a frame a die rolled every frame would mean a barrel that
     * hardly ever passes a ladder at all.
     */
    if (b->floor > 0) {
        for (int i = 0; i < DK_LADDERS; i++) {
            int lx = DK_LADDER[i].x;
            if (DK_LADDER[i].gap != (int)b->floor - 1) {
                continue;
            }
            if (b->takes == i + 1 && (was < lx) != (now < lx)) {
                begin_fall(b, b->floor - 1, lx * DK_SUB, DK_LADDER_T);
                return;
            }
            int tell = lx - b->dir * DK_TELL;
            if ((was < tell) == (now < tell) || was == now) {
                continue;
            }
            if (range(g, 0, 2) == 0) {
                b->takes = (uint8_t)(i + 1);
            }
        }
    }

    if (now >= DK_EDGE && now <= PM_PANEL - 1 - DK_EDGE) {
        b->y = (int16_t)(dk_floor_y(b->floor, now) * DK_SUB);
        return;
    }

    /* over the low end: onto the girder below, or into the drum */
    if (b->floor == 0) {
        b->state = DK_B_GONE;
        return;
    }
    int edge = now < DK_EDGE ? DK_EDGE : PM_PANEL - 1 - DK_EDGE;
    begin_fall(b, b->floor - 1, edge * DK_SUB, DK_DROP_T);
}

/*
 * Where a barrel will be in t frames, as a girder and a column.  It is not a
 * simulation of the whole board: the die a barrel rolls at each ladder is not
 * predicted, because a third of a chance is not a fact and pricing it as one
 * gives a pilot that will not walk down a girder with a barrel two floors
 * above it.  Everything that is settled, though, is followed to the end - a
 * barrel that will go over the low end in nine frames is on the girder below
 * ten frames from now, rolling the other way, and it was pinning it at the
 * edge instead that killed this climber most often: the barrel it had cleared
 * arrived from a girder the pilot had stopped following.
 *
 * A barrel part way down a ladder counts as being on the girder it is landing
 * on for the whole of the fall.  That is deliberately pessimistic: standing at
 * the foot of a ladder with a barrel on it is exactly the position this pilot
 * used to walk into.
 */
static bool barrel_at(const dk_game *g, const dk_barrel *b, int t, int *floor, int *x,
                      bool *span) {
    int v = geared(g, DK_ROLL);
    int lo = DK_EDGE * DK_SUB, hi = (PM_PANEL - 1 - DK_EDGE) * DK_SUB;
    int fl, x8, dir;

    if (b->state == DK_B_GONE) {
        return false;
    }
    fl = b->floor;
    x8 = b->x;
    dir = b->dir;
    *span = false;

    if (b->state != DK_B_ROLL) {
        fl = b->to;
        dir = DK_DIR(fl);
        int rem = (int)b->n - (int)b->t;
        if (t <= rem) {
            *floor = fl;
            *x = DK_PX(x8);
            *span = b->state == DK_B_LADDER;
            return true;
        }
        t -= rem;
    }

    for (int takes = b->state == DK_B_ROLL ? b->takes : 0;;) {
        /* the ladder it has said it is going down, if it is still ahead of it */
        if (takes > 0 && fl > 0) {
            int lx8 = DK_LADDER[takes - 1].x * DK_SUB;
            int togo = dir < 0 ? x8 - lx8 : lx8 - x8;
            if (togo >= 0) {
                int frames = togo / v;
                if (t <= frames) {
                    *floor = fl;
                    *x = DK_PX(x8 + dir * v * t);
                    return true;
                }
                t -= frames + 1;
                x8 = lx8;
                fl--;
                dir = DK_DIR(fl);
                if (t <= DK_LADDER_T) {
                    *floor = fl;
                    *x = DK_PX(x8);
                    *span = true;
                    return true;
                }
                t -= DK_LADDER_T;
            }
            takes = 0;
            continue;
        }

        int room = dir < 0 ? x8 - lo : hi - x8;
        int frames = room / v;

        if (t <= frames) {
            *floor = fl;
            *x = DK_PX(x8 + dir * v * t);
            return true;
        }
        t -= frames + 1;
        if (fl == 0) {
            return false; /* into the drum, and no longer anybody's problem */
        }
        x8 = dir < 0 ? lo : hi;
        fl--;
        dir = DK_DIR(fl);
        if (t <= DK_DROP_T) {
            *floor = fl;
            *x = DK_PX(x8);
            return true;
        }
        t -= DK_DROP_T;
    }
}

static int barrels_on(const dk_game *g, int floor) {
    int n = 0;
    for (int i = 0; i < DK_BARRELS; i++) {
        if (g->barrel[i].state != DK_B_GONE && g->barrel[i].floor == floor) {
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* the climber                                                         */
/* ------------------------------------------------------------------ */

static void stand(dk_hero *h, int floor, int x8) {
    h->floor = (uint8_t)floor;
    h->x = (int16_t)clamp_x(x8);
    h->y = (int16_t)(dk_floor_y(floor, DK_PX(h->x)) * DK_SUB);
    h->state = DK_ST_WALK;
    h->t = 0;
    h->vx = 0;
}

static void place_hero(dk_game *g) {
    dk_hero *h = &g->hero;

    *h = (dk_hero){0};
    h->facing = 1;
    stand(h, 0, 40 * DK_SUB);
    g->reached = 0;
    g->aim = -1;
    g->fetch = 0;
    g->patient = 0;
}

static void reset_board(dk_game *g) {
    for (int i = 0; i < DK_BARRELS; i++) {
        g->barrel[i].state = DK_B_GONE;
    }
    for (int i = 0; i < DK_HAMMERS; i++) {
        g->hammer_up[i] = true;
    }
    g->throw_t = (uint16_t)(DK_THROW / 2);
    g->ape = 0;
    g->clock = DK_TIME;
    place_hero(g);
}

static void die(dk_game *g, dk_death why) {
    if (g->phase != DK_PLAY) {
        return;
    }
    g->why = (uint8_t)why;
    g->phase = DK_DYING;
    g->phase_timer = 0;
}

/* ten a girder, and only for one he has not stood on this climb */
static void credit_floor(dk_game *g) {
    if (g->hero.floor > g->reached) {
        g->reached = g->hero.floor;
        g->score += DK_SCORE_GIRDER;
        g->patient = 0;
    }
}

static void step_hero(dk_game *g) {
    dk_hero *h = &g->hero;

    if (h->hammer > 0) {
        h->hammer--;
    }

    if (h->state == DK_ST_CLIMB) {
        h->t++;
        h->y = (int16_t)(h->y0 + (h->y1 - h->y0) * (int)h->t / (int)h->n);
        if (h->t >= h->n) {
            stand(h, h->to, h->x);
            credit_floor(g);
        }
        return;
    }

    if (h->state == DK_ST_JUMP) {
        h->t++;
        h->x = (int16_t)clamp_x(h->x + h->vx);
        h->y = (int16_t)(dk_floor_y(h->floor, DK_PX(h->x)) * DK_SUB -
                         dk_jump_dy(h->t) * DK_SUB);
        if (h->t >= DK_JUMP_T) {
            stand(h, h->floor, h->x);
        }
        return;
    }

    if (h->vx != 0) {
        h->x = (int16_t)clamp_x(h->x + h->vx);
        h->step = (uint8_t)((h->step + 1) & 31);
    }
    h->y = (int16_t)(dk_floor_y(h->floor, DK_PX(h->x)) * DK_SUB);
}

/* ------------------------------------------------------------------ */
/* what touches what                                                   */
/* ------------------------------------------------------------------ */

static bool boxes_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return !(ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay);
}

/*
 * A barrel against the climber, in real pixels rather than in girders: a
 * barrel coming down a ladder is on neither floor while it does it, and the
 * climber halfway up the same ladder is exactly who it is coming for.
 */
static bool barrel_touches(const dk_game *g, const dk_barrel *b) {
    int bx = DK_PX(b->x), by = DK_PX(b->y);
    int hx = DK_PX(g->hero.x), hy = DK_PX(g->hero.y);

    return boxes_meet(hx - DK_HIT, hy - DK_HERO_H, 2 * DK_HIT, DK_HERO_H, bx - DK_BARREL_HIT,
                      by - DK_BARREL_H, 2 * DK_BARREL_HIT, DK_BARREL_H);
}

/* a barrel that went by underneath while he was over it, once per jump */
static void credit_jumps(dk_game *g) {
    dk_hero *h = &g->hero;

    if (h->state != DK_ST_JUMP) {
        h->cleared = 0;
        return;
    }
    for (int i = 0; i < DK_BARRELS; i++) {
        const dk_barrel *b = &g->barrel[i];
        if (b->state == DK_B_GONE || b->floor != h->floor || (h->cleared & (1u << i))) {
            continue;
        }
        if (iabs(DK_PX(b->x) - DK_PX(h->x)) < DK_HIT + DK_BARREL_HIT) {
            h->cleared |= (uint8_t)(1u << i);
            g->score += DK_SCORE_JUMP;
        }
    }
}

static void collide(dk_game *g) {
    dk_hero *h = &g->hero;

    for (int i = 0; i < DK_BARRELS; i++) {
        dk_barrel *b = &g->barrel[i];
        if (b->state == DK_B_GONE || !barrel_touches(g, b)) {
            continue;
        }
        if (h->hammer > 0) {
            b->state = DK_B_GONE;
            g->score += DK_SCORE_SMASH;
            continue;
        }
        die(g, DK_D_BARREL);
        return;
    }

    for (int i = 0; i < DK_HAMMERS; i++) {
        if (!g->hammer_up[i] || DK_HAMMER[i].floor != h->floor) {
            continue;
        }
        int hy = dk_floor_y(DK_HAMMER[i].floor, DK_HAMMER[i].x) - DK_HAMMER_UP;
        if (iabs(DK_PX(h->x) - (int)DK_HAMMER[i].x) <= DK_HAMMER_GRAB &&
            iabs(DK_PX(h->y) - DK_HERO_H - hy) <= DK_HAMMER_UP) {
            g->hammer_up[i] = false;
            h->hammer = DK_HAMMER_T;
            g->fetch = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* the pilot                                                           */
/* ------------------------------------------------------------------ */

/*
 * The prices.  A move that walks him under a barrel is struck off outright;
 * everything else is worth what it gains up the site, which is one girder
 * against however far along one he has to walk to get there.  DK_UP has to
 * beat the whole width of the panel or a climber at the wrong end of a girder
 * would rather stand still than walk to the ladder.
 */
#define DK_FATAL 1000000
#define DK_UP 600
#define DK_HOLD 6     /* how long standing or walking commits him */
#define DK_SETTLE 4   /* and how long after a landing must also be clear */
/* long enough that the longest plan there is - a whole ladder - is proved to
 * the end of it rather than to the end of the look-ahead */
#define DK_LOOK 34    /* how far ahead anything is priced at all */
/* what a threat he has time to answer is worth: less than the ground a
 * couple of steps covers, so it steers him rather than turning him round */
#define DK_TAIL 45
/* and what a jump costs, which has to sit under what standing in front of an
 * arriving barrel costs and over what walking a clear girder gains */
#define DK_JUMP_COST 40
#define DK_STICK 3    /* carrying on the way he is facing, to stop dithering */

/*
 * What a hammer is worth going to get.  Below the cost of walking the width
 * of a girder, because it is a detour and not the job: a pilot that valued it
 * higher fetched both of them every climb and spent the eight seconds it
 * bought standing on a girder it was not allowed to leave.
 */
#define DK_HAMMER_WORTH 70

/* what one barrel that could still come down the ladder he is about to climb
 * is worth waiting for.  Under DK_UP, so a climb under one still beats
 * standing still, and well over what a few pixels of walking costs */
#define DK_RISKY 220
#define DK_HAMMER_NEAR 80

enum {
    DK_A_STAND = 0,
    DK_A_LEFT,
    DK_A_RIGHT,
    DK_A_JUMP,
    DK_A_JUMP_L,
    DK_A_JUMP_R,
    DK_A_UP,
    DK_A_DOWN,
    DK_A_COUNT,
};

typedef struct {
    uint8_t kind;
    int8_t dx;      /* which way he goes, in walking speeds */
    int8_t to;      /* the girder it ends on */
    int16_t end_x;  /* eighths */
    int16_t hold_x; /* the column a climb happens at, in pixels */
    uint8_t n;      /* how long a climb takes, without the settling */
    uint8_t commit;
    uint8_t risky;  /* barrels that could turn down this ladder while he is on it */
    bool ok;
} dk_plan;

/*
 * Where he is aiming on a girder: her, a hammer he is about to walk past, or
 * the ladder he has settled on.  Deliberately not the nearest barrel while he
 * is armed: swinging at them is what the hammer is for and going to look for
 * them is not, and a pilot that chased them spent the whole four seconds
 * walking back the way it had come, on the girder it had picked the hammer up
 * on.  It was by some way the largest single thing this climber did with its
 * life.  Armed, he walks to the ladder as usual and breaks whatever comes to
 * meet him.
 */
static int target_x(const dk_game *g, int floor) {
    if (g->fetch > 0 && DK_HAMMER[g->fetch - 1].floor == floor) {
        return DK_HAMMER[g->fetch - 1].x;
    }
    if (floor >= DK_FLOORS - 1) {
        return DK_LADY_X;
    }
    if (g->aim >= 0 && DK_LADDER[g->aim].gap == floor) {
        return DK_LADDER[g->aim].x;
    }
    /* no aim yet for this girder: the nearer of its two usable ladders */
    int best = DK_PX(g->hero.x), near = 1 << 20;
    for (int i = 0; i < DK_LADDERS; i++) {
        if (DK_LADDER[i].gap != floor || DK_LADDER[i].broken) {
            continue;
        }
        int d = iabs((int)DK_LADDER[i].x - DK_PX(g->hero.x));
        if (d < near) {
            near = d;
            best = DK_LADDER[i].x;
        }
    }
    return best;
}

/*
 * How good a place this is to be.  One number, so that climbing, walking and
 * jumping are compared rather than each having a rule of its own: a girder is
 * worth DK_UP and, on it, being near what he is walking to is worth the
 * difference.  Everything the pilot does is the difference between two of
 * these, less what the move costs.
 */
static int worth(const dk_game *g, int floor, int x8) {
    return floor * DK_UP - iabs(DK_PX(x8) - target_x(g, floor));
}

/*
 * Where a plan puts him t frames in: which girders could reach him there, the
 * column, and where his feet are.  The feet rather than a girder, because that
 * is what the collision the pilot is trying to avoid is actually made of - a
 * climber halfway up a ladder is beside the girder above him and cannot be
 * touched by anything rolling along it, and pricing him as though he were
 * standing on it is what had him waiting at the foot of every ladder for a
 * gap in traffic two girders wide.
 */
static void plan_pose(const dk_game *g, const dk_plan *p, int t, int *floor, int *floor2,
                      int *x, int *feet) {
    const dk_hero *h = &g->hero;
    int v = geared(g, DK_WALK);

    *floor2 = -1;

    switch (p->kind) {
    case DK_A_UP:
    case DK_A_DOWN: {
        /* both girders are in reach of him on the way between them, and which
         * of them can actually touch him falls out of where his feet are */
        int n = p->n < 1 ? 1 : p->n;
        int lo = dk_floor_y(h->floor, p->hold_x), hi = dk_floor_y(p->to, p->hold_x);
        *floor = h->floor;
        *floor2 = p->to;
        *x = p->hold_x;
        *feet = t >= n ? hi : lo + (hi - lo) * t / n;
        return;
    }
    case DK_A_JUMP:
    case DK_A_JUMP_L:
    case DK_A_JUMP_R:
        *floor = h->floor;
        *x = DK_PX(clamp_x(h->x + p->dx * v * (t < DK_JUMP_T ? t : DK_JUMP_T)));
        *feet = dk_floor_y(h->floor, *x) - dk_jump_dy(t);
        return;
    default:
        *floor = h->floor;
        *x = DK_PX(clamp_x(h->x + p->dx * v * t));
        *feet = dk_floor_y(h->floor, *x);
        return;
    }
}

/*
 * What a plan costs.  He is wound forward frame by frame along the path the
 * step will actually take him - the same arithmetic, so the pose being priced
 * is the pose he will be in - and every barrel is asked where it will be at
 * that frame.  One that reaches him while he is still committed is fatal; one
 * that arrives after he could have changed his mind is worth less the further
 * off it is, which is what makes him prefer a girder that will be clear to one
 * that merely is.
 */
static int plan_cost(const dk_game *g, const dk_plan *p) {
    int worst = 0;

    for (int t = 1; t <= DK_LOOK; t++) {
        int floor, floor2, x, feet;
        plan_pose(g, p, t, &floor, &floor2, &x, &feet);

        /* a barrel meeting a hammer is a barrel, not a death */
        if (g->hero.hammer > t) {
            continue;
        }
        for (int i = 0; i < DK_BARRELS; i++) {
            int bf, bx;
            /*
             * A frame behind him, and that is not a rounding: a frame is the
             * barrels stepping, then this, then him stepping, then the two of
             * them being compared - so the pose he is in after t steps meets
             * the barrel that has taken t - 1.  Lining them both up on t
             * instead reads every barrel a step further on than the one he
             * will actually walk into, which is a hair over three pixels, and
             * three pixels is most of the difference between clearing a barrel
             * and landing on it.  It was the commonest death here by far.
             */
            bool span;
            if (!barrel_at(g, &g->barrel[i], t - 1, &bf, &bx, &span)) {
                continue;
            }
            if (bf != floor && bf != floor2) {
                continue;
            }
            if (iabs(bx - x) >= DK_HIT + DK_BARREL_HIT) {
                continue;
            }
            /* the same two boxes collide() will compare, one of them wound
             * forward - except for a barrel on a ladder, which is taken to
             * fill the whole of it rather than to be anywhere in particular */
            int by = dk_floor_y(bf, bx);
            if (!span && (feet <= by - DK_BARREL_H || feet - DK_HERO_H >= by)) {
                continue;
            }
            if (t <= p->commit) {
                /*
                 * Graded by when, not merely fatal.  Every so often every move
                 * there is kills him, and then which one he takes still
                 * matters: the step that dies last is the one a barrel might
                 * yet turn away from, and without the grading he took whichever
                 * of them gained the most ground - which was reliably the
                 * ladder he was standing at the foot of.
                 */
                return DK_FATAL - t * 1000;
            }
            /*
             * After the commitment it is worth very little, and deliberately:
             * it is a barrel he will still be standing there to see, and he
             * gets to decide again eleven more times before it arrives.
             * Pricing it steeply instead - the obvious thing, and what this
             * did first - buys a climber who backs away from every barrel on
             * the horizon, loses forty pixels of ground doing it and then
             * jumps the thing anyway.  It walked five times as far along a
             * girder as the girder is wide.
             */
            int cost = DK_TAIL * (DK_LOOK - t) / DK_LOOK;
            if (cost > worst) {
                worst = cost;
            }
        }
    }
    return worst;
}

/*
 * How many barrels could still turn down this ladder while he is on it.  A
 * climb is the one move he cannot take back, and the one thing on this board
 * that cannot be predicted is which ladder a barrel takes - it says so only
 * DK_TELL pixels ahead, which is four frames, and he needs twenty.  So a
 * barrel that has not yet reached the top of this ladder is counted rather
 * than predicted, and what it buys is that he waits for it to go by.
 */
static int crossers(const dk_game *g, int ladder, int window) {
    int lx = DK_LADDER[ladder].x, above = DK_LADDER[ladder].gap + 1;
    int v = geared(g, DK_ROLL);
    int n = 0;

    for (int i = 0; i < DK_BARRELS; i++) {
        const dk_barrel *b = &g->barrel[i];
        int bf, bx;
        bool span;

        if (b->state == DK_B_GONE || !barrel_at(g, b, 0, &bf, &bx, &span)) {
            continue;
        }
        if (bf != above) {
            continue;
        }
        /* upstream of it, and near enough to reach it while he is climbing */
        int togo = DK_DIR(above) > 0 ? lx - bx : bx - lx;
        if (togo >= -DK_TELL && togo <= window * v / DK_SUB) {
            n++;
        }
    }
    return n;
}

/* the plans he could take from where he is standing */
static int plans(const dk_game *g, dk_plan *out) {
    const dk_hero *h = &g->hero;
    int v = geared(g, DK_WALK);
    int n = 0;

    for (int a = 0; a < DK_A_COUNT; a++) {
        dk_plan p = {(uint8_t)a, 0, (int8_t)h->floor, h->x, DK_PX(h->x), 1, DK_HOLD, 0, true};

        switch (a) {
        case DK_A_STAND:
            break;
        case DK_A_LEFT:
        case DK_A_RIGHT:
            p.dx = (int8_t)(a == DK_A_LEFT ? -1 : 1);
            p.end_x = (int16_t)clamp_x(h->x + p.dx * v * DK_HOLD);
            break;
        case DK_A_JUMP:
        case DK_A_JUMP_L:
        case DK_A_JUMP_R:
            p.dx = (int8_t)(a == DK_A_JUMP_L ? -1 : a == DK_A_JUMP_R ? 1 : 0);
            p.end_x = (int16_t)clamp_x(h->x + p.dx * v * DK_JUMP_T);
            p.commit = DK_JUMP_T + DK_SETTLE;
            break;
        case DK_A_UP:
        case DK_A_DOWN: {
            /* the hammer is two hands full: it is the trade the arcade made,
             * and without it a pilot fetches one at the foot of every ladder */
            int gap = a == DK_A_UP ? h->floor : (int)h->floor - 1;
            int l = (gap < 0 || h->hammer > 0) ? -1 : ladder_at(gap, DK_PX(h->x), true);
            if (l < 0 || (a == DK_A_UP && h->floor + 1 >= DK_FLOORS)) {
                p.ok = false;
                break;
            }
            p.to = (int8_t)(a == DK_A_UP ? h->floor + 1 : h->floor - 1);
            p.hold_x = DK_LADDER[l].x;
            p.end_x = (int16_t)(DK_LADDER[l].x * DK_SUB);
            int rise = iabs(dk_floor_y(p.to, DK_LADDER[l].x) -
                            dk_floor_y(h->floor, DK_LADDER[l].x));
            p.n = (uint8_t)(rise * DK_SUB / geared(g, DK_CLIMB) + 1);
            p.commit = (uint8_t)(p.n + DK_SETTLE);
            p.risky = crossers(g, l, p.commit);
            break;
        }
        default:
            break;
        }
        if (p.ok) {
            out[n++] = p;
        }
    }
    return n;
}

/*
 * Whether to pick a hammer up.  Only one that is already on the way: on the
 * same side of him as the ladder he is walking to, and not much further off
 * than the ladder is.  Fetching one instead - walking to it because there
 * happen to be barrels about - is what a hammer looks like it is for and is
 * the most expensive thing this pilot ever did, because the five seconds it
 * then owes are spent on the girder the barrels were on rather than on the
 * one above it.
 *
 * Held as a decision rather than worked out afresh every frame, because the
 * test involves how many barrels are on the girder and that stops being true
 * the moment one of them rolls off the end - and a pilot that then turned
 * round spent the girder oscillating between the ladder and the hammer.
 */
static void pick_hammer(dk_game *g) {
    const dk_hero *h = &g->hero;

    if (g->fetch > 0) {
        const dk_pickup *p = &DK_HAMMER[g->fetch - 1];
        if (!g->hammer_up[g->fetch - 1] || p->floor != h->floor || h->hammer > 0) {
            g->fetch = 0;
        }
        return;
    }
    if (h->hammer > 0) {
        return;
    }
    for (int i = 0; i < DK_HAMMERS; i++) {
        if (!g->hammer_up[i] || DK_HAMMER[i].floor != h->floor) {
            continue;
        }
        int x = DK_PX(h->x), to = target_x(g, h->floor);
        int dh = (int)DK_HAMMER[i].x - x, dt = to - x;

        if (dh * dt < 0 || iabs(dh) > iabs(dt) + DK_HAMMER_NEAR) {
            continue; /* behind him, or a detour rather than a pick-up */
        }
        if (barrels_on(g, h->floor) >= 2) {
            g->fetch = (uint8_t)(i + 1);
            return;
        }
    }
}

/*
 * Which ladder to walk to.  The nearer of them, unless walking to it means
 * walking under something: what standing at its foot would cost is added to
 * the distance, so a ladder with a barrel on its way to it is worth going
 * round.
 *
 * Chosen once, when he arrives on a girder, and then held.  Choosing again
 * every frame looks like the more responsive thing to do and is not: the
 * traffic that makes one ladder look worse than the other has rolled past by
 * the time he has walked ten pixels towards the other one, and then the first
 * one wins again.  He covered a panel's width of walking per girder that way,
 * all of it between two ladders and none of it up.  It is reconsidered only
 * when he has been getting nowhere for DK_RETHINK frames, which is the case
 * the latch would otherwise leave him stuck in.
 */
#define DK_RETHINK 90

static void pick_aim(dk_game *g) {
    const dk_hero *h = &g->hero;
    int best = -1, best_cost = 1 << 30;

    if (h->floor >= DK_FLOORS - 1) {
        g->aim = -1;
        return;
    }
    if (g->aim >= 0 && DK_LADDER[g->aim].gap == h->floor && g->patient < DK_RETHINK) {
        return;
    }
    g->patient = 0;
    for (int i = 0; i < DK_LADDERS; i++) {
        if (DK_LADDER[i].gap != h->floor || DK_LADDER[i].broken) {
            continue;
        }
        int lx = DK_LADDER[i].x;
        int cost = iabs(lx - DK_PX(h->x));

        /* what is waiting at the foot of it when he could get there */
        int walk = cost * DK_SUB / geared(g, DK_WALK);
        for (int b = 0; b < DK_BARRELS; b++) {
            int bf, bx;
            bool span;
            if (!barrel_at(g, &g->barrel[b], walk < DK_LOOK ? walk : DK_LOOK, &bf, &bx,
                           &span)) {
                continue;
            }
            if (bf == h->floor && iabs(bx - lx) < 3 * DK_HIT) {
                cost += 60;
            }
        }
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    g->aim = (int8_t)best;
}

static void think(dk_game *g) {
    dk_hero *h = &g->hero;
    dk_plan list[DK_A_COUNT];

    pick_hammer(g);
    pick_aim(g);

    int n = plans(g, list);
    int here = worth(g, h->floor, h->x);
    int best = -(1 << 30), best_i = 0;

    for (int i = 0; i < n; i++) {
        const dk_plan *p = &list[i];
        int value = worth(g, p->to, p->end_x) - here - plan_cost(g, p);

        if (p->kind == DK_A_JUMP || p->kind == DK_A_JUMP_L || p->kind == DK_A_JUMP_R) {
            value -= DK_JUMP_COST;
        }
        /*
         * Priced rather than forbidden: often enough every ladder off a girder
         * has something above it, and then going up under one of them is
         * still better than standing on a girder with barrels arriving.
         */
        value -= p->risky * DK_RISKY;
        if (p->kind == DK_A_UP && g->fetch > 0) {
            value -= DK_HAMMER_WORTH; /* the hammer is on this girder, not the next */
        }
        if (p->dx != 0 && p->dx == h->facing) {
            value += DK_STICK;
        }
        if (value > best) {
            best = value;
            best_i = i;
        }
    }
    const dk_plan *p = &list[best_i];
    int v = geared(g, DK_WALK);

    switch (p->kind) {
    case DK_A_STAND:
        h->vx = 0;
        break;
    case DK_A_LEFT:
    case DK_A_RIGHT:
        h->vx = (int8_t)(p->dx * v);
        h->facing = p->dx;
        break;
    case DK_A_JUMP:
    case DK_A_JUMP_L:
    case DK_A_JUMP_R:
        h->state = DK_ST_JUMP;
        h->t = 0;
        h->vx = (int8_t)(p->dx * v);
        h->cleared = 0;
        if (p->dx != 0) {
            h->facing = p->dx;
        }
        break;
    default: {
        h->state = DK_ST_CLIMB;
        h->t = 0;
        h->vx = 0;
        h->x = (int16_t)(p->hold_x * DK_SUB);
        h->to = (uint8_t)p->to;
        h->y0 = (int16_t)(dk_floor_y(h->floor, p->hold_x) * DK_SUB);
        h->y1 = (int16_t)(dk_floor_y(p->to, p->hold_x) * DK_SUB);
        int rise = iabs(h->y1 - h->y0);
        int step = geared(g, DK_CLIMB);
        h->n = (uint8_t)(rise / step + 1);
        /* the same length plans() proved the climb over, or he is on the
         * ladder for longer than the gap he was shown to have */
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* the frame                                                           */
/* ------------------------------------------------------------------ */

static void next_level(dk_game *g) {
    if (g->level < 99) {
        g->level++;
    }
    reset_board(g);
}

static void step_phase(dk_game *g) {
    g->phase_timer++;

    if (g->phase == DK_DYING && g->phase_timer >= DK_DYING_T) {
        if (g->lives > 1) {
            g->lives--;
            g->phase = DK_PLAY;
            g->phase_timer = 0;
            reset_board(g);
        } else {
            g->phase = DK_OVER;
            g->phase_timer = 0;
        }
        return;
    }
    if (g->phase == DK_WON && g->phase_timer >= DK_WON_T) {
        g->phase = DK_PLAY;
        g->phase_timer = 0;
        next_level(g);
        return;
    }
    if (g->phase == DK_OVER && g->phase_timer >= DK_OVER_T) {
        uint32_t seed = g->rng;
        dk_init(g, seed);
        g->redraw = true;
    }
}

void dk_step(dk_game *g) {
    if (g->phase != DK_PLAY) {
        step_phase(g);
        return;
    }

    if (g->clock > 0) {
        g->clock--;
    } else {
        die(g, DK_D_TIME);
        return;
    }

    if (g->ape > 0) {
        g->ape--;
    }
    if (g->throw_t > 0) {
        g->throw_t--;
    } else {
        int every = DK_THROW - DK_THROW_STEP * (int)(g->level - 1);
        if (every < DK_THROW_MIN) {
            every = DK_THROW_MIN;
        }
        g->throw_t = (uint16_t)(every * 4 / g->speed + range(g, 0, 6));
        throw_barrel(g);
    }

    for (int i = 0; i < DK_BARRELS; i++) {
        step_barrel(g, &g->barrel[i]);
    }

    if (g->hero.state == DK_ST_WALK) {
        think(g);
    }
    step_hero(g);
    credit_jumps(g);
    collide(g);

    if (g->phase != DK_PLAY) {
        return;
    }

    if (g->hero.floor == DK_FLOORS - 1 && g->hero.state == DK_ST_WALK &&
        iabs(DK_PX(g->hero.x) - DK_LADY_X) <= DK_REACH) {
        g->score += DK_SCORE_HOME + g->clock / 6;
        g->phase = DK_WON;
        g->phase_timer = 0;
        return;
    }

    g->patient++;
}

void dk_init(dk_game *g, uint32_t seed) {
    *g = (dk_game){0};
    g->rng = seed | 1u;
    g->speed = 4;
    g->lives = 3;
    g->level = 1;
    g->phase = DK_PLAY;
    reset_board(g);
    g->redraw = true;
}

void dk_set_speed(dk_game *g, uint8_t gear) {
    g->speed = gear < 3 ? 3 : (gear > 5 ? 5 : gear);
}

/* ------------------------------------------------------------------ */
/* what the renderer asks                                              */
/* ------------------------------------------------------------------ */

int dk_spin_age(const dk_game *g) {
    return g->phase == DK_DYING ? (int)g->phase_timer : -1;
}

bool dk_hero_visible(const dk_game *g) {
    if (g->phase == DK_OVER) {
        return false;
    }
    if (g->phase != DK_DYING) {
        return true;
    }
    return (g->phase_timer / 4) % 2 == 0;
}

const char *dk_banner(const dk_game *g) {
    if (g->phase == DK_OVER) {
        return "GAME OVER";
    }
    if (g->phase == DK_WON) {
        return "SAVED";
    }
    return NULL;
}
