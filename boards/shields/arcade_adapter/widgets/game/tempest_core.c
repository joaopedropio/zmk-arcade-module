/*
 * The well dongle - game core (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include "tempest_core.h"

/* ------------------------------------------------------------------ */
/* how fast everything is                                              */
/* ------------------------------------------------------------------ */

/*
 * Depth units a frame, at the middle gear.  The one that matters is the claw's
 * shot against a flipper's climb: a shot closes at eighteen units a frame and
 * the well is two hundred and fifty-six deep, so anything the claw is lined up
 * on dies within fifteen frames wherever it is.  That is what makes the game a
 * question of which lane to be in rather than of whether a shot arrives, and
 * dropping the shot much below this turns every flipper into a coin toss.
 */
#define TP_CLIMB 4
#define TP_TANK_V 3
#define TP_SPIKE_V 2
#define TP_PULSE_V 2
#define TP_SHOT_V 14
#define TP_BOLT_V 5
#define TP_DIVE_V 6

/* eighths of a lane the claw slides in a frame - a lap of a closed well in
 * about a second and three quarters, which is fast enough to answer a flipper
 * across the well and slow enough that where it is standing is a decision */
#define TP_SLIDE 5

/* how often a flipper at the rim starts another tumble, which is the clock the
 * claw is racing when one has landed beside it - the tumble's own length is
 * TP_FLIP_T, in the header, because the renderer needs it as well */
#define TP_RIM_FLIP 9

/* how deep a flipper has to be before it will tumble, so nothing arrives at
 * the rim already halfway across a spoke */
#define TP_FLIP_MIN 24
/* one chance in this many frames of a flipper tumbling for no reason */
#define TP_FLIP_ODDS 22

/* frames between the claw's shots, and between one enemy's */
#define TP_COOL 5
#define TP_FIRE_EVERY 64
#define TP_FIRE_MIN 40 /* and how far up the well it has to be to bother */

/* how close to a shot's path down the lane something has to pass to be hit */
#define TP_HIT 10

/* the pulsars' shared beat, and how much of it is lethal */
#define TP_PULSE_T 62
#define TP_PULSE_HOT 18

/* how much of a spike a shot takes off */
#define TP_CHIP 40

/* the phases that are not play, in frames */
#define TP_DYING_T 45
#define TP_CLEARED_T 40
#define TP_OVER_T 60

/* what everything is worth */
#define TP_SCORE_FLIPPER 150
#define TP_SCORE_TANKER 100
#define TP_SCORE_SPIKER 200
#define TP_SCORE_PULSAR 200
#define TP_SCORE_SPIKE 5
#define TP_SCORE_LEVEL 300

/* how many enemies a level sends, and how many may be out at once */
#define TP_WAVE 9
#define TP_WAVE_STEP 2
#define TP_WAVE_MAX 22
#define TP_SPAWN_T 34
#define TP_SPAWN_MIN 14
#define TP_ALIVE 3
#define TP_ALIVE_STEP 2

/*
 * Three numbers that have to hold against each other rather than on their own,
 * so they are checked here instead of being remembered.  A shot slower than a
 * flipper would never catch one walked up the well ahead of it; a claw slower
 * along the rim than a flipper tumbling down it would make a landed flipper
 * unanswerable rather than merely urgent; and a spike built all the way to the
 * rim would make sitting on that lane fatal, when the whole point of a spike is
 * that it only matters on the way out.
 */
_Static_assert(TP_SHOT_V > TP_CLIMB, "a shot is slower than a flipper");
_Static_assert(TP_SLIDE * TP_RIM_FLIP > TP_SUB, "a flipper outruns the claw along the rim");
_Static_assert(TP_SPIKE_MAX + TP_CLAW_D < TP_DEPTH, "a full spike reaches the rim");

/* ------------------------------------------------------------------ */
/* arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t rnd(tp_game *g) {
    g->rng = g->rng * 1103515245u + 12345u;
    return g->rng >> 16;
}

static int range(tp_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

/* the gear the words per minute put it in, applied to a distance a frame */
static int geared(const tp_game *g, int v) {
    int r = v * g->speed / 4;
    return r < 1 ? 1 : r;
}

/* ------------------------------------------------------------------ */
/* the well                                                            */
/* ------------------------------------------------------------------ */

/*
 * Five wells, as sixteen lanes' worth of offsets from the middle of the panel.
 * They are written out rather than worked out because a shape is the one thing
 * here anybody looking at the source should be able to see - the circle is a
 * circle, the cross is a cross - and because two of them are open, which is a
 * property no formula would carry.
 *
 * An open well is the harder board of the two kinds and not merely a different
 * one: the claw cannot run away round the back of it, so a flipper landing at
 * one end has to be shot rather than avoided.  Levels alternate closed and open
 * for that reason rather than for the look.
 */
const tp_shape TP_SHAPE[TP_SHAPES] = {
    /* a circle, and the one every game of this starts on */
    {{0, 38, 71, 92, 100, 92, 71, 38, 0, -38, -71, -92, -100, -92, -71, -38, 0},
     {100, 92, 71, 38, 0, -38, -71, -92, -100, -92, -71, -38, 0, 38, 71, 92, 100},
     0, 0, 1},
    /* a flat strip, open at both ends, vanishing well above itself */
    {{-108, -94, -81, -68, -54, -40, -27, -14, 0, 14, 27, 40, 54, 68, 81, 94, 108},
     {102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102},
     0, -64, 0},
    /* a square, whose corner lanes are much wider at the rim than its flat
     * ones - which is the whole difference between it and the circle */
    {{0, 46, 92, 92, 92, 92, 92, 46, 0, -46, -92, -92, -92, -92, -92, -46, 0},
     {92, 92, 92, 46, 0, -46, -92, -92, -92, -92, -92, -46, 0, 46, 92, 92, 92},
     0, 0, 1},
    /* a V, open at both ends */
    {{-104, -91, -78, -65, -52, -39, -26, -13, 0, 13, 26, 39, 52, 65, 78, 91, 104},
     {-56, -36, -17, 2, 22, 42, 61, 80, 100, 80, 61, 42, 22, 2, -17, -36, -56},
     0, -72, 0},
    /* a cross, four arms deep, where a lane down an arm is long and the one
     * across its mouth is short */
    {{0, 34, 34, 100, 100, 100, 34, 34, 0, -34, -34, -100, -100, -100, -34, -34, 0},
     {100, 100, 34, 34, 0, -34, -34, -100, -100, -100, -34, -34, 0, 34, 34, 100, 100},
     0, 0, 1},
};

int tp_persp(int d) { return (256 * TP_Z0) / (TP_Z0 + TP_DEPTH - d); }

void tp_at(const tp_shape *s, int pos8, int d, int *x, int *y) {
    if (s->closed) {
        pos8 %= TP_RIM;
        if (pos8 < 0) {
            pos8 += TP_RIM;
        }
    } else {
        if (pos8 < 0) {
            pos8 = 0;
        }
        if (pos8 > TP_RIM) {
            pos8 = TP_RIM;
        }
    }
    int i = pos8 / TP_SUB;
    int f = pos8 - i * TP_SUB;
    if (i >= TP_SEGS) {
        i = TP_SEGS - 1;
        f = TP_SUB;
    }
    int ox = s->x[i] * (TP_SUB - f) + s->x[i + 1] * f - s->vx * TP_SUB;
    int oy = s->y[i] * (TP_SUB - f) + s->y[i + 1] * f - s->vy * TP_SUB;
    int p = tp_persp(d);

    *x = TP_CX + s->vx + ox * p / (TP_SUB * 256);
    *y = TP_CY + s->vy + oy * p / (TP_SUB * 256);
}

const tp_shape *tp_well(const tp_game *g) { return &TP_SHAPE[g->shape]; }

/*
 * How far it is from one place on the rim to another, signed, the short way
 * round.  Everything the pilot does with distance goes through this, so a
 * closed well's wrap is written down once instead of at each of the half dozen
 * places that would otherwise have to remember it.
 */
static int rim_gap(const tp_shape *s, int from8, int to8) {
    int d = to8 - from8;

    if (s->closed) {
        while (d > TP_RIM / 2) {
            d -= TP_RIM;
        }
        while (d < -TP_RIM / 2) {
            d += TP_RIM;
        }
    }
    return d;
}

static int lane_gap(const tp_shape *s, int from, int to) {
    return rim_gap(s, from * TP_SUB, to * TP_SUB) / TP_SUB;
}

/* the lane on the other side of a spoke, or -1 at the end of an open well */
static int lane_step(const tp_shape *s, int lane, int dir) {
    int n = lane + dir;

    if (s->closed) {
        return (n + TP_SEGS) % TP_SEGS;
    }
    return (n < 0 || n >= TP_SEGS) ? -1 : n;
}

/* whether the claw, one lane wide and sitting at pos8, is over lane `lane` */
static bool claw_over(const tp_shape *s, int pos8, int lane) {
    return iabs(rim_gap(s, pos8, lane * TP_SUB)) < TP_SUB;
}

int tp_claw_lane(const tp_game *g) {
    const tp_shape *s = tp_well(g);
    int l = (g->pos + TP_SUB / 2) / TP_SUB;

    if (s->closed) {
        return l % TP_SEGS;
    }
    return l < 0 ? 0 : (l >= TP_SEGS ? TP_SEGS - 1 : l);
}

int tp_claw_d(const tp_game *g) { return g->phase == TP_DIVE ? g->dive : TP_DEPTH; }

/* ------------------------------------------------------------------ */
/* the pulsars' beat                                                   */
/* ------------------------------------------------------------------ */

/*
 * Every pulsar on the board beats off the same clock rather than one apiece,
 * which is what makes a pulse something the claw can be somewhere else for.
 * Independent pulsars would leave no window at all with three of them out.
 */
static bool hot_at(const tp_game *g, int t) {
    return (int)((g->pulse + (unsigned)t) % TP_PULSE_T) >= TP_PULSE_T - TP_PULSE_HOT;
}

static bool pulse_hot(const tp_game *g) { return hot_at(g, 0); }

bool tp_lane_hot(const tp_game *g, int lane) {
    if (!pulse_hot(g)) {
        return false;
    }
    for (int i = 0; i < TP_ENEMIES; i++) {
        const tp_enemy *e = &g->foe[i];
        if (e->kind == TP_E_PULSAR && e->lane == lane) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* sending them up the well                                            */
/* ------------------------------------------------------------------ */

static int alive(const tp_game *g) {
    int n = 0;

    for (int i = 0; i < TP_ENEMIES; i++) {
        if (g->foe[i].kind != TP_E_GONE) {
            n++;
        }
    }
    return n;
}

static tp_enemy *free_foe(tp_game *g) {
    for (int i = 0; i < TP_ENEMIES; i++) {
        if (g->foe[i].kind == TP_E_GONE) {
            return &g->foe[i];
        }
    }
    return NULL;
}

static int foe_speed(const tp_game *g, int kind) {
    switch (kind) {
    case TP_E_TANKER:
        return geared(g, TP_TANK_V);
    case TP_E_SPIKER:
        return geared(g, TP_SPIKE_V);
    case TP_E_PULSAR:
        return geared(g, TP_PULSE_V);
    default:
        return geared(g, TP_CLIMB);
    }
}

static void arm(tp_game *g, tp_enemy *e, int kind, int lane) {
    e->kind = (uint8_t)kind;
    e->lane = (uint8_t)lane;
    e->d = 0;
    e->step = (int8_t)foe_speed(g, kind);
    e->flip = 0;
    e->turn = 0;
    e->t = (uint16_t)range(g, TP_FIRE_EVERY / 2, TP_FIRE_EVERY);
    e->rim = 0;
}

/*
 * What comes up, and when.  The mix is by level rather than by wave because
 * there are no waves here - a level is a fixed number of enemies fed in as
 * room appears, so a board never becomes a queue of them waiting at the far
 * rim for a slot.  Which one is picked is a weight rather than a table so that
 * a level deep enough to have all four still sends mostly flippers, which are
 * the only kind the claw can do anything about quickly.
 */
static void spawn(tp_game *g) {
    tp_enemy *e = free_foe(g);

    if (e == NULL || g->left == 0) {
        return;
    }
    int w_flip = 6;
    int w_tank = g->level >= 2 ? 3 : 0;
    int w_spike = g->level >= 3 ? 2 : 0;
    int w_pulse = g->level >= 5 ? 2 : 0;
    int total = w_flip + w_tank + w_spike + w_pulse;
    int r = range(g, 0, total - 1);
    int kind;

    if (r < w_flip) {
        kind = TP_E_FLIPPER;
    } else if (r < w_flip + w_tank) {
        kind = TP_E_TANKER;
    } else if (r < w_flip + w_tank + w_spike) {
        kind = TP_E_SPIKER;
    } else {
        kind = TP_E_PULSAR;
    }

    /* a spiker in a lane that already has a full spike would have nothing to
     * do there, so it is sent somewhere it can still build */
    int lane = range(g, 0, TP_SEGS - 1);
    if (kind == TP_E_SPIKER) {
        for (int i = 0; i < TP_SEGS && g->spike[lane] >= TP_SPIKE_MAX - 24; i++) {
            lane = (lane + 1) % TP_SEGS;
        }
    }
    arm(g, e, kind, lane);
    g->left--;
}

/* ------------------------------------------------------------------ */
/* what they do                                                        */
/* ------------------------------------------------------------------ */

static void fire_bolt(tp_game *g, const tp_enemy *e) {
    for (int i = 0; i < TP_BOLTS; i++) {
        if (!g->bolt[i].on) {
            g->bolt[i].on = 1;
            g->bolt[i].lane = e->lane;
            g->bolt[i].d = e->d;
            return;
        }
    }
}

/* the tumble across a spoke, started only where there is a lane on the far
 * side - an open well's ends are walls to a flipper as much as to the claw */
static void start_flip(tp_game *g, tp_enemy *e, int dir) {
    if (lane_step(tp_well(g), e->lane, dir) < 0) {
        dir = -dir;
        if (lane_step(tp_well(g), e->lane, dir) < 0) {
            return;
        }
    }
    e->flip = TP_FLIP_T;
    e->turn = (int8_t)dir;
}

static void step_foe(tp_game *g, tp_enemy *e) {
    const tp_shape *s = tp_well(g);

    if (e->flip > 0) {
        e->flip--;
        if (e->flip == 0) {
            int n = lane_step(s, e->lane, e->turn);
            if (n >= 0) {
                e->lane = (uint8_t)n;
            }
            e->turn = 0;
        }
    }

    switch (e->kind) {
    case TP_E_SPIKER: {
        /* it never comes all the way up: it yo-yos in its lane leaving the
         * spike behind it, which is the thing that has to be dealt with */
        e->d += e->step;
        if (e->d >= TP_SPIKE_MAX) {
            e->d = TP_SPIKE_MAX;
            e->step = (int8_t)-e->step;
        }
        if (e->d <= 0) {
            e->d = 0;
            e->step = (int8_t)-e->step;
        }
        if (e->d > (int)g->spike[e->lane]) {
            g->spike[e->lane] = (uint8_t)e->d;
        }
        return;
    }
    case TP_E_TANKER:
    case TP_E_FLIPPER:
    case TP_E_PULSAR:
    default:
        break;
    }

    if (e->d < TP_DEPTH) {
        e->d += e->step;
        if (e->d > TP_DEPTH) {
            e->d = TP_DEPTH;
        }
    }

    if (e->d >= TP_DEPTH) {
        /* at the rim a flipper stops climbing and starts hunting: it tumbles
         * towards the claw on a beat the claw can count, which is what makes
         * one that has landed a race rather than an ambush */
        e->rim = (uint8_t)(e->rim < 255 ? e->rim + 1 : 255);
        if (e->kind != TP_E_TANKER && e->flip == 0 && e->rim % TP_RIM_FLIP == 0) {
            int away = rim_gap(s, e->lane * TP_SUB + TP_SUB / 2, g->pos + TP_SUB / 2);
            if (away != 0) {
                start_flip(g, e, away > 0 ? 1 : -1);
            }
        }
    } else if (e->kind == TP_E_FLIPPER || e->kind == TP_E_PULSAR) {
        if (e->flip == 0 && e->d > TP_FLIP_MIN) {
            int away = rim_gap(s, e->lane * TP_SUB + TP_SUB / 2, g->pos + TP_SUB / 2);
            bool chase = away != 0 && (int)rnd(g) % TP_FLIP_ODDS == 0;
            if (chase) {
                start_flip(g, e, away > 0 ? 1 : -1);
            }
        }
    }

    if ((e->kind == TP_E_FLIPPER || e->kind == TP_E_PULSAR) && e->d > TP_FIRE_MIN) {
        if (e->t > 0) {
            e->t--;
        } else {
            e->t = (uint16_t)range(g, TP_FIRE_EVERY / 2, TP_FIRE_EVERY);
            fire_bolt(g, e);
        }
    }
}

/*
 * A tanker is worth shooting early and expensive to shoot late, because what
 * it leaves behind is two flippers at the depth it died at.  One that reaches
 * the rim breaks up there of its own accord, which is the same rule applied by
 * the board rather than by the claw.
 */
static void split(tp_game *g, const tp_enemy *e) {
    const tp_shape *s = tp_well(g);

    for (int k = -1; k <= 1; k += 2) {
        int lane = lane_step(s, e->lane, k);
        if (lane < 0) {
            lane = e->lane;
        }
        tp_enemy *n = free_foe(g);
        if (n == NULL) {
            return;
        }
        arm(g, n, TP_E_FLIPPER, lane);
        n->d = e->d;
    }
}

static int foe_worth(int kind) {
    switch (kind) {
    case TP_E_TANKER:
        return TP_SCORE_TANKER;
    case TP_E_SPIKER:
        return TP_SCORE_SPIKER;
    case TP_E_PULSAR:
        return TP_SCORE_PULSAR;
    default:
        return TP_SCORE_FLIPPER;
    }
}

static void kill(tp_game *g, tp_enemy *e) {
    g->score += (uint32_t)foe_worth(e->kind);
    if (e->kind == TP_E_TANKER) {
        tp_enemy dead = *e;
        e->kind = TP_E_GONE;
        split(g, &dead);
        return;
    }
    e->kind = TP_E_GONE;
}

/* ------------------------------------------------------------------ */
/* the shots                                                           */
/* ------------------------------------------------------------------ */

/* a flipping enemy is over a spoke, so it answers to both of the lanes it is
 * between - anything else would make the tumble a moment nothing can hit */
static bool foe_in_lane(const tp_shape *s, const tp_enemy *e, int lane) {
    if (e->lane == lane) {
        return true;
    }
    return e->flip > 0 && lane_step(s, e->lane, e->turn) == lane;
}

static void step_shots(tp_game *g) {
    const tp_shape *s = tp_well(g);

    for (int i = 0; i < TP_SHOTS; i++) {
        tp_shot *sh = &g->shot[i];
        if (!sh->on) {
            continue;
        }
        int was = sh->d;
        sh->d -= (int16_t)geared(g, TP_SHOT_V);

        for (int j = 0; j < TP_ENEMIES; j++) {
            tp_enemy *e = &g->foe[j];
            if (e->kind == TP_E_GONE || !foe_in_lane(s, e, sh->lane)) {
                continue;
            }
            if (e->d <= was + TP_HIT && e->d >= sh->d - TP_HIT) {
                kill(g, e);
                sh->on = 0;
                break;
            }
        }
        if (!sh->on) {
            continue;
        }

        /* a shot that reaches the spike stops on it and takes a bite out */
        if (g->spike[sh->lane] > 0 && sh->d <= (int)g->spike[sh->lane]) {
            int left = (int)g->spike[sh->lane] - TP_CHIP;
            g->spike[sh->lane] = (uint8_t)(left > 0 ? left : 0);
            g->score += TP_SCORE_SPIKE;
            sh->on = 0;
            continue;
        }
        if (sh->d <= 0) {
            sh->on = 0;
        }
    }

    for (int i = 0; i < TP_BOLTS; i++) {
        tp_shot *b = &g->bolt[i];
        if (!b->on) {
            continue;
        }
        b->d += (int16_t)geared(g, TP_BOLT_V);
        if (b->d > TP_DEPTH) {
            b->on = 0;
        }
    }
}

static void fire(tp_game *g) {
    for (int i = 0; i < TP_SHOTS; i++) {
        if (!g->shot[i].on) {
            g->shot[i].on = 1;
            g->shot[i].lane = (uint8_t)tp_claw_lane(g);
            g->shot[i].d = (int16_t)tp_claw_d(g);
            g->cool = TP_COOL;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* dying                                                               */
/* ------------------------------------------------------------------ */

static void die(tp_game *g, int why) {
    g->phase = TP_DYING;
    g->phase_timer = 0;
    g->why = (uint8_t)why;
    g->redraw = true;
}

static void collide(tp_game *g) {
    const tp_shape *s = tp_well(g);

    if (g->phase == TP_DIVE) {
        /* the only thing left down there is what the spikers built */
        for (int l = 0; l < TP_SEGS; l++) {
            if (g->spike[l] > 0 && claw_over(s, g->pos, l) && g->dive <= (int)g->spike[l]) {
                die(g, TP_D_SPIKE);
                return;
            }
        }
        return;
    }

    for (int i = 0; i < TP_ENEMIES; i++) {
        const tp_enemy *e = &g->foe[i];
        if (e->kind == TP_E_GONE) {
            continue;
        }
        bool over = claw_over(s, g->pos, e->lane) ||
                    (e->flip > 0 && claw_over(s, g->pos, lane_step(s, e->lane, e->turn)));
        if (!over) {
            continue;
        }
        if (e->kind == TP_E_PULSAR && pulse_hot(g)) {
            die(g, TP_D_PULSE);
            return;
        }
        if (e->d >= TP_DEPTH - TP_GRAB && e->kind != TP_E_SPIKER) {
            die(g, TP_D_GRABBED);
            return;
        }
    }

    for (int i = 0; i < TP_BOLTS; i++) {
        const tp_shot *b = &g->bolt[i];
        if (b->on && b->d >= TP_DEPTH - TP_LAND && claw_over(s, g->pos, b->lane)) {
            die(g, TP_D_SHOT);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* the pilot                                                           */
/* ------------------------------------------------------------------ */

/*
 * The claw is deciding one thing: which lane to be over.  Everything else -
 * when to fire, when to spend the superzapper - falls out of that, because a
 * shot only ever goes down the lane it is already on and the zapper is only
 * ever spent when no lane is worth being over.
 *
 * So every lane gets one number, and the number is made of two currencies the
 * way the crossing's is.  A lane something will kill the claw in before it can
 * leave again costs TP_FATAL and is never chosen; a lane something will kill it
 * in later than that costs by how much later, tailing off to nothing at the end
 * of the window.  Against that, a lane with something in it the claw can shoot
 * before it arrives is worth having, worth more the closer that thing is to the
 * rim, because a flipper at the rim is the only thing here that cannot be
 * outrun.
 */
#define TP_FATAL 1000000
#define TP_LOOK 46   /* frames priced */
#define TP_COMMIT 11 /* and how many of them it is stuck with */
#define TP_TAIL 55   /* the most a danger past the commitment costs */
#define TP_TRAVEL 5  /* what a lane of sliding is worth giving up */
#define TP_STICK 18  /* and what staying with last frame's answer is worth */

/* what a target in the lane is worth, flat and then by how near the rim it is */
#define TP_TARGET 70
#define TP_URGENT 150
/* a tanker breaks into two flippers, so it is worth taking deep */
#define TP_TANK_BONUS 60
/* what a spike in the lane is worth taking down, once the dive is what is left */
#define TP_SPIKE_WORTH 180

/* how long the pilot will hold a heading before pricing the board again */
#define TP_RETHINK 3

/*
 * The frame the claw is standing on a lane, counting the one it is deciding in
 * as frame zero.  It is a frame index rather than a count of frames because
 * everything it is compared against is one: the order within a frame is decide,
 * slide, then be hit, so the claw has already moved once by the time frame zero
 * is over, and counting the moves instead puts every danger one frame later
 * than it arrives.
 */
static int travel_to(const tp_game *g, int lane) {
    int v = geared(g, TP_SLIDE);
    int gap = iabs(rim_gap(tp_well(g), g->pos, lane * TP_SUB));
    int t = (gap + v - 1) / v - 1;

    return t < 0 ? 0 : t;
}

/*
 * The frame at which an enemy makes a lane lethal, or -1 if it does not within
 * the window.  A climber makes its own lane lethal when it reaches the rim and
 * then the lanes either side of it as it tumbles along; a pulsar makes its lane
 * lethal on the next beat wherever it is.  Both are the same question - when
 * does being here stop being survivable - so both come back as a frame number
 * and the caller never has to know which kind it was looking at.
 */
static int threat_t(const tp_game *g, const tp_enemy *e, int lane, int from) {
    const tp_shape *s = tp_well(g);

    if (e->kind == TP_E_SPIKER) {
        return -1;
    }
    if (e->kind == TP_E_PULSAR && foe_in_lane(s, e, lane)) {
        /* where up the well it is does not come into it: the beat kills
         * anything in the lane, so the question is only when the next one is */
        for (int t = from; t <= TP_LOOK; t++) {
            if (hot_at(g, t)) {
                return t;
            }
        }
        return -1;
    }
    int step = e->step > 0 ? e->step : 1;
    int t_rim = e->d >= TP_DEPTH - TP_GRAB ? 0 : (TP_DEPTH - TP_GRAB - e->d) / step;
    int away = iabs(lane_gap(s, e->lane, lane));

    if (e->kind == TP_E_TANKER && away > 1) {
        return -1; /* what it leaves at the rim is two flippers, one either side */
    }
    int t = t_rim + away * TP_RIM_FLIP;

    return t <= TP_LOOK ? t : -1;
}

/*
 * The frame at which a shot fired from `lane`, once the claw has got there,
 * would reach an enemy - or -1 when it cannot be shot from there at all.  This
 * is the other half of the comparison above: a flipper coming up the lane is a
 * target while this is the smaller of the two numbers and a danger the moment
 * it is not, and that one comparison is the whole of the claw's nerve.
 */
static int kill_t(const tp_game *g, const tp_enemy *e, int lane, int arrive) {
    const tp_shape *s = tp_well(g);

    if (!foe_in_lane(s, e, lane)) {
        return -1;
    }
    int close = geared(g, TP_SHOT_V) + (e->step > 0 ? e->step : 0);

    return arrive + (int)g->cool + (TP_DEPTH - e->d) / close;
}

/*
 * Whether a lane the claw is only passing over kills it between two frames.
 * Passing over is not the same question as standing in: there is no chance to
 * shoot anything on the way through, so everything in it is a danger and
 * nothing in it is a target.
 */
static bool crossing_kills(const tp_game *g, int lane, int t0, int t1) {
    for (int i = 0; i < TP_ENEMIES; i++) {
        const tp_enemy *e = &g->foe[i];
        if (e->kind == TP_E_GONE) {
            continue;
        }
        int bad = threat_t(g, e, lane, t0);
        if (bad >= 0 && bad <= t1) {
            return true;
        }
    }
    for (int i = 0; i < TP_BOLTS; i++) {
        const tp_shot *b = &g->bolt[i];
        if (!b->on || b->lane != lane) {
            continue;
        }
        int lands = (TP_DEPTH - TP_LAND - b->d) / geared(g, TP_BOLT_V);
        if (lands >= t0 && lands <= t1) {
            return true;
        }
    }
    return false;
}

/*
 * Getting there is as dangerous as being there.  The claw is a lane wide, so it
 * covers two lanes for every moment it is not squarely on one - the lane it is
 * leaving as much as the ones in between - and a pulsar beating in any of them
 * kills it exactly as dead as one in the lane it was going to.  This works out
 * when the claw is over each lane on the way and asks that lane the passing-over
 * question for those frames alone.
 *
 * Pricing only the destination is what put nearly every death in the soak down
 * to a pulse: the claw would step out of a doomed lane into a safe one and be
 * caught halfway, still touching the lane it had left.
 */
static bool path_kills(const tp_game *g, int dest, int *when) {
    const tp_shape *s = tp_well(g);
    int v = geared(g, TP_SLIDE);
    int total = rim_gap(s, g->pos, dest * TP_SUB);
    int dir = total >= 0 ? 1 : -1;
    int len = iabs(total);

    for (int m = 0; m < TP_SEGS; m++) {
        if (m == dest) {
            continue; /* where it is going is priced properly, targets and all */
        }
        int u = rim_gap(s, g->pos, m * TP_SUB) * dir;
        if (u <= -TP_SUB || u >= len + TP_SUB) {
            continue; /* the claw is never over it */
        }
        /* how far it has slid by the end of frame t, and the frames either side
         * of that during which the two of them are still touching */
        int t0 = u > TP_SUB ? (u - TP_SUB) / v : 0;
        /* a lane the slide stops short of leaving is one it is over for good */
        int t1 = len >= u + TP_SUB ? (u + TP_SUB + v - 1) / v - 2 : TP_LOOK;
        if (t1 > TP_LOOK) {
            t1 = TP_LOOK;
        }
        if (t1 < t0) {
            continue; /* it is already past, or never gets there */
        }
        if (crossing_kills(g, m, t0, t1)) {
            *when = t0;
            return true;
        }
    }
    return false;
}

static int lane_score(const tp_game *g, int lane) {
    const tp_shape *s = tp_well(g);
    int arrive = travel_to(g, lane);
    int worst = 0;
    int value = 0;

    for (int i = 0; i < TP_ENEMIES; i++) {
        const tp_enemy *e = &g->foe[i];
        if (e->kind == TP_E_GONE) {
            continue;
        }
        int bad = threat_t(g, e, lane, arrive);
        int shot = kill_t(g, e, lane, arrive);

        if (bad >= 0 && (shot < 0 || shot >= bad)) {
            if (bad <= arrive || bad <= TP_COMMIT) {
                return -TP_FATAL + bad * 1000;
            }
            int cost = TP_TAIL * (TP_LOOK - bad) / TP_LOOK;
            if (cost > worst) {
                worst = cost;
            }
        } else if (shot >= 0) {
            value += TP_TARGET + TP_URGENT * e->d / TP_DEPTH;
            if (e->kind == TP_E_TANKER) {
                value += TP_TANK_BONUS;
            }
        }
    }

    for (int i = 0; i < TP_BOLTS; i++) {
        const tp_shot *b = &g->bolt[i];
        if (!b->on || b->lane != lane) {
            continue;
        }
        int lands = (TP_DEPTH - TP_LAND - b->d) / geared(g, TP_BOLT_V);
        if (lands < 0) {
            lands = 0;
        }
        if (lands >= arrive && lands <= TP_COMMIT) {
            return -TP_FATAL + lands * 1000;
        }
        if (lands > TP_COMMIT && lands <= TP_LOOK) {
            int cost = TP_TAIL * (TP_LOOK - lands) / TP_LOOK;
            if (cost > worst) {
                worst = cost;
            }
        }
    }

    int when;
    if (path_kills(g, lane, &when)) {
        return -TP_FATAL + when * 1000;
    }

    /* once the board is clear the only thing left to do is pick a lane to dive
     * down, so a spike in one stops being scenery and becomes the whole score */
    if (g->left == 0 && alive(g) == 0) {
        value += g->spike[lane] > 0 ? TP_SPIKE_WORTH * (TP_SPIKE_MAX - (int)g->spike[lane]) /
                                          TP_SPIKE_MAX
                                    : TP_SPIKE_WORTH;
    } else if (g->spike[lane] > 0 && alive(g) < 2) {
        value += TP_SPIKE_WORTH / 4;
    }

    return value - worst - iabs(lane_gap(s, tp_claw_lane(g), lane)) * TP_TRAVEL;
}

/*
 * The superzapper, spent on the frame nothing survives rather than on a rule
 * about when it is worth it.  One a level is the arcade's bargain and it is the
 * right one here too: a claw that keeps it in reserve for ever is a claw that
 * dies holding it, and one that spends it early has thirty seconds of level
 * left to get through without it.
 */
static bool zap(tp_game *g) {
    if (g->zap == 0) {
        return false;
    }
    g->zap--;
    g->zap_t = 8;
    for (int i = 0; i < TP_ENEMIES; i++) {
        if (g->foe[i].kind != TP_E_GONE) {
            g->score += (uint32_t)foe_worth(g->foe[i].kind);
            g->foe[i].kind = TP_E_GONE;
        }
    }
    for (int i = 0; i < TP_BOLTS; i++) {
        g->bolt[i].on = 0;
    }
    g->redraw = true;
    return true;
}

static void think(tp_game *g) {
    const tp_shape *s = tp_well(g);
    int best = -TP_FATAL * 2;
    int best_lane = tp_claw_lane(g);

    if (g->patient > 0) {
        g->patient--;
    }
    for (int l = 0; l < TP_SEGS; l++) {
        int v = lane_score(g, l);
        if (l == g->aim) {
            v += TP_STICK;
        }
        if (v > best) {
            best = v;
            best_lane = l;
        }
    }
    if (g->patient == 0 || best_lane == g->aim || best <= -TP_FATAL / 2) {
        g->aim = (int8_t)best_lane;
        g->patient = TP_RETHINK;
    }

    if (best <= -TP_FATAL / 2) {
        zap(g);
    }

    int want = rim_gap(s, g->pos, g->aim * TP_SUB);
    int v = geared(g, TP_SLIDE);
    g->vpos = (int8_t)(want > v ? v : (want < -v ? -v : want));

    /* it fires down the lane it is over, and only when there is something in
     * it: a claw shooting at nothing looks like one that is not aiming */
    if (g->cool == 0) {
        int lane = tp_claw_lane(g);
        bool target = false;
        for (int i = 0; i < TP_ENEMIES && !target; i++) {
            target = g->foe[i].kind != TP_E_GONE && foe_in_lane(s, &g->foe[i], lane);
        }
        if (!target && g->spike[lane] > 0 && (g->left == 0 || alive(g) < 2)) {
            target = true;
        }
        if (target) {
            fire(g);
        }
    }
}

static void slide(tp_game *g) {
    const tp_shape *s = tp_well(g);

    g->pos += g->vpos;
    if (s->closed) {
        g->pos = (int16_t)((g->pos + TP_RIM) % TP_RIM);
    } else {
        if (g->pos < 0) {
            g->pos = 0;
        }
        if (g->pos > (TP_SEGS - 1) * TP_SUB) {
            g->pos = (TP_SEGS - 1) * TP_SUB;
        }
    }
}

/* ------------------------------------------------------------------ */
/* levels                                                              */
/* ------------------------------------------------------------------ */

static void clear_board(tp_game *g) {
    for (int i = 0; i < TP_ENEMIES; i++) {
        g->foe[i].kind = TP_E_GONE;
    }
    for (int i = 0; i < TP_SHOTS; i++) {
        g->shot[i].on = 0;
    }
    for (int i = 0; i < TP_BOLTS; i++) {
        g->bolt[i].on = 0;
    }
    g->cool = 0;
    g->vpos = 0;
}

static void begin_level(tp_game *g) {
    clear_board(g);
    for (int i = 0; i < TP_SEGS; i++) {
        g->spike[i] = 0;
    }
    g->shape = (uint8_t)((g->level - 1) % TP_SHAPES);
    g->pos = (TP_SEGS / 2) * TP_SUB;
    g->aim = (int8_t)(TP_SEGS / 2);
    g->patient = 0;
    g->zap = 1;
    g->zap_t = 0;
    g->dive = TP_DEPTH;

    int wave = TP_WAVE + (g->level - 1) * TP_WAVE_STEP;
    g->left = (uint16_t)(wave > TP_WAVE_MAX ? TP_WAVE_MAX : wave);
    g->spawn_t = 12;
    g->redraw = true;
}

static void next_level(tp_game *g) {
    if (g->level < 99) {
        g->level++;
    }
    g->score += TP_SCORE_LEVEL;
    begin_level(g);
}

static void restart(tp_game *g) {
    g->score = 0;
    g->lives = 3;
    g->level = 1;
    begin_level(g);
    g->phase = TP_PLAY;
    g->phase_timer = 0;
    g->why = TP_D_NONE;
}

static void step_phase(tp_game *g) {
    g->phase_timer++;
    switch (g->phase) {
    case TP_DYING:
        if (g->phase_timer >= TP_DYING_T) {
            if (g->lives > 0) {
                g->lives--;
            }
            if (g->lives == 0) {
                g->phase = TP_OVER;
                g->phase_timer = 0;
                g->redraw = true;
                return;
            }
            clear_board(g);
            g->pos = (TP_SEGS / 2) * TP_SUB;
            g->aim = (int8_t)(TP_SEGS / 2);
            g->dive = TP_DEPTH;
            g->zap = 1;
            /* a claw that ran into a spike goes back to the pause before the
             * dive rather than to play, or the level would be over the instant
             * it came back and it would dive into the same spike for ever */
            g->phase = g->why == TP_D_SPIKE ? TP_CLEARED : TP_PLAY;
            g->phase_timer = 0;
            g->redraw = true;
        }
        return;
    case TP_CLEARED:
        if (g->phase_timer >= TP_CLEARED_T) {
            g->phase = TP_DIVE;
            g->phase_timer = 0;
            g->dive = TP_DEPTH;
            g->redraw = true;
        }
        return;
    case TP_PLAY:
    case TP_DIVE:
        return;
    case TP_OVER:
        if (g->phase_timer >= TP_OVER_T) {
            restart(g);
        }
        return;
    default:
        return;
    }
}

/* ------------------------------------------------------------------ */
/* a frame                                                             */
/* ------------------------------------------------------------------ */

void tp_step(tp_game *g) {
    g->clock++;
    g->pulse++;
    if (g->cool > 0) {
        g->cool--;
    }
    if (g->zap_t > 0) {
        /* the flash is drawn over the whole well, so the frame it ends on has
         * to repaint all of it rather than only what moved */
        g->zap_t--;
        if (g->zap_t == 0) {
            g->redraw = true;
        }
    }

    if (g->phase == TP_CLEARED) {
        /* the pause before the dive is when the spikes get shot down, so the
         * claw is still flying - it is only the well that has gone quiet */
        think(g);
        slide(g);
        step_shots(g);
        step_phase(g);
        return;
    }
    if (g->phase != TP_PLAY && g->phase != TP_DIVE) {
        step_phase(g);
        return;
    }

    if (g->phase == TP_DIVE) {
        think(g);
        slide(g);
        step_shots(g);
        g->dive -= (int16_t)geared(g, TP_DIVE_V);
        collide(g);
        if (g->phase == TP_DIVE && g->dive <= 0) {
            next_level(g);
            g->phase = TP_PLAY;
            g->phase_timer = 0;
        }
        return;
    }

    for (int i = 0; i < TP_ENEMIES; i++) {
        if (g->foe[i].kind != TP_E_GONE) {
            step_foe(g, &g->foe[i]);
        }
    }
    /* a tanker that gets all the way up breaks itself open rather than
     * grabbing, so the rim ends up with the two flippers either way */
    for (int i = 0; i < TP_ENEMIES; i++) {
        tp_enemy *e = &g->foe[i];
        if (e->kind == TP_E_TANKER && e->d >= TP_DEPTH) {
            tp_enemy dead = *e;
            e->kind = TP_E_GONE;
            split(g, &dead);
        }
    }

    think(g);
    slide(g);
    step_shots(g);
    collide(g);
    if (g->phase != TP_PLAY) {
        return;
    }

    if (g->spawn_t > 0) {
        g->spawn_t--;
    }
    int cap = TP_ALIVE + (g->level - 1) / TP_ALIVE_STEP;
    if (cap > TP_ENEMIES) {
        cap = TP_ENEMIES;
    }
    if (g->spawn_t == 0 && alive(g) < cap) {
        spawn(g);
        int every = TP_SPAWN_T - (int)(g->level - 1) * 2;
        g->spawn_t = (uint16_t)(every < TP_SPAWN_MIN ? TP_SPAWN_MIN : every);
    }

    if (g->left == 0 && alive(g) == 0) {
        g->phase = TP_CLEARED;
        g->phase_timer = 0;
        g->redraw = true;
    }
}

void tp_init(tp_game *g, uint32_t seed) {
    for (unsigned i = 0; i < sizeof(*g); i++) {
        ((uint8_t *)g)[i] = 0;
    }
    g->rng = seed ? seed : 1u;
    g->speed = 4;
    restart(g);
}

void tp_set_speed(tp_game *g, uint8_t gear) { g->speed = gear < 3 ? 3 : (gear > 5 ? 5 : gear); }

/* ------------------------------------------------------------------ */
/* what the renderer asks                                              */
/* ------------------------------------------------------------------ */

int tp_spin_age(const tp_game *g) { return g->phase == TP_DYING ? (int)g->phase_timer : -1; }

bool tp_claw_visible(const tp_game *g) { return g->phase != TP_OVER; }

const char *tp_banner(const tp_game *g) {
    if (g->phase == TP_OVER) {
        return "GAME OVER";
    }
    if (g->phase == TP_CLEARED) {
        return "AVOID SPIKES";
    }
    return 0;
}
