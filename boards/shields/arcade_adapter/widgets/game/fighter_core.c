/*
 * Street Fighter dongle - game core (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "fighter_core.h"

/* ------------------------------------------------------------------ */
/* what the pilot pays for things                                      */
/* ------------------------------------------------------------------ */

/*
 * How long an attack has to have been coming before the other one is allowed
 * to notice it.  This is the single number that decides whether a self-playing
 * fight is worth looking at, and it is three because that is one frame more
 * than a punch spends winding up.  So a punch is seen only once it is already
 * dangerous and lands on anybody who was not guarding beforehand, while a
 * sweep - twice as slow to start - is seen with a frame to spare and is the
 * one that gets blocked.  Set it to one and both are answerable, both are
 * blocked about half the time, and two fighters stand there trading nothing
 * for a whole round; the first version of this did exactly that, and the
 * measure of it was rounds ending on the clock rather than on the floor.
 */
#define FG_REACTION 3

/*
 * And how far ahead an answer is worth making.  A guard put up eight frames
 * before a fireball arrives is a guard that has dropped again by the time it
 * does; this is what stops the pilot from answering something it has seen
 * across the whole stage and then standing in the open when it lands.
 */
#define FG_ANSWER 8

/*
 * How many frames before a fireball arrives a fighter has to leave the ground
 * to be over it.  It is not a taste - it is where the arc crosses the top of
 * the band the fireball travels in, and the assertion below is what says so.
 * Jump later than this and the fighter is still on its way up when the ball
 * gets there, which is the worst of both answers.
 */
#define FG_HOP_LEAD 7

_Static_assert(FG_RISE(FG_HOP_LEAD) > FG_HIGH_Y + FG_HIGH_H,
               "a hop that early does not clear a fireball");
_Static_assert(FG_HOP_LEAD <= FG_ANSWER, "and the pilot would never look that far ahead");

/*
 * The beat between one attack and the next, on top of the attack's own
 * recovery.  Without it a fighter swings on every frame it is able to, which
 * means it is never in a state that could put a guard up, which means neither
 * of them ever guards - and a fight where nothing is ever blocked is decided
 * entirely by which attack does more damage.  The first version of this had
 * no beat, and the measure of it was that one fighter threw sweeps, the other
 * threw punches, and the sweeps won nine matches in ten.
 *
 * It is a spread rather than a constant so that two fighters coming out of the
 * same exchange do not come back out of it on the same frame for ever.
 */
#define FG_POISE 8
#define FG_POISE_SPREAD 8

/*
 * Knockback, in quarter pixels a frame and what comes off it each frame.  It
 * is here rather than being a step of so many pixels because a shove has to be
 * something the fighter is still carrying while it decides what to do next -
 * a fighter knocked out of its own range and then walking back in during the
 * shove is the whole reason a corner is worth having somebody in.
 */
#define FG_PUSH_HIT   14
#define FG_PUSH_BLOCK 8
#define FG_PUSH_DECAY 2

/*
 * How far the two preferred distances may be closed by the clock, in pixels.
 * Two pilots that both like standing at forty pixels will stand at forty
 * pixels all round; FG_PATIENCE takes one off both of them every time nothing
 * has landed for three seconds, and this is where that stops - past it the
 * fighters would be walking through each other rather than fighting.
 */
#define FG_SQUEEZE_MAX 30

/* how long the panel holds a knockout, and a decided match, before moving on */
#define FG_KO_HOLD   45
#define FG_OVER_HOLD 60
/* and how long "ROUND n" is up before "FIGHT" replaces it */
#define FG_READY_HOLD 45
#define FG_GO_HOLD    15

/* where the two of them start a round, far enough apart to need a first step */
#define FG_START_X 74

/* ------------------------------------------------------------------ */
/* small change                                                        */
/* ------------------------------------------------------------------ */

static uint32_t rnd(fg_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int range_of(fg_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

static int iabs(int v) { return v < 0 ? -v : v; }

static int mid_x(const fg_fighter *f) { return FG_PX(f->x); }
static int foot_y(const fg_fighter *f) { return FG_FLOOR - FG_PX(f->h); }

int fg_height(const fg_fighter *f) {
    return (f->state == FG_S_CROUCH || f->state == FG_S_DOWN) ? FG_CROUCH_H : FG_BODY_H;
}

/* the box an attack has to reach into: the fighter, wherever it is standing */
typedef struct {
    int x0, y0, x1, y1;
} fg_box;

static void body_box(const fg_fighter *f, fg_box *b) {
    int cx = mid_x(f), fy = foot_y(f);

    b->x0 = cx - FG_BODY_W / 2;
    b->x1 = cx + FG_BODY_W / 2;
    b->y1 = fy;
    b->y0 = fy - fg_height(f);
}

static bool boxes_meet(const fg_box *a, const fg_box *b) {
    return !(a->x1 <= b->x0 || b->x1 <= a->x0 || a->y1 <= b->y0 || b->y1 <= a->y0);
}

/*
 * The three attacks as numbers, in one place.  The core reads it to work out
 * when a swing is dangerous and the renderer reads it through fg_swing() to
 * work out how far the limb is out, so the picture and the hit can never
 * disagree about where the foot is.
 */
static bool attack_of(const fg_fighter *f, int *wind, int *hit, int *rest, int *reach,
                      int *high) {
    switch (f->state) {
    case FG_S_PUNCH:
        *wind = FG_PUNCH_WIND;
        *hit = FG_PUNCH_HIT;
        *rest = FG_PUNCH_REST;
        *reach = FG_PUNCH_REACH;
        *high = 1;
        return true;
    case FG_S_KICK:
        *wind = FG_KICK_WIND;
        *hit = FG_KICK_HIT;
        *rest = FG_KICK_REST;
        *reach = FG_KICK_REACH;
        *high = 0;
        return true;
    default:
        return false;
    }
}

/* how many frames into the current attack it is */
static int elapsed_of(const fg_fighter *f, int wind, int hit, int rest) {
    return wind + hit + rest - (int)f->timer;
}

int fg_swing(const fg_fighter *f, int *high) {
    int wind, hit, rest, reach;

    if (!attack_of(f, &wind, &hit, &rest, &reach, high)) {
        return 0;
    }
    int elapsed = elapsed_of(f, wind, hit, rest);
    if (elapsed < wind) {
        /* on its way out, so a swing reads as a swing rather than as a limb
         * that was suddenly there */
        return reach * (elapsed + 1) / (wind + 1);
    }
    if (elapsed < wind + hit) {
        return reach;
    }
    int back = elapsed - wind - hit;
    return reach - reach * back / rest;
}

/* the rectangle a swing is dangerous in, or false when it is not */
static bool swing_box(const fg_fighter *f, fg_box *b) {
    int wind, hit, rest, reach, high;

    if (!attack_of(f, &wind, &hit, &rest, &reach, &high)) {
        return false;
    }
    int elapsed = elapsed_of(f, wind, hit, rest);
    if (elapsed < wind || elapsed >= wind + hit) {
        return false;
    }

    int cx = mid_x(f), fy = foot_y(f);
    b->x0 = f->face ? cx + FG_BODY_W / 2 : cx - reach;
    b->x1 = f->face ? cx + reach : cx - FG_BODY_W / 2;
    b->y1 = fy - (high ? FG_HIGH_Y : FG_LOW_Y);
    b->y0 = b->y1 - (high ? FG_HIGH_H : FG_LOW_H);
    return true;
}

/* a fireball is a box at chest height, which is what a crouch goes under */
static void ball_box(const fg_ball *b, fg_box *out) {
    int cx = FG_PX(b->x);

    out->x0 = cx - FG_BALL_R;
    out->x1 = cx + FG_BALL_R;
    out->y1 = FG_FLOOR - FG_HIGH_Y;
    out->y0 = out->y1 - FG_HIGH_H;
}

/* facing an attacker is what makes a guard a guard rather than a turned back */
static bool guarding(const fg_fighter *f, int from_x) {
    if (f->state != FG_S_BLOCK) {
        return false;
    }
    return f->face ? from_x > mid_x(f) : from_x < mid_x(f);
}

static void spark_at(fg_game *g, int x, int y, bool guarded) {
    for (int i = 0; i < FG_SPARKS; i++) {
        if (g->sparks[i].age > 0) {
            continue;
        }
        g->sparks[i].x = (int16_t)x;
        g->sparks[i].y = (int16_t)y;
        g->sparks[i].age = 4;
        g->sparks[i].guarded = guarded;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* the exchange                                                        */
/* ------------------------------------------------------------------ */

/*
 * One landed attack.  A guard stops everything the fighters can throw and only
 * the fireball takes anything through it, which is the reason to throw one at
 * somebody who blocks well; a clean hit costs health, six frames of not being
 * able to answer, and a shove that is twice what a guarded one is.
 */
static void land(fg_game *g, int who, int on, int damage, int chip, int at_x, int at_y) {
    fg_fighter *d = &g->f[on];
    bool blocked = guarding(d, mid_x(&g->f[who]));
    int taken = blocked ? chip : damage;
    int push = blocked ? FG_PUSH_BLOCK : FG_PUSH_HIT;

    if (taken > 0) {
        d->health = (uint8_t)(taken >= d->health ? 0 : d->health - taken);
    }
    d->push = (int16_t)(mid_x(d) < mid_x(&g->f[who]) ? -push : push);
    if (!blocked) {
        d->state = FG_S_HURT;
        d->timer = FG_HURT;
        d->h = 0;
        d->vy = 0;
        d->air = 0;
    }
    spark_at(g, at_x, at_y, blocked);
    g->sfx |= blocked ? FG_SFX_BLOCK : FG_SFX_HIT;
    g->idle = 0;
}

/* whichever of the two rectangles overlaps, roughly where they met */
static void meeting(const fg_box *a, const fg_box *b, int *x, int *y) {
    int x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    int x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    int y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    int y1 = a->y1 < b->y1 ? a->y1 : b->y1;

    *x = (x0 + x1) / 2;
    *y = (y0 + y1) / 2;
}

/*
 * Both swings are read off the board before either of them is applied, and
 * that is not tidiness.  Applied one at a time, whichever fighter was looked
 * at first put the other into a flinch, the flinch cancelled the swing that
 * was already touching it, and the same fighter won every single trade - which
 * over a long run was four matches in five to whoever happened to be index
 * zero.  Deciding both against the same board makes a trade a trade.
 */
static void resolve_swings(fg_game *g) {
    fg_box swing[2], body[2];
    int hurts[2] = {0, 0};
    int mx[2] = {0, 0}, my[2] = {0, 0};

    for (int i = 0; i < 2; i++) {
        body_box(&g->f[i], &body[i]);
    }
    for (int i = 0; i < 2; i++) {
        if (g->f[i].spent || !swing_box(&g->f[i], &swing[i])) {
            continue;
        }
        if (!boxes_meet(&swing[i], &body[1 - i])) {
            continue;
        }
        /* what it is worth is taken now, with the swing: applying the first
         * one puts the other into a flinch, and a flinch is not a sweep - so
         * reading the damage back afterwards charged every trade at the price
         * of a punch, and always to the same fighter */
        hurts[i] = g->f[i].state == FG_S_KICK ? FG_D_KICK : FG_D_PUNCH;
        meeting(&swing[i], &body[1 - i], &mx[i], &my[i]);
    }
    for (int i = 0; i < 2; i++) {
        if (hurts[i] == 0) {
            continue;
        }
        g->f[i].spent = true;
        land(g, i, 1 - i, hurts[i], 0, mx[i], my[i]);
    }
}

/*
 * Fireballs, and the one thing two of them can do to each other.  Two meeting
 * in the middle cancel rather than passing through, because a pair that passed
 * would leave both fighters guarding at once and nothing to watch - and
 * because a fireball war that ends in a flash in the middle of the stage is
 * the picture anybody who has seen this game is expecting.
 */
static void move_balls(fg_game *g) {
    for (int i = 0; i < 2; i++) {
        fg_ball *b = &g->ball[i];
        if (!b->live) {
            continue;
        }
        b->x = (int16_t)(b->x + b->vx);
        if (FG_PX(b->x) < -FG_BALL_R || FG_PX(b->x) > ARC_PANEL + FG_BALL_R) {
            b->live = false;
        }
    }

    if (g->ball[0].live && g->ball[1].live) {
        fg_box a, b;
        ball_box(&g->ball[0], &a);
        ball_box(&g->ball[1], &b);
        if (boxes_meet(&a, &b)) {
            int x, y;
            meeting(&a, &b, &x, &y);
            g->ball[0].live = false;
            g->ball[1].live = false;
            spark_at(g, x, y, false);
            return;
        }
    }

    for (int i = 0; i < 2; i++) {
        fg_ball *b = &g->ball[i];
        if (!b->live) {
            continue;
        }
        fg_box ball, body;
        ball_box(b, &ball);
        int on = 1 - b->owner;
        body_box(&g->f[on], &body);
        if (!boxes_meet(&ball, &body)) {
            continue;
        }
        int x, y;
        meeting(&ball, &body, &x, &y);
        b->live = false;
        land(g, b->owner, on, FG_D_FIRE, FG_D_CHIP, x, y);
    }
}

/* ------------------------------------------------------------------ */
/* the pilot                                                           */
/* ------------------------------------------------------------------ */

/* whether a fighter is in a state it can be talked out of this frame */
static bool answerable(const fg_fighter *f) {
    switch (f->state) {
    case FG_S_IDLE:
    case FG_S_WALK:
    case FG_S_BACK:
    case FG_S_CROUCH:
    case FG_S_BLOCK:
        return true;
    default:
        return false;
    }
}

/*
 * How many frames until something lands on this fighter, and whether it comes
 * in high or low; -1 when there is nothing coming.  A swing is only counted
 * once it has been going FG_REACTION frames and only while it is still able to
 * connect, and only from a distance it could actually reach - so the pilot
 * neither answers a swing thrown at nothing nor one it has already survived.
 */
static int incoming(const fg_game *g, int who, int *high) {
    const fg_fighter *me = &g->f[who], *you = &g->f[1 - who];
    int wind, hit, rest, reach, hi;
    int best = -1;

    *high = 1;
    if (attack_of(you, &wind, &hit, &rest, &reach, &hi)) {
        int elapsed = elapsed_of(you, wind, hit, rest);
        int gap = iabs(mid_x(you) - mid_x(me)) - FG_BODY_W / 2;
        bool facing = you->face ? mid_x(me) > mid_x(you) : mid_x(me) < mid_x(you);
        if (elapsed >= FG_REACTION && elapsed < wind + hit && facing && gap <= reach) {
            best = wind - elapsed;
            if (best < 0) {
                best = 0;
            }
            *high = hi;
        }
    }

    for (int i = 0; i < 2; i++) {
        const fg_ball *b = &g->ball[i];
        if (!b->live || b->owner == who) {
            continue;
        }
        /* one going the other way is somebody else's problem */
        if ((b->vx > 0) != (b->x < me->x)) {
            continue;
        }
        int gap = iabs(FG_PX(b->x) - mid_x(me)) - FG_BODY_W / 2 - FG_BALL_R;
        int when = gap <= 0 ? 0 : gap * FG_SUB / FG_BALL_V;
        if (best < 0 || when < best) {
            best = when;
            *high = 1;
        }
    }
    return best;
}

static void start_attack(fg_game *g, int who, int state) {
    fg_fighter *f = &g->f[who];
    int wind = 0, hit = 0, rest = 0, reach = 0, high = 0;

    f->state = (uint8_t)state;
    f->poise = (uint8_t)(FG_POISE + (int)(rnd(g) % FG_POISE_SPREAD));
    f->spent = false;
    f->h = 0;
    f->vy = 0;
    f->air = 0;
    if (state == FG_S_FIRE) {
        f->timer = FG_FIRE_WIND + FG_FIRE_REST;
        f->cool = FG_FIRE_COOL;
        return;
    }
    if (attack_of(f, &wind, &hit, &rest, &reach, &high)) {
        f->timer = (uint8_t)(wind + hit + rest);
    }
}

static void start_jump(fg_fighter *f, int drift) {
    f->state = FG_S_JUMP;
    f->vy = FG_JUMP_V;
    f->air = (int8_t)drift;
}

/*
 * The three answers, chosen by how much this fighter would rather guard than
 * move.  Guarding is safe and gives up the initiative; going under a punch or
 * over a sweep gives it back, and is wrong against the other one.  Which is
 * exactly why the pilot has to answer what is coming rather than what it
 * usually does.
 */
static void answer(fg_game *g, int who, int when, int high) {
    fg_fighter *f = &g->f[who];
    int roll = (int)(rnd(g) & 0xFF);

    if (roll < f->guard) {
        f->state = FG_S_BLOCK;
        f->timer = (uint8_t)(when + 4);
        return;
    }
    if (high) {
        /* over it when there is time to be off the ground by the time it
         * arrives, and under it when there is not */
        if (when >= FG_HOP_LEAD) {
            start_jump(f, 0);
            return;
        }
        f->state = FG_S_CROUCH;
        f->timer = (uint8_t)(when + 4);
        return;
    }
    /* a sweep is gone in four frames, so a jump only answers it if it leaves
     * now; any later and the fighter lands into the next one */
    if (when <= 2) {
        start_jump(f, 0);
        return;
    }
    f->state = FG_S_BLOCK;
    f->timer = (uint8_t)(when + 4);
}

/*
 * What to do when nothing is coming: stand where this fighter likes to stand,
 * and swing whenever the other one is close enough to be hit.  The order is
 * the whole pilot - the range check comes first, so a fighter in range always
 * has an opinion, and the walking is only what happens when it is not.
 */
static void press(fg_game *g, int who) {
    fg_fighter *me = &g->f[who], *you = &g->f[1 - who];
    int d = iabs(mid_x(you) - mid_x(me));
    int walk = g->speed - 1;
    int want = me->nerve - g->squeeze;

    if (want < FG_CLINCH + 6) {
        want = FG_CLINCH + 6;
    }

    /*
     * Standing further out than this fighter likes comes first, and that
     * order is the whole of what `nerve` is worth: put the range check first
     * and every fighter attacks from the edge of the sweep, because that is
     * the first distance at which anything is possible - which is a stage
     * where nobody ever throws a punch, whatever their numbers say.
     */
    if (d > want) {
        if (d > FG_KICK_REACH && me->cool == 0 &&
            (int)(rnd(g) % 100) < 1 + me->temper / 80) {
            start_attack(g, who, FG_S_FIRE);
            return;
        }
        /* jumping in is a way of crossing the fireball range; it is rare
         * because it lands committed, which is what makes it a decision */
        if (d > FG_KICK_REACH * 2 && (int)(rnd(g) % 100) < 1 + me->temper / 96) {
            me->face = (uint8_t)(mid_x(you) > mid_x(me));
            start_jump(me, (me->face ? walk : -walk) * FG_SUB);
            return;
        }
        me->state = FG_S_WALK;
        me->stride++;
        return;
    }

    if (d <= FG_KICK_REACH && me->poise == 0) {
        /* the sweep reaches further and catches a crouch, and is the one to
         * throw at anybody who is not standing up straight */
        bool low = d > FG_PUNCH_REACH || you->state == FG_S_CROUCH ||
                   (int)(rnd(g) & 0xFF) < me->temper;
        start_attack(g, who, low ? FG_S_KICK : FG_S_PUNCH);
        return;
    }
    if (d > FG_KICK_REACH && me->cool == 0 && (int)(rnd(g) % 100) < 1 + me->temper / 80) {
        start_attack(g, who, FG_S_FIRE);
        return;
    }
    if (d < want - 6) {
        me->state = FG_S_BACK;
        me->stride++;
        return;
    }
    me->state = FG_S_IDLE;
}

static void plan(fg_game *g, int who) {
    fg_fighter *me = &g->f[who], *you = &g->f[1 - who];
    int high, when;

    if (!answerable(me)) {
        return;
    }
    me->face = (uint8_t)(mid_x(you) > mid_x(me));

    when = incoming(g, who, &high);
    if (when >= 0 && when <= FG_ANSWER) {
        answer(g, who, when, high);
        return;
    }
    press(g, who);
}

/* ------------------------------------------------------------------ */
/* moving                                                              */
/* ------------------------------------------------------------------ */

static void clamp_x(fg_fighter *f) {
    int lo = FG_WALL * FG_SUB, hi = (ARC_PANEL - FG_WALL) * FG_SUB;

    if (f->x < lo) {
        f->x = (int16_t)lo;
        f->push = 0;
    }
    if (f->x > hi) {
        f->x = (int16_t)hi;
        f->push = 0;
    }
}

static void throw_ball(fg_game *g, int who) {
    fg_ball *b = &g->ball[who];
    const fg_fighter *f = &g->f[who];

    b->x = (int16_t)(f->x + (f->face ? FG_BODY_W / 2 : -FG_BODY_W / 2) * FG_SUB);
    b->y = (int16_t)((FG_FLOOR - FG_HIGH_Y - FG_HIGH_H / 2) * FG_SUB);
    b->vx = (int8_t)(f->face ? FG_BALL_V : -FG_BALL_V);
    b->owner = (uint8_t)who;
    b->live = true;
    g->sfx |= FG_SFX_FIRE;
}

static void advance(fg_game *g, int who) {
    fg_fighter *f = &g->f[who];
    int walk = g->speed - 1;

    if (f->timer > 0) {
        f->timer--;
    }
    if (f->cool > 0) {
        f->cool--;
    }
    if (f->poise > 0) {
        f->poise--;
    }

    if (f->push != 0) {
        f->x = (int16_t)(f->x + f->push);
        f->push = (int16_t)(f->push > 0 ? f->push - FG_PUSH_DECAY : f->push + FG_PUSH_DECAY);
        if (iabs(f->push) < FG_PUSH_DECAY) {
            f->push = 0;
        }
    }

    switch (f->state) {
    case FG_S_WALK:
        f->x = (int16_t)(f->x + (f->face ? walk : -walk) * FG_SUB);
        break;
    case FG_S_BACK:
        f->x = (int16_t)(f->x - (f->face ? walk : -walk) * FG_SUB);
        break;
    case FG_S_JUMP:
        f->x = (int16_t)(f->x + f->air);
        f->h = (int16_t)(f->h + f->vy);
        f->vy = (int16_t)(f->vy - FG_FALL);
        if (f->h <= 0) {
            f->h = 0;
            f->vy = 0;
            f->air = 0;
            /* the two frames of landing that make a jump a commitment; they
             * cost nothing but the answer the fighter cannot give in them */
            f->state = FG_S_CROUCH;
            f->timer = 2;
        }
        break;
    case FG_S_FIRE:
        if (elapsed_of(f, FG_FIRE_WIND, 0, FG_FIRE_REST) == FG_FIRE_WIND) {
            throw_ball(g, who);
        }
        break;
    default:
        break;
    }

    /* an attack, a guard, a crouch or a flinch that has run out stands up */
    if (f->timer == 0) {
        switch (f->state) {
        case FG_S_PUNCH:
        case FG_S_KICK:
        case FG_S_FIRE:
        case FG_S_HURT:
        case FG_S_BLOCK:
        case FG_S_CROUCH:
            f->state = FG_S_IDLE;
            break;
        default:
            break;
        }
    }
    clamp_x(f);
}

/*
 * Two fighters may not stand inside one another.  Pushing both apart by half
 * would let a fighter in the corner be walked through, so whatever the wall
 * refuses to take is handed to the other one - which is what makes a corner
 * somewhere to be trapped rather than somewhere to be pushed out of.
 */
static void separate(fg_game *g) {
    int d = mid_x(&g->f[1]) - mid_x(&g->f[0]);
    int over = FG_CLINCH - iabs(d);

    if (over <= 0) {
        return;
    }
    int step = (over * FG_SUB + 1) / 2;
    int left = d >= 0 ? 0 : 1, right = 1 - left;

    g->f[left].x = (int16_t)(g->f[left].x - step);
    g->f[right].x = (int16_t)(g->f[right].x + step);
    clamp_x(&g->f[left]);
    clamp_x(&g->f[right]);

    /* whatever the wall would not take, the other one gives up */
    int now = iabs(mid_x(&g->f[1]) - mid_x(&g->f[0]));
    if (now >= FG_CLINCH) {
        return;
    }
    int rest = (FG_CLINCH - now) * FG_SUB;
    if (g->f[left].x <= FG_WALL * FG_SUB) {
        g->f[right].x = (int16_t)(g->f[right].x + rest);
    } else {
        g->f[left].x = (int16_t)(g->f[left].x - rest);
    }
    clamp_x(&g->f[0]);
    clamp_x(&g->f[1]);
}

static void tick_sparks(fg_game *g) {
    for (int i = 0; i < FG_SPARKS; i++) {
        if (g->sparks[i].age > 0) {
            g->sparks[i].age--;
        }
    }
}

/* the trailing bar, which is what makes a big hit read as a big hit */
static void tick_bars(fg_game *g) {
    for (int i = 0; i < 2; i++) {
        if (g->bar[i] > g->f[i].health) {
            g->bar[i]--;
        } else {
            g->bar[i] = g->f[i].health;
        }
    }
}

/* ------------------------------------------------------------------ */
/* rounds and matches                                                  */
/* ------------------------------------------------------------------ */

static void stand(fg_game *g, int who) {
    fg_fighter *f = &g->f[who];

    f->x = (int16_t)((who == 0 ? FG_START_X : ARC_PANEL - FG_START_X) * FG_SUB);
    f->h = 0;
    f->vy = 0;
    f->push = 0;
    f->air = 0;
    f->state = FG_S_IDLE;
    f->timer = 0;
    f->face = (uint8_t)(who == 0);
    f->health = FG_HEALTH;
    f->stride = 0;
    f->cool = 0;
    f->poise = 0;
    f->spent = false;
}

static void reset_round(fg_game *g) {
    for (int i = 0; i < 2; i++) {
        stand(g, i);
        g->bar[i] = FG_HEALTH;
        g->ball[i].live = false;
    }
    memset(g->sparks, 0, sizeof(g->sparks));
    g->clock = FG_ROUND_CLOCK;
    g->idle = 0;
    g->squeeze = 0;
    g->winner = 2;
    g->phase = FG_READY;
    g->phase_timer = FG_READY_HOLD;
}

/*
 * A fighter's three numbers, drawn once a match.  The ranges are what keeps
 * one match from looking like the last: a nervy guard at the far end of the
 * stage against a brawler who will not stop walking forward is a different
 * three rounds from two of either, and both of them are recognisably a fight.
 */
static void draw_fighter(fg_game *g, int who) {
    fg_fighter *f = &g->f[who];

    /* the far end of this is a fighter that likes to be just out of reach and
     * throw; past it is one that never closes at all, and two of those play a
     * round of nothing and leave it to the clock */
    f->nerve = (uint8_t)range_of(g, FG_CLINCH + 8, FG_KICK_REACH + 4);
    f->guard = (uint8_t)range_of(g, 40, 190);
    f->temper = (uint8_t)range_of(g, 30, 220);
}

static void new_match(fg_game *g) {
    for (int i = 0; i < 2; i++) {
        g->wins[i] = 0;
        draw_fighter(g, i);
    }
    g->round = 1;
    g->ended = FG_E_NONE;
    reset_round(g);
}

static void end_round(fg_game *g, int winner, int how) {
    g->winner = (uint8_t)winner;
    g->ended = (uint8_t)how;
    g->phase = FG_KO;
    g->phase_timer = FG_KO_HOLD;
    g->sfx |= FG_SFX_KO;

    if (winner < 2) {
        g->wins[winner]++;
        g->f[winner].state = FG_S_WIN;
        g->f[1 - winner].state = FG_S_DOWN;
        g->f[1 - winner].h = 0;
        g->f[1 - winner].vy = 0;
    } else {
        for (int i = 0; i < 2; i++) {
            g->f[i].state = FG_S_DOWN;
            g->f[i].h = 0;
        }
    }
    for (int i = 0; i < 2; i++) {
        g->f[i].timer = 0;
        g->f[i].push = 0;
        g->f[i].air = 0;
        /* a fireball still in the air belonged to the round that just ended */
        g->ball[i].live = false;
    }
}

static void check_round(fg_game *g) {
    for (int i = 0; i < 2; i++) {
        if (g->f[i].health == 0) {
            end_round(g, 1 - i, FG_E_KO);
            return;
        }
    }
    if (g->clock == 0) {
        int w = 2;
        if (g->f[0].health > g->f[1].health) {
            w = 0;
        } else if (g->f[1].health > g->f[0].health) {
            w = 1;
        }
        end_round(g, w, FG_E_TIME);
    }
}

/* who the match belongs to once there is no more of it to play */
static int match_winner(const fg_game *g) {
    if (g->wins[0] == g->wins[1]) {
        return 2;
    }
    return g->wins[0] > g->wins[1] ? 0 : 1;
}

static bool match_over(const fg_game *g) {
    return g->wins[0] >= FG_ROUNDS_WIN || g->wins[1] >= FG_ROUNDS_WIN ||
           g->round >= FG_ROUNDS_MAX;
}

static void after_round(fg_game *g) {
    if (match_over(g)) {
        g->winner = (uint8_t)match_winner(g);
        g->phase = FG_OVER;
        g->phase_timer = FG_OVER_HOLD;
        g->bouts++;
        g->sfx |= FG_SFX_MATCH;
        return;
    }
    g->round++;
    reset_round(g);
}

/* ------------------------------------------------------------------ */
/* the frame                                                           */
/* ------------------------------------------------------------------ */

static void fight(fg_game *g) {
    if (g->clock > 0) {
        g->clock--;
    }
    /* whichever of them is thought about first is thinking with a frame of
     * newer information than the other, so which one that is alternates */
    int first = g->frame & 1;
    for (int i = 0; i < 2; i++) {
        plan(g, i ^ first);
    }
    for (int i = 0; i < 2; i++) {
        advance(g, i ^ first);
    }
    resolve_swings(g);
    move_balls(g);
    separate(g);

    if (++g->idle >= FG_PATIENCE) {
        g->idle = 0;
        if (g->squeeze < FG_SQUEEZE_MAX) {
            g->squeeze++;
        }
    }
    check_round(g);
}

/* the fireballs and the flashes carry on while the stage waits */
static void settle(fg_game *g) {
    move_balls(g);
    for (int i = 0; i < 2; i++) {
        if (g->f[i].push != 0) {
            g->f[i].x = (int16_t)(g->f[i].x + g->f[i].push);
            g->f[i].push = (int16_t)(g->f[i].push > 0 ? g->f[i].push - FG_PUSH_DECAY
                                                      : g->f[i].push + FG_PUSH_DECAY);
            if (iabs(g->f[i].push) < FG_PUSH_DECAY) {
                g->f[i].push = 0;
            }
            clamp_x(&g->f[i]);
        }
    }
}

void fg_step(fg_game *g) {
    g->sfx = 0;
    g->frame++;

    switch (g->phase) {
    case FG_READY:
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            g->phase = FG_FIGHT;
            g->sfx |= FG_SFX_ROUND;
        }
        break;
    case FG_FIGHT:
        fight(g);
        break;
    case FG_KO:
        settle(g);
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            after_round(g);
        }
        break;
    default:
        settle(g);
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            new_match(g);
        }
        break;
    }

    /* the stage flashes on the frame a round is decided, and only then */
    g->flash = (g->phase == FG_KO && g->phase_timer > FG_KO_HOLD - 5);
    tick_sparks(g);
    tick_bars(g);
}

/* ------------------------------------------------------------------ */
/* the outside                                                         */
/* ------------------------------------------------------------------ */

void fg_init(fg_game *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1u;
    g->speed = 4;
    new_match(g);
    g->redraw = true;
}

void fg_set_speed(fg_game *g, uint8_t gear) { g->speed = gear ? gear : 1; }

const char *fg_banner(const fg_game *g) {
    static const char *const ROUND[FG_ROUNDS_MAX] = {"ROUND 1", "ROUND 2", "ROUND 3",
                                                     "ROUND 4", "ROUND 5"};

    if (g->phase == FG_READY) {
        if (g->phase_timer <= FG_GO_HOLD) {
            return "FIGHT";
        }
        return ROUND[(g->round - 1) % FG_ROUNDS_MAX];
    }
    if (g->phase == FG_KO) {
        return g->ended == FG_E_TIME ? "TIME" : "K O";
    }
    if (g->phase == FG_OVER) {
        if (g->winner == 0) {
            return "P1 WINS";
        }
        return g->winner == 1 ? "P2 WINS" : "DRAW";
    }
    return NULL;
}
