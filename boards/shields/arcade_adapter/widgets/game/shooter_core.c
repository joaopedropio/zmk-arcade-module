/*
 * Space Shooter dongle - game core (portable).
 *
 * Meteors drift across from the edges and break into smaller meteors; the
 * spawner keeps them coming as fast as they go, so there is nothing to clear
 * and nothing to count.  Nobody is playing it: the ship works out which meteor
 * its shot can catch soonest, turns the nose onto the point that meteor will
 * be at by the time the shot arrives, and fires when the two agree - unless
 * something is about to reach it, in which case turning away and thrusting
 * outranks the shot.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shooter_core.h"

const uint8_t ss_rock_r[SS_SIZES] = {6, 10, 15};

/* how long each thing lasts, in frames */
#define SS_DEAD_FRAMES  30
#define SS_OVER_FRAMES  90
#define SS_INVULN       60
#define SS_POWER_FRAMES 400
#define SS_BLAST_AGES   6

#define SS_COOLDOWN       9
#define SS_COOLDOWN_RAPID 3
#define SS_SHOT_SPEED     (7 * SS_SUB)
#define SS_SHOT_LIFE      40           /* 280 pixels, more than the diagonal */
#define SS_SPREAD_ANGLE   12           /* about 17 degrees either side */

/*
 * Flight.  Thrust is deliberately weak against the drag: a ship that could
 * reach its top speed in three frames would spend the animation darting, and
 * one with no drag at all would end up pinned against an edge with nothing to
 * push it off.  What this pair gives is a ship that leans into a move and
 * coasts out of it.
 */
#define SS_THRUST    3                 /* eighths per frame, per frame */
#define SS_DRAG      48                /* a forty-eighth of the speed per frame */
#define SS_MAX_SPEED (3 * SS_SUB)

/*
 * A stalemate would leave the panel showing the same meteors until the dongle
 * is unplugged, so after this many frames without a hit the nearest one burns
 * up on its own.  The same bargain pacman_core.c's `hungry` makes, and it
 * should never fire in a soak.
 */
#define SS_PATIENCE 900

#define SS_LIVES 3

/* ------------------------------------------------------------------ */
/* the arithmetic nothing else here has                                */
/* ------------------------------------------------------------------ */

static int iabs(int v) { return v < 0 ? -v : v; }

/* sin over the first quarter turn, scaled by 1024; the rest is reflection */
static const int16_t SIN_Q10[65] = {
       0,   25,   50,   75,  100,  125,  150,  175,
     200,  224,  249,  273,  297,  321,  345,  369,
     392,  415,  438,  460,  483,  505,  526,  548,
     569,  590,  610,  630,  650,  669,  688,  706,
     724,  742,  759,  775,  792,  807,  822,  837,
     851,  865,  878,  891,  903,  915,  926,  936,
     946,  955,  964,  972,  980,  987,  993,  999,
    1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024,
};

int ss_sin(uint8_t angle) {
    int i = angle & 63;
    switch (angle >> 6) {
    case 0:
        return SIN_Q10[i];
    case 1:
        return SIN_Q10[64 - i];
    case 2:
        return -SIN_Q10[i];
    default:
        return -SIN_Q10[64 - i];
    }
}

int ss_cos(uint8_t angle) { return ss_sin((uint8_t)(angle + 64)); }

/* atan(i / 32) in the same 1/256 of a turn, for the first eighth of one */
static const uint8_t ATAN_Q8[33] = {
     0,  1,  3,  4,  5,  6,  8,  9, 10, 11, 12,
    13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 25, 26, 27, 28, 29, 29, 30, 31, 31, 32,
};

/*
 * Which way something lies, to the nearest degree and a half.  The table
 * covers the first eighth of a turn - where the ratio of the short side to the
 * long one runs 0 to 1 - and the other seven eighths are that answer
 * reflected, which is what keeps a whole circle in thirty-three bytes.
 */
uint8_t ss_angle_of(int x, int y) {
    int ax = iabs(x), ay = iabs(y);
    uint8_t base;

    if (ax == 0 && ay == 0) {
        return 0;
    }
    if (ax >= ay) {
        base = ATAN_Q8[(ay * 32 + ax / 2) / ax];
        if (y >= 0) {
            return x >= 0 ? base : (uint8_t)(128 - base);
        }
        return x >= 0 ? (uint8_t)(0 - base) : (uint8_t)(128 + base);
    }
    base = ATAN_Q8[(ax * 32 + ay / 2) / ay];
    if (y >= 0) {
        return x >= 0 ? (uint8_t)(64 - base) : (uint8_t)(64 + base);
    }
    return x >= 0 ? (uint8_t)(192 + base) : (uint8_t)(192 - base);
}

/* the turn from one angle to another: however many units the short way round */
static int angle_err(uint8_t from, uint8_t to) {
    return (int)(int8_t)(uint8_t)(to - from);
}

static int isqrt(int32_t v) {
    int32_t rest = v, root = 0, bit = 1L << 30;

    if (v <= 0) {
        return 0;
    }
    while (bit > rest) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (rest >= root + bit) {
            rest -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (int)root;
}

static uint32_t rnd(ss_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int range(ss_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------------ */
/* how much rock is on the panel                                       */
/* ------------------------------------------------------------------ */

/*
 * What a meteor is worth to the spawner, which is roughly what it costs the
 * renderer: a big one covers about five times the pixels of a small one, so
 * counting in these rather than in rocks keeps a frame about the same price
 * whether the panel is holding four big ones or a dozen small ones.
 */
static const uint8_t ROCK_WEIGHT[SS_SIZES] = {1, 2, 5};
static const uint16_t ROCK_SCORE[SS_SIZES] = {100, 50, 20};

/*
 * How hard it is now.  There are no waves to number, so the difficulty rides
 * on the score instead - more rock on the panel, and moving faster - and it
 * stops climbing well before the ship stops coping, because this is something
 * to glance at rather than something to win.
 */
static int pressure(const ss_game *g) {
    int p = (int)(g->score / 4000u);
    return p > 5 ? 5 : p;
}

static int want_weight(const ss_game *g) { return 8 + 2 * pressure(g); }

static int rock_weight(const ss_game *g) {
    int total = 0;
    for (int i = 0; i < SS_ROCKS; i++) {
        if (g->rocks[i].alive) {
            total += ROCK_WEIGHT[g->rocks[i].size];
        }
    }
    return total;
}

static int rocks_alive(const ss_game *g) {
    int n = 0;
    for (int i = 0; i < SS_ROCKS; i++) {
        n += g->rocks[i].alive ? 1 : 0;
    }
    return n;
}

static ss_rock *free_rock(ss_game *g) {
    for (int i = 0; i < SS_ROCKS; i++) {
        if (!g->rocks[i].alive) {
            return &g->rocks[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* meteors                                                             */
/* ------------------------------------------------------------------ */

/*
 * A new meteor comes in from just outside one of the four edges, aimed at a
 * point somewhere in the middle of the panel rather than straight across:
 * aimed straight they arrive in lanes, and a lane is a thing the eye picks out
 * and then stops watching.
 */
static void spawn_rock(ss_game *g) {
    ss_rock *r = free_rock(g);
    if (r == NULL) {
        return;
    }
    int rad = ss_rock_r[SS_BIG];
    int along = range(g, 0, ARC_PANEL - 1);
    int x, y;

    switch (range(g, 0, 3)) {
    case 0:
        x = along;
        y = -rad - 2;
        break;
    case 1:
        x = along;
        y = ARC_PANEL + rad + 2;
        break;
    case 2:
        x = -rad - 2;
        y = along;
        break;
    default:
        x = ARC_PANEL + rad + 2;
        y = along;
        break;
    }

    int aim_x = range(g, ARC_PANEL / 4, 3 * ARC_PANEL / 4);
    int aim_y = range(g, ARC_PANEL / 4, 3 * ARC_PANEL / 4);
    uint8_t heading = ss_angle_of(aim_x - x, aim_y - y);
    int speed = 4 + pressure(g) + range(g, 0, 3);

    r->alive = true;
    r->size = SS_BIG;
    r->shape = (uint8_t)range(g, 0, 3);
    r->spin = (uint8_t)range(g, 0, 3);
    r->x = (int16_t)(x * SS_SUB);
    r->y = (int16_t)(y * SS_SUB);
    r->vx = (int16_t)(speed * ss_cos(heading) / 1024);
    r->vy = (int16_t)(speed * ss_sin(heading) / 1024);
}

static void blast_at(ss_game *g, int x, int y) {
    ss_blast *pick = &g->blasts[0];

    for (int i = 0; i < SS_BLASTS; i++) {
        if (!g->blasts[i].alive) {
            pick = &g->blasts[i];
            break;
        }
        if (g->blasts[i].age > pick->age) {
            pick = &g->blasts[i];
        }
    }
    pick->alive = true;
    pick->age = 0;
    pick->x = (int16_t)x;
    pick->y = (int16_t)y;
}

/*
 * A pickup now and then rather than on any schedule: there are no waves to put
 * one between, so one meteor in twenty-five leaves one behind.  Often enough
 * that one is usually running before long, rare enough that seeing it drop is
 * still an event.
 */
static void maybe_drop(ss_game *g, int x, int y) {
    if (g->drop.alive || range(g, 0, 24) != 0) {
        return;
    }
    uint8_t heading = (uint8_t)range(g, 0, 255);

    g->drop.alive = true;
    g->drop.kind = (uint8_t)range(g, SS_P_RAPID, SS_POWERS - 1);
    g->drop.x = (int16_t)x;
    g->drop.y = (int16_t)y;
    g->drop.vx = (int16_t)(5 * ss_cos(heading) / 1024);
    g->drop.vy = (int16_t)(5 * ss_sin(heading) / 1024);
}

/*
 * Breaking a meteor: two of the size below, thrown out sideways to whatever it
 * was doing so the pair does not simply carry on as one.  A small one leaves
 * nothing, and the spawner sees the weight go and sends another in.
 */
static void break_rock(ss_game *g, ss_rock *r) {
    int x = r->x, y = r->y, vx = r->vx, vy = r->vy;
    uint8_t size = r->size;

    g->score += ROCK_SCORE[size];
    g->patient = 0;
    r->alive = false;
    blast_at(g, SS_PX(x), SS_PX(y));
    maybe_drop(g, x, y);

    if (size == SS_SMALL) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        ss_rock *c = free_rock(g);
        if (c == NULL) {
            return;
        }
        int kick = range(g, 3, 6);
        int sign = i == 0 ? 1 : -1;
        /* square across the parent's course, one child either side of it */
        uint8_t away = ss_angle_of(-vy * sign, vx * sign);

        c->alive = true;
        c->size = (uint8_t)(size - 1);
        c->shape = (uint8_t)range(g, 0, 3);
        c->spin = (uint8_t)range(g, 0, 3);
        c->x = (int16_t)x;
        c->y = (int16_t)y;
        c->vx = (int16_t)(vx + kick * ss_cos(away) / 1024);
        c->vy = (int16_t)(vy + kick * ss_sin(away) / 1024);
    }
}

/* ------------------------------------------------------------------ */
/* the ship's own mind                                                 */
/* ------------------------------------------------------------------ */

/*
 * Where a shot fired now would meet a meteor.  The time is found by going
 * round three times rather than by solving the quadratic: start with how long
 * a shot would take to reach where the meteor is, see where the meteor has got
 * to by then, and try again.  It settles well inside a pixel for anything
 * moving as slowly as these do, and it costs three square roots instead of one
 * plus a discriminant that can come out negative.
 */
static int intercept(const ss_game *g, const ss_rock *r, int *ix, int *iy) {
    int rx = r->x - g->ship.x, ry = r->y - g->ship.y;
    int t = isqrt(rx * rx + ry * ry) / SS_SHOT_SPEED;

    for (int i = 0; i < 3; i++) {
        int px = rx + (int)r->vx * t, py = ry + (int)r->vy * t;
        t = isqrt(px * px + py * py) / SS_SHOT_SPEED;
    }
    *ix = rx + (int)r->vx * t;
    *iy = ry + (int)r->vy * t;
    return t;
}

/*
 * How far off a meteor has to break for the pieces to be somebody else's
 * problem.  Shooting one that is nearly on top of the ship is what killed
 * most of the early runs: a medium one comes apart into two small ones that
 * inherit its course and are thrown out sideways from wherever it was, so
 * breaking it at arm's length is handing yourself two faster meteors already
 * inside the distance you would need to dodge them.  Close ones get flown
 * around instead, which is what a person playing this learns in a minute.
 */
#define SS_KEEP_OFF 36

/*
 * The meteor worth pointing at: the one a shot could reach soonest, out of
 * those it could reach at all and far enough out to be worth breaking.
 * Working outwards from the nearest is also what keeps the ship dealing with
 * its own neighbourhood rather than sniping across the panel while something
 * closes on it.
 */
static const ss_rock *best_target(const ss_game *g, uint8_t *heading) {
    const ss_rock *best = NULL;
    int best_t = SS_SHOT_LIFE;

    for (int i = 0; i < SS_ROCKS; i++) {
        const ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int ix, iy;
        int t = intercept(g, r, &ix, &iy);
        if (t >= best_t || isqrt(ix * ix + iy * iy) < SS_KEEP_OFF * SS_SUB) {
            continue;
        }
        best = r;
        best_t = t;
        *heading = ss_angle_of(ix, iy);
    }
    return best;
}

/*
 * Whatever is closest, and how much room is left between it and the hull.  In
 * whole pixels, because that is the number the rest of the pilot compares
 * against distances it can picture.  `closing` asks for the nearest meteor
 * that is actually coming this way: one already going past is not worth
 * turning round for, and half the panel's meteors are on their way out.
 */
static const ss_rock *closest(const ss_game *g, int *gap, bool closing) {
    const ss_rock *near = NULL;
    int least = 1 << 20;

    for (int i = 0; i < SS_ROCKS; i++) {
        const ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int dx = r->x - g->ship.x, dy = r->y - g->ship.y;
        /*
         * Closing is about the two courses, not the meteor's alone.  A meteor
         * drifting away is still a collision if the ship is coasting into it
         * faster, and leaving the ship's own speed out of this is what let it
         * fly into perfectly harmless rocks.
         */
        int wx = r->vx - g->ship.vx, wy = r->vy - g->ship.vy;
        if (closing && wx * dx + wy * dy >= 0) {
            continue;
        }
        int px = SS_PX(dx), py = SS_PX(dy);
        int d = isqrt(px * px + py * py) - ss_rock_r[r->size] - SS_SHIP_R;
        if (d < least) {
            least = d;
            near = r;
        }
    }
    *gap = least;
    return near;
}

/*
 * How near a meteor has to be before the ship stops shooting and starts
 * flying.  It has to cover swinging the nose round - up to sixteen frames at
 * the turn rate below - and then the eight or so the thrust needs to build,
 * during which the meteor keeps coming.  Anything tighter and the ship starts
 * losing races it began winning.
 */
#define SS_DANGER 58

/*
 * How close to a wall it will drift before it turns back in.  Not a pull
 * towards the middle, which is where the meteors are all aimed: what the ship
 * needs is room on every side, and the middle is only one of the places that
 * has any.
 */
#define SS_EDGE 45

/*
 * How long a dodge is held before it is thought about again.  Long enough to
 * clear the meteor at the speed the ship can reach, short enough that a second
 * one arriving is answered rather than flown into.
 */
#define SS_EVADE_HOLD 16

/*
 * Which way to go to get out from under a meteor.  Straight away from it is
 * the answer that feels right and is wrong - it is running down the track the
 * meteor is already on, and it stays under it the whole way.  Sideways to the
 * meteor's own course clears it in a fraction of the distance, so what this
 * returns is the part of the offset between the two that is square to that
 * course, and the ship is already on the correct side of it.
 */
static uint8_t dodge_angle(const ss_game *g, const ss_rock *r) {
    int32_t wx = r->vx - g->ship.vx, wy = r->vy - g->ship.vy;
    int32_t rx = g->ship.x - r->x, ry = g->ship.y - r->y;
    int32_t ww = wx * wx + wy * wy;
    int32_t px = rx, py = ry;

    if (ww > 0) {
        int32_t dot = rx * wx + ry * wy;
        px = rx - wx * dot / ww;
        py = ry - wy * dot / ww;
    }
    /*
     * Dead in line with it, where the sideways offset is too small to point
     * anywhere useful: either side clears it, so take the one the meteor's
     * own course names and stop thinking about it.
     */
    if (px * px + py * py < 4 * SS_SUB * SS_SUB) {
        px = -wy;
        py = wx;
    }
    return ss_angle_of((int)px, (int)py);
}

/*
 * Whether a shot fired along this heading lands on anything, walked forward a
 * few frames at a time.  Cheaper than it looks - a handful of distance tests
 * per meteor - and unlike an angular tolerance it is right about a big meteor
 * close by and a small one across the panel at the same time.
 */
static bool shot_lands(const ss_game *g, uint8_t heading) {
    int vx = SS_SHOT_SPEED * ss_cos(heading) / 1024;
    int vy = SS_SHOT_SPEED * ss_sin(heading) / 1024;

    for (int t = 2; t <= SS_SHOT_LIFE; t += 3) {
        int bx = SS_PX(g->ship.x + vx * t), by = SS_PX(g->ship.y + vy * t);
        if (bx < 0 || bx >= ARC_PANEL || by < 0 || by >= ARC_PANEL) {
            return false; /* it left the panel before it reached anything */
        }
        int ox = bx - SS_PX(g->ship.x), oy = by - SS_PX(g->ship.y);
        if (ox * ox + oy * oy < SS_KEEP_OFF * SS_KEEP_OFF) {
            continue; /* too close to break something; keep walking */
        }
        for (int i = 0; i < SS_ROCKS; i++) {
            const ss_rock *r = &g->rocks[i];
            if (!r->alive) {
                continue;
            }
            int dx = bx - SS_PX(r->x + r->vx * t), dy = by - SS_PX(r->y + r->vy * t);
            int rad = ss_rock_r[r->size] + 2;
            if (dx * dx + dy * dy <= rad * rad) {
                return true;
            }
        }
    }
    return false;
}

static void fire(ss_game *g) {
    int fired = 0;
    int want = g->power == SS_P_SPREAD ? 3 : 1;

    for (int i = 0; i < SS_SHOTS && fired < want; i++) {
        if (g->shots[i].alive) {
            continue;
        }
        uint8_t a = (uint8_t)(g->ship.angle + (want == 1 ? 0 : (fired - 1) * SS_SPREAD_ANGLE));
        g->shots[i].alive = true;
        g->shots[i].life = SS_SHOT_LIFE;
        /* out of the nose rather than the middle, or a shot's first frame is
         * drawn inside the hull that fired it */
        g->shots[i].x = (int16_t)(g->ship.x + SS_NOSE * SS_SUB * ss_cos(a) / 1024);
        g->shots[i].y = (int16_t)(g->ship.y + SS_NOSE * SS_SUB * ss_sin(a) / 1024);
        g->shots[i].vx = (int16_t)(SS_SHOT_SPEED * ss_cos(a) / 1024);
        g->shots[i].vy = (int16_t)(SS_SHOT_SPEED * ss_sin(a) / 1024);
        fired++;
    }
    g->cooldown = g->power == SS_P_RAPID ? SS_COOLDOWN_RAPID : SS_COOLDOWN;
}

/*
 * One frame of flying it.  Three things want the nose, and they get it in this
 * order: away from a meteor that is about to arrive, back towards the middle
 * when it has drifted off, and onto whatever it means to shoot.  Only the
 * first two ever light the engine - coasting is free, and thrusting is what
 * puts the ship somewhere it did not choose.
 *
 * Firing is decided apart from all of that, on where the nose actually ended
 * up: a ship that has just turned to run will still take a shot if a shot
 * happens to be there, which is both what a person would do and what keeps it
 * shooting while it manoeuvres.
 */
static void pilot(ss_game *g) {
    int gap;
    const ss_rock *near = closest(g, &gap, true);
    int px = SS_PX(g->ship.x), py = SS_PX(g->ship.y);
    int cx = px - ARC_PANEL / 2, cy = py - ARC_PANEL / 2;
    bool near_wall = px < SS_EDGE || px > ARC_PANEL - SS_EDGE || py < SS_EDGE ||
                     py > ARC_PANEL - SS_EDGE;

    uint8_t want = g->ship.angle;
    bool burn = false;

    if (near != NULL && gap < SS_DANGER && g->power != SS_P_SHIELD) {
        /* held for a while once chosen, so the ship commits to one side */
        if (g->evade_left == 0) {
            g->evade = dodge_angle(g, near);
            g->evade_left = SS_EVADE_HOLD;
        }
        want = g->evade;
        burn = true;
    } else {
        g->evade_left = 0;
    }

    if (burn) {
        /* nothing else gets a say while it is getting out of the way */
    } else if (g->drop.alive) {
        /* a pickup drifts off the panel if it is not chased, and there is no
         * wave coming round to hand out another */
        want = ss_angle_of(g->drop.x - g->ship.x, g->drop.y - g->ship.y);
        burn = true;
    } else if (near_wall) {
        want = ss_angle_of(-cx, -cy);
        burn = true;
    } else {
        uint8_t aim;
        if (best_target(g, &aim) != NULL) {
            want = aim;
        }
    }

    int turn = angle_err(g->ship.angle, want);
    int rate = 6 + g->speed;
    if (turn > rate) {
        turn = rate;
    }
    if (turn < -rate) {
        turn = -rate;
    }
    g->ship.angle = (uint8_t)(g->ship.angle + turn);

    /* thrust only once the nose is roughly where the burn was wanted */
    g->ship.thrusting = burn && iabs(angle_err(g->ship.angle, want)) < 32;
    if (g->ship.thrusting) {
        g->ship.vx = (int16_t)(g->ship.vx + SS_THRUST * ss_cos(g->ship.angle) / 1024);
        g->ship.vy = (int16_t)(g->ship.vy + SS_THRUST * ss_sin(g->ship.angle) / 1024);
    }

    if (g->cooldown == 0 && shot_lands(g, g->ship.angle)) {
        fire(g);
    }
}

/* ------------------------------------------------------------------ */
/* one frame                                                           */
/* ------------------------------------------------------------------ */

static void move_ship(ss_game *g) {
    g->ship.vx = (int16_t)(g->ship.vx - g->ship.vx / SS_DRAG);
    g->ship.vy = (int16_t)(g->ship.vy - g->ship.vy / SS_DRAG);

    int speed = isqrt((int)g->ship.vx * g->ship.vx + (int)g->ship.vy * g->ship.vy);
    if (speed > SS_MAX_SPEED) {
        g->ship.vx = (int16_t)((int)g->ship.vx * SS_MAX_SPEED / speed);
        g->ship.vy = (int16_t)((int)g->ship.vy * SS_MAX_SPEED / speed);
    }

    g->ship.x = (int16_t)(g->ship.x + g->ship.vx);
    g->ship.y = (int16_t)(g->ship.y + g->ship.vy);

    /*
     * The panel is the edge of the world for the ship alone.  Meteors come and
     * go across it, but a ship that wrapped would vanish from under whatever
     * was chasing it and turn up somewhere else, which reads as a dropped
     * frame rather than as a move.  So it stops at the wall and loses the
     * speed it was carrying into it, and the pilot's pull back towards the
     * middle is what keeps it from sitting there.
     */
    int lo = SS_HULL_R * SS_SUB;
    int hi = (ARC_PANEL - 1 - SS_HULL_R) * SS_SUB;
    if (g->ship.x < lo) {
        g->ship.x = (int16_t)lo;
        g->ship.vx = 0;
    }
    if (g->ship.x > hi) {
        g->ship.x = (int16_t)hi;
        g->ship.vx = 0;
    }
    if (g->ship.y < lo) {
        g->ship.y = (int16_t)lo;
        g->ship.vy = 0;
    }
    if (g->ship.y > hi) {
        g->ship.y = (int16_t)hi;
        g->ship.vy = 0;
    }
}

/* whether something at x, y in eighths has left the panel by more than margin */
static bool gone(int x, int y, int margin) {
    return x < -margin * SS_SUB || x > (ARC_PANEL + margin) * SS_SUB ||
           y < -margin * SS_SUB || y > (ARC_PANEL + margin) * SS_SUB;
}

static void move_rocks(ss_game *g) {
    for (int i = 0; i < SS_ROCKS; i++) {
        ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        r->x = (int16_t)(r->x + r->vx);
        r->y = (int16_t)(r->y + r->vy);

        /* one that has crossed and left is simply gone; the spawner notices */
        if (gone(r->x, r->y, ss_rock_r[r->size] + 4)) {
            r->alive = false;
            continue;
        }
        if ((g->frame & 7) == 0) {
            r->spin = (uint8_t)((r->spin + 1) & 3);
        }
    }
}

static void move_shots(ss_game *g) {
    for (int i = 0; i < SS_SHOTS; i++) {
        ss_shot *s = &g->shots[i];
        if (!s->alive) {
            continue;
        }
        s->x = (int16_t)(s->x + s->vx);
        s->y = (int16_t)(s->y + s->vy);
        if (--s->life == 0 || gone(s->x, s->y, 4)) {
            s->alive = false;
        }
    }
}

static void age_blasts(ss_game *g) {
    for (int i = 0; i < SS_BLASTS; i++) {
        if (!g->blasts[i].alive) {
            continue;
        }
        if (++g->blasts[i].age >= SS_BLAST_AGES) {
            g->blasts[i].alive = false;
        }
    }
}

static void hit_rocks(ss_game *g) {
    for (int i = 0; i < SS_SHOTS; i++) {
        ss_shot *s = &g->shots[i];
        if (!s->alive) {
            continue;
        }
        for (int j = 0; j < SS_ROCKS; j++) {
            ss_rock *r = &g->rocks[j];
            if (!r->alive) {
                continue;
            }
            int dx = SS_PX(s->x - r->x), dy = SS_PX(s->y - r->y);
            int rad = ss_rock_r[r->size];
            if (dx * dx + dy * dy <= rad * rad) {
                s->alive = false;
                break_rock(g, r);
                break;
            }
        }
    }
}

static void move_drop(ss_game *g) {
    if (!g->drop.alive) {
        return;
    }
    g->drop.x = (int16_t)(g->drop.x + g->drop.vx);
    g->drop.y = (int16_t)(g->drop.y + g->drop.vy);
    if (gone(g->drop.x, g->drop.y, 10)) {
        g->drop.alive = false;
        return;
    }
    if (!ss_ship_visible(g)) {
        return;
    }
    int dx = SS_PX(g->drop.x - g->ship.x), dy = SS_PX(g->drop.y - g->ship.y);
    int reach = SS_HULL_R + 6;
    if (dx * dx + dy * dy <= reach * reach) {
        g->drop.alive = false;
        g->power = (ss_power)g->drop.kind;
        g->power_left = SS_POWER_FRAMES;
    }
}

static void lose_ship(ss_game *g) {
    blast_at(g, SS_PX(g->ship.x), SS_PX(g->ship.y));
    g->power = SS_P_NONE;
    g->power_left = 0;
    if (g->lives > 0) {
        g->lives--;
    }
    g->phase = g->lives == 0 ? SS_OVER : SS_DEAD;
    g->phase_timer = g->lives == 0 ? SS_OVER_FRAMES : SS_DEAD_FRAMES;
}

static void hit_ship(ss_game *g) {
    if (!ss_ship_visible(g) || g->invuln > 0) {
        return;
    }
    for (int i = 0; i < SS_ROCKS; i++) {
        ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int dx = SS_PX(r->x - g->ship.x), dy = SS_PX(r->y - g->ship.y);
        int reach = ss_rock_r[r->size] + SS_SHIP_R;
        if (dx * dx + dy * dy > reach * reach) {
            continue;
        }
        if (g->power == SS_P_SHIELD) {
            /* the shield is spent on the meteor rather than on the frame */
            g->power = SS_P_NONE;
            g->power_left = 0;
            g->invuln = 20;
            break_rock(g, r);
            return;
        }
        lose_ship(g);
        return;
    }
}

/* nothing has been hit for a long time: burn the nearest one and carry on */
static void break_stalemate(ss_game *g) {
    int gap;
    const ss_rock *near = closest(g, &gap, false);

    if (near != NULL) {
        break_rock(g, (ss_rock *)near);
    }
}

static void respawn(ss_game *g) {
    g->ship.x = (int16_t)(ARC_PANEL * SS_SUB / 2);
    g->ship.y = (int16_t)(ARC_PANEL * SS_SUB / 2);
    g->ship.vx = 0;
    g->ship.vy = 0;
    g->ship.angle = 192; /* nose up, which is where one starts */
    g->ship.thrusting = false;
    g->invuln = SS_INVULN;
    g->cooldown = 0;
    for (int i = 0; i < SS_SHOTS; i++) {
        g->shots[i].alive = false;
    }
}

void ss_step(ss_game *g) {
    g->frame++;

    if (g->power_left > 0 && --g->power_left == 0) {
        g->power = SS_P_NONE;
    }
    if (g->invuln > 0) {
        g->invuln--;
    }
    if (g->cooldown > 0) {
        g->cooldown--;
    }
    if (g->evade_left > 0) {
        g->evade_left--;
    }

    /*
     * Topped up one meteor at a time rather than all at once, so the room a
     * big one leaves as it breaks up fills over a few seconds instead of the
     * panel suddenly gaining four rocks.
     */
    if (rock_weight(g) < want_weight(g) && rocks_alive(g) + 2 <= SS_ROCKS &&
        (g->frame % 24) == 0) {
        spawn_rock(g);
    }

    move_rocks(g);
    move_shots(g);
    age_blasts(g);
    hit_rocks(g);
    move_drop(g);

    if (ss_ship_visible(g)) {
        pilot(g);
        move_ship(g);
        hit_ship(g);
    } else {
        g->ship.thrusting = false;
    }

    if (++g->patient >= SS_PATIENCE) {
        g->patient = 0;
        break_stalemate(g);
    }

    switch (g->phase) {
    case SS_FLY:
        break;
    case SS_DEAD:
        if (--g->phase_timer == 0) {
            respawn(g);
            g->phase = SS_FLY;
        }
        break;
    case SS_OVER:
        if (--g->phase_timer == 0) {
            ss_init(g, g->rng);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* setting up, and what the renderer asks                              */
/* ------------------------------------------------------------------ */

/*
 * The starfield is laid out once and then never moves.  Spreading it by hand
 * would put it in a pattern; drawing it from the seed puts a couple of stars
 * on top of each other now and again, which is what a real one does.
 */
static void scatter_stars(ss_game *g) {
    for (int i = 0; i < SS_STARS; i++) {
        g->stars[i].x = (uint8_t)range(g, 1, ARC_PANEL - 2);
        g->stars[i].y = (uint8_t)range(g, 1, ARC_PANEL - 2);
        /* two thirds of them hold steady; the rest blink, slowly and apart */
        g->stars[i].period = (uint8_t)(range(g, 0, 2) == 0 ? range(g, 24, 90) : 0);
        g->stars[i].phase = (uint8_t)range(g, 0, 89);
    }
}

void ss_init(ss_game *g, uint32_t seed) {
    /* whatever the last words per minute set, which outlives a restart */
    uint8_t gear = g->speed;

    for (unsigned i = 0; i < sizeof(*g); i++) {
        ((uint8_t *)g)[i] = 0;
    }
    g->rng = seed ? seed : 1u;
    g->speed = gear ? gear : 4;
    g->lives = SS_LIVES;
    g->phase = SS_FLY;
    g->redraw = true;

    scatter_stars(g);
    respawn(g);
    /* enough rock to be worth looking at on the first frame, not one meteor */
    for (int i = 0; i < 3; i++) {
        spawn_rock(g);
    }
}

void ss_set_speed(ss_game *g, uint8_t gear) { g->speed = gear ? gear : 1; }

bool ss_ship_visible(const ss_game *g) {
    if (g->phase != SS_FLY) {
        return false;
    }
    /* blinking while it is untouchable, so the state is visible rather than felt */
    return g->invuln == 0 || ((g->invuln >> 2) & 1) == 0;
}

bool ss_star_lit(const ss_game *g, int i) {
    const ss_star *s = &g->stars[i];
    if (s->period == 0) {
        return true;
    }
    return ((g->frame + s->phase) % s->period) >= s->period / 4;
}

const char *ss_power_name(const ss_game *g) {
    switch (g->power) {
    case SS_P_RAPID:
        return "RAPID";
    case SS_P_SPREAD:
        return "SPREAD";
    case SS_P_SHIELD:
        return "SHIELD";
    default:
        return NULL;
    }
}

const char *ss_banner(const ss_game *g) {
    return g->phase == SS_OVER ? "GAME OVER" : NULL;
}
