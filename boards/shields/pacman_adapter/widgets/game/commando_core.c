/*
 * Metal Slug dongle - game core (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "commando_core.h"

/* ------------------------------------------------------------------ */
/* what the pilot pays for things                                      */
/* ------------------------------------------------------------------ */

/*
 * How far ahead the trooper looks for something to jump.  It is the length of
 * a jump and no more: anything further off is something the ridge may still
 * change its mind about by the time it gets there - a hole is generated
 * fourteen chunks ahead but an enemy can be standing on the far lip of it by
 * the time the jump would land - and a pilot that commits to a take-off from
 * outside its own arc is committing to a board it has not seen.
 */
#define CM_LOOK (CM_HANG * CM_RUN(5))

/*
 * The one case neither of the two rules in want_jump() can answer: hard up
 * against something with no take-off that clears it.  The generator will not
 * build one - a step is never more than one level and a hole is never wider
 * than a chunk - so this is a safety net rather than a plan, and it jumps
 * rather than standing there, because a trooper that stops running is a panel
 * that has stopped moving.
 */
#define CM_DESPERATE 2

/*
 * How close an enemy round is allowed to get before the trooper jumps it.  A
 * bullet travels at the height of whatever fired it, so a trooper with both
 * feet off the ground is a trooper it goes under - and that is the only
 * defence there is, the rifle being the only other answer and the rifle being
 * no use once the round is in the air.
 *
 * Three frames is what the arc asks for rather than what looks right: two
 * frames of rising already puts the feet above the line the round is on, and
 * the third is the one the round spends crossing the trooper.  Without this
 * the run ended every thirteen seconds and always the same way, which is what
 * `deaths: shot=` in the soak is for.
 */
#define CM_DUCK 3

_Static_assert(CM_RISE(2) > CM_BODY_H / 2, "a hop has to clear an enemy round");

/* how far the rifle is worth firing, and how long between rounds */
#define CM_SIGHT  200
#define CM_RELOAD 5
#define CM_SHOT_V 9
#define CM_LANE   7 /* how far off the line of fire a target may still be */

/* and the grenade, which is the only answer to anything standing higher up */
#define CM_NADE_V     34 /* quarter pixels a frame, upwards, at the throw */
#define CM_NADE_RUN   5  /* and along, which sets how far it lands */
#define CM_NADE_MAX   150
#define CM_BLAST      20 /* how far from the burst it kills */
#define CM_NADES_0    4
#define CM_NADES_MAX  9

/*
 * How close something is allowed to get before the trooper stops running at
 * it.  It is a little further than the rifle's own reload, so that anything
 * walking in gets shot at twice before it arrives; without it the trooper
 * walks into things it is in the middle of killing, which reads as the panel
 * having decided to lose.
 */
#define CM_STANDOFF 56

/* what an enemy is worth, and what the ground under the trooper's feet is */
#define CM_V_GRUNT  100
#define CM_V_GUNNER 300
#define CM_V_CRATE  50
#define CM_STRIDE   8 /* world pixels per point of the running score */

/* enemies, and how often they shoot back */
#define CM_FOE_RUN   1
#define CM_FOE_SIGHT 150
#define CM_FOE_COOL  40
#define CM_FOE_SHOT  6

/* the holds between one life and the next */
#define CM_READY_HOLD 24
#define CM_DOWN_HOLD  30
#define CM_OVER_HOLD  45
#define CM_INVULN     36

/* where the world starts, far enough in that nothing is ever at a negative x */
#define CM_START (CM_CHUNK * 4)

/* ------------------------------------------------------------------ */
/* small change                                                        */
/* ------------------------------------------------------------------ */

static uint32_t rnd(cm_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* a hash rather than the ring, for the one thing that is not the ridge */
static uint32_t hash32(uint32_t k) {
    k = (k ^ 61u) ^ (k >> 16);
    k += k << 3;
    k ^= k >> 4;
    k *= 0x27d4eb2du;
    k ^= k >> 15;
    return k;
}

static int32_t hero_wx(const cm_game *g) { return g->scroll + CM_HERO_X; }
static int hero_foot(const cm_game *g) { return CM_PX(g->hero_y); }

/* ------------------------------------------------------------------ */
/* the ridge                                                           */
/* ------------------------------------------------------------------ */

/*
 * One chunk of ground, made from the one before it and thrown at the far end
 * of the ring.  Three rules, and between them they are the whole guarantee
 * that the run never ends on terrain rather than on a bullet:
 *
 *   a hole is never next to a hole, so a jump has somewhere to land;
 *   the far side of a hole is the height of the near side, so the landing is
 *     where the take-off was, whatever the generator felt like doing next;
 *   and no two chunks differ by more than one step, so a wall is always
 *     something the arc in commando_core.h clears.
 *
 * They are applied here, once, going forward - rather than being checked by
 * whoever reads the ground - because a rule about a chunk and its neighbour is
 * trivial to write when the neighbour has already been decided and turns into
 * a recursion the moment it has not.
 */
static void populate(cm_game *g, uint32_t k, int level);

static void make_chunk(cm_game *g, uint32_t k) {
    uint8_t v;

    if (g->last_level == CM_PIT) {
        v = g->hold_level;
    } else if (g->since_pit >= 3 && (rnd(g) % 7) == 0) {
        g->hold_level = g->last_level;
        v = CM_PIT;
    } else {
        int lv = g->last_level;
        uint32_t r = rnd(g) % 9;
        if (r == 0 && lv > 0) {
            lv--;
        } else if (r == 1 && lv < CM_LEVELS - 1) {
            lv++;
        }
        v = (uint8_t)lv;
    }

    g->ground[k % CM_SPAN] = v;
    g->last_level = v;
    g->since_pit = (uint8_t)(v == CM_PIT ? 0 : g->since_pit + 1);
    if (v != CM_PIT) {
        populate(g, k, v);
    }
}

static int level_at(const cm_game *g, int32_t wx) {
    if (wx < 0) {
        return CM_PIT;
    }
    uint32_t k = (uint32_t)wx / CM_CHUNK;
    if (k < g->chunk0 || k >= g->chunk0 + CM_SPAN) {
        return CM_PIT;
    }
    return g->ground[k % CM_SPAN];
}

int cm_surface(const cm_game *g, int32_t wx) {
    int lv = level_at(g, wx);

    return lv == CM_PIT ? CM_PIT : CM_BASE - lv * CM_STEP;
}

/*
 * The skyline, at a quarter of the scroll.  It comes out of a hash rather than
 * out of the ring, because the ring only holds the ground the camera is near
 * and a quarter-speed layer is four times further back than that - and because
 * the hills have no rules to obey, nobody having to walk on them.
 *
 * What they do have to be is flat on top.  commando_render.h repaints only the
 * columns whose silhouette moved, so a hill drawn with any detail finer than a
 * chunk would move every column on the panel on every frame, and this layer
 * alone would cost more than the rest of the game put together.  Each hill is
 * one chunk of hash, which at a quarter speed is a hundred and twenty-eight
 * pixels of flat top.
 */
int cm_hill(const cm_game *g, int screen_x) {
    uint32_t k = (uint32_t)((g->scroll + screen_x) / 4) / CM_CHUNK;

    return CM_HORIZON - 12 - (int)(hash32(k) % 3) * 13;
}

/* keep the ring covering the panel and the pilot's lookahead beyond it */
static void ensure_world(cm_game *g) {
    uint32_t want = (uint32_t)(g->scroll / CM_CHUNK);

    if (want < 2) {
        return;
    }
    want -= 2;
    while (g->chunk0 < want) {
        make_chunk(g, g->chunk0 + CM_SPAN);
        g->chunk0++;
    }
}

/* ------------------------------------------------------------------ */
/* moving over it                                                      */
/* ------------------------------------------------------------------ */

/*
 * The trooper's own movement, as a thing that can be run on a copy.  The pilot
 * decides when to jump by taking this exact function and winding it forward
 * over the ridge in front of it, so what it proves and what then happens are
 * the same arithmetic rather than two descriptions of it that have to be kept
 * in step.
 */
typedef struct {
    int32_t wx;
    int16_t y, vy;
    bool air;
    bool lost; /* through the bottom of the panel, which is a life */
} cm_body;

static void body_step(const cm_game *g, cm_body *b, int run) {
    int feet = CM_PX(b->y);

    /* forward, unless the ground in front is higher than a step up */
    int ahead = cm_surface(g, b->wx + run + CM_BODY_W / 2);
    if (ahead == CM_PIT || ahead >= feet - CM_CLIMB) {
        b->wx += run;
    }

    int under = cm_surface(g, b->wx);
    if (b->air) {
        b->y = (int16_t)(b->y + b->vy);
        b->vy = (int16_t)(b->vy + CM_FALL);
        if (under != CM_PIT && b->vy > 0 && CM_PX(b->y) >= under) {
            b->y = (int16_t)(under * CM_SUB);
            b->vy = 0;
            b->air = false;
        } else if (CM_PX(b->y) >= PM_PANEL) {
            b->lost = true;
        }
        return;
    }

    if (under == CM_PIT) {
        b->air = true;
        b->vy = 0;
        return;
    }
    /*
     * A step down is walked down rather than fallen off, and that is the one
     * thing here that is a decision rather than physics.  Falling off a ledge
     * takes the trooper off the ground for half a second, and a trooper in the
     * air cannot jump - so a step immediately before a hole meant walking into
     * the hole with no say in the matter, which was every life this thing lost
     * to the ground.  The step up is snapped the same way, and cannot be more
     * than CM_CLIMB because the check above would not have let it walk there.
     */
    b->y = (int16_t)(under * CM_SUB);
}

/* ------------------------------------------------------------------ */
/* the pilot                                                           */
/* ------------------------------------------------------------------ */

/* how far ahead the first thing worth leaving the ground for is, or -1 */
static int obstacle(const cm_game *g) {
    int feet = hero_foot(g);
    int32_t from = hero_wx(g) + CM_BODY_W / 2;

    for (int d = 1; d <= CM_LOOK; d++) {
        int s = cm_surface(g, from + d);
        if (s == CM_PIT || s < feet - CM_CLIMB) {
            return d;
        }
    }
    return -1;
}

/* whether the ground in front is too high to walk into, which body_step
 * decides and the pilot has to ask about before it walks into it */
static bool walled(const cm_game *g, int run) {
    int ahead = cm_surface(g, hero_wx(g) + run + CM_BODY_W / 2);

    return ahead != CM_PIT && ahead < hero_foot(g) - CM_CLIMB;
}

/* would leaving the ground in `wait` frames land the trooper on something? */
static bool jump_lands(const cm_game *g, int wait, int run) {
    cm_body b = {hero_wx(g), g->hero_y, g->hero_vy, g->airborne, false};

    for (int t = 0; t < wait; t++) {
        body_step(g, &b, run);
        if (b.air || b.lost) {
            return false;
        }
    }
    b.air = true;
    b.vy = (int16_t)(-CM_JUMP_V);
    for (int t = 0; t < CM_HANG + 10; t++) {
        body_step(g, &b, run);
        if (b.lost) {
            return false;
        }
        if (!b.air) {
            return true;
        }
    }
    return false;
}

/*
 * Two reasons to leave the ground and no others.  There is something in front
 * too high to walk into, and walking is over; or waiting one more frame would
 * mean the jump no longer lands, and this is the last frame it is still worth
 * taking.  Everything else keeps running.
 *
 * The second of those is why the arc is wound forward rather than compared
 * against a take-off distance.  "Jump when the hole is n pixels away" is the
 * same answer whatever the ridge on the far side looks like, and the far side
 * is exactly what decides whether the jump was any good; asking the arc means
 * a hole with a step beyond it is taken off differently from a flat one
 * without anything here knowing that there is such a case.
 *
 * And it is asked as late as it can be, rather than as early: an arc proved
 * from where the trooper was standing sixty pixels ago is an arc from the
 * wrong place, and taking the first take-off that happens to work means
 * hopping the whole way down a flat ridge because there is a hole at the end
 * of it.
 */
static bool want_jump(const cm_game *g, int run) {
    if (walled(g, run)) {
        return true;
    }
    int d = obstacle(g);
    if (d < 0) {
        return false;
    }
    if (jump_lands(g, 1, run)) {
        return false;
    }
    return jump_lands(g, 0, run) || d <= CM_DESPERATE;
}

/* how many frames until an enemy round arrives at the trooper, or -1 for
 * none: only the ones coming this way and only the ones at its own height */
static int incoming(const cm_game *g, int run) {
    int feet = hero_foot(g);
    int32_t me = hero_wx(g);
    int best = -1;

    for (int i = 0; i < CM_SHOTS; i++) {
        const cm_shot *s = &g->shots[i];
        if (!s->live || s->mine || s->wx <= me) {
            continue;
        }
        if (s->y < feet - CM_BODY_H || s->y > feet) {
            continue;
        }
        int gap = (int)(s->wx - me) - CM_BODY_W / 2;
        int when = gap <= 0 ? 0 : gap / (CM_FOE_SHOT + run);
        if (best < 0 || when < best) {
            best = when;
        }
    }
    return best;
}

int cm_foe_y(const cm_game *g, const cm_foe *f) {
    int s = cm_surface(g, f->wx);

    return s == CM_PIT ? CM_BASE : s;
}

/* an enemy the rifle can actually reach: ahead, and on the same line */
static const cm_foe *in_lane(const cm_game *g, int muzzle) {
    const cm_foe *best = NULL;
    int32_t me = hero_wx(g);

    for (int i = 0; i < CM_FOES; i++) {
        const cm_foe *f = &g->foes[i];
        if (!f->alive || f->wx <= me) {
            continue;
        }
        if (f->wx - me > CM_SIGHT) {
            continue;
        }
        if (iabs(cm_foe_y(g, f) - CM_BODY_H / 2 - muzzle) > CM_LANE) {
            continue;
        }
        if (best == NULL || f->wx < best->wx) {
            best = f;
        }
    }
    return best;
}

/* and one it cannot: standing high enough up that a flat shot goes under it */
static const cm_foe *up_a_step(const cm_game *g) {
    const cm_foe *best = NULL;
    int32_t me = hero_wx(g);
    int feet = hero_foot(g);

    for (int i = 0; i < CM_FOES; i++) {
        const cm_foe *f = &g->foes[i];
        if (!f->alive || f->wx <= me || f->wx - me > CM_NADE_MAX) {
            continue;
        }
        if (feet - cm_foe_y(g, f) < CM_STEP - 4) {
            continue;
        }
        if (best == NULL || f->wx < best->wx) {
            best = f;
        }
    }
    return best;
}

/*
 * One round into the air.  The slots are shared, and the enemies are held to
 * half of them: they fire from off the right of the panel and there are
 * always more of them coming, so left to fill the array they would take every
 * slot and the trooper's own rifle would go quiet exactly when it was needed.
 * That is a much worse failure than a round that is never fired, because
 * nothing on the panel says it happened.
 */
static void fire(cm_game *g, int32_t wx, int y, int vx, bool mine) {
    if (!mine) {
        int out = 0;
        for (int i = 0; i < CM_SHOTS; i++) {
            out += (g->shots[i].live && !g->shots[i].mine) ? 1 : 0;
        }
        if (out >= CM_SHOTS / 2) {
            return;
        }
    }
    for (int i = 0; i < CM_SHOTS; i++) {
        cm_shot *s = &g->shots[i];
        if (s->live) {
            continue;
        }
        s->wx = wx;
        s->y = (int16_t)y;
        s->vx = (int8_t)vx;
        s->mine = mine;
        s->live = true;
        g->sfx |= mine ? CM_SFX_SHOT : 0u;
        return;
    }
}

static void lob(cm_game *g) {
    for (int i = 0; i < CM_NADES; i++) {
        cm_nade *n = &g->bombs[i];
        if (n->live) {
            continue;
        }
        n->wx = hero_wx(g) + CM_BODY_W / 2;
        n->y = (int16_t)((hero_foot(g) - CM_BODY_H + 4) * CM_SUB);
        n->vy = (int16_t)(-CM_NADE_V);
        n->vx = CM_NADE_RUN;
        n->live = true;
        g->nades--;
        g->sfx |= CM_SFX_NADE;
        return;
    }
}

static bool nade_out(const cm_game *g) {
    for (int i = 0; i < CM_NADES; i++) {
        if (g->bombs[i].live) {
            return true;
        }
    }
    return false;
}

/*
 * Everything the trooper decides, in the order it decides it.  Shooting comes
 * before moving because the rifle is what makes room to move into; the jump
 * comes before the run because a jump taken a frame late is a jump into a
 * hole; and the run is last, and is the only one of the three that anything is
 * allowed to stop.
 */
static void pilot(cm_game *g) {
    int run = CM_RUN(g->speed);
    int muzzle = hero_foot(g) - CM_BODY_H / 2;

    if (g->reload == 0) {
        const cm_foe *mark = in_lane(g, muzzle);
        if (mark != NULL) {
            fire(g, hero_wx(g) + CM_BODY_W / 2, muzzle, CM_SHOT_V, true);
            g->reload = CM_RELOAD;
        }
    }
    if (g->nades > 0 && !nade_out(g) && up_a_step(g) != NULL) {
        lob(g);
    }

    if (!g->airborne) {
        int round = incoming(g, run);
        bool duck = round >= 0 && round <= CM_DUCK && jump_lands(g, 0, run);

        if (duck || want_jump(g, run)) {
            g->hero_vy = (int16_t)(-CM_JUMP_V);
            g->airborne = true;
            g->sfx |= CM_SFX_JUMP;
        }
    }

    /* nothing stops a jump once it has started, and nothing stops the run but
     * something close enough to walk into */
    if (!g->airborne) {
        const cm_foe *near = in_lane(g, muzzle);
        if (near != NULL && near->wx - hero_wx(g) < CM_STANDOFF) {
            return;
        }
    }

    int32_t was = hero_wx(g);
    cm_body b = {was, g->hero_y, g->hero_vy, g->airborne, false};
    body_step(g, &b, run);

    /* the trooper holds its column and the world moves under it, so walking
     * forward is the scroll and nothing else */
    g->scroll += b.wx - was;
    g->hero_y = b.y;
    g->hero_vy = b.vy;
    g->airborne = b.air;
    if (b.wx != was) {
        g->hero_step++;
    }
}

/* ------------------------------------------------------------------ */
/* what is on the ridge                                                */
/* ------------------------------------------------------------------ */

static void put_foe(cm_game *g, int32_t wx, int kind) {
    for (int i = 0; i < CM_FOES; i++) {
        cm_foe *f = &g->foes[i];
        if (f->alive) {
            continue;
        }
        f->wx = wx;
        f->kind = (uint8_t)kind;
        f->hp = (uint8_t)(kind == CM_F_GUNNER ? CM_GUNNER_HP : 1);
        f->cool = (uint8_t)(CM_FOE_COOL / 2);
        f->step = 0;
        f->alive = true;
        return;
    }
}

static void put_crate(cm_game *g, int32_t wx) {
    for (int i = 0; i < CM_CRATES; i++) {
        cm_crate *c = &g->crates[i];
        if (c->live) {
            continue;
        }
        c->wx = wx;
        c->live = true;
        return;
    }
}

/*
 * What gets put on a chunk as it is generated.  A gunner only ever stands on
 * ground above the lowest, because a gunner the rifle can reach is a grunt
 * that takes two shots - the point of it is to be the thing that has to be
 * answered with a grenade.
 */
static void populate(cm_game *g, uint32_t k, int level) {
    int32_t wx = (int32_t)(k * CM_CHUNK + CM_CHUNK / 2);
    uint32_t r = rnd(g) % 100;

    if (r < 16) {
        put_foe(g, wx, (level > 0 && (r & 1)) ? CM_F_GUNNER : CM_F_GRUNT);
        return;
    }
    if (r < 22) {
        put_crate(g, wx);
    }
}

static void boom_at(cm_game *g, int32_t wx, int y, bool big) {
    for (int i = 0; i < CM_BOOMS; i++) {
        cm_boom *b = &g->booms[i];
        if (b->age > 0) {
            continue;
        }
        b->wx = wx;
        b->y = (int16_t)y;
        b->age = (uint8_t)(big ? 8 : 4);
        b->big = big;
        return;
    }
}

static void kill_foe(cm_game *g, cm_foe *f) {
    f->alive = false;
    g->score += f->kind == CM_F_GUNNER ? CM_V_GUNNER : CM_V_GRUNT;
    boom_at(g, f->wx, cm_foe_y(g, f) - CM_BODY_H / 2, false);
    g->sfx |= CM_SFX_KILL;
}

static void move_shots(cm_game *g) {
    for (int i = 0; i < CM_SHOTS; i++) {
        cm_shot *s = &g->shots[i];
        if (!s->live) {
            continue;
        }
        s->wx += s->vx;
        if (s->wx < g->scroll - 16 || s->wx > g->scroll + PM_PANEL + 16) {
            s->live = false;
            continue;
        }
        if (!s->mine) {
            continue;
        }
        for (int j = 0; j < CM_FOES; j++) {
            cm_foe *f = &g->foes[j];
            if (!f->alive || iabs((int)(f->wx - s->wx)) > CM_BODY_W / 2) {
                continue;
            }
            int foot = cm_foe_y(g, f);
            if (s->y < foot - CM_BODY_H || s->y > foot) {
                continue;
            }
            s->live = false;
            if (--f->hp == 0) {
                kill_foe(g, f);
            } else {
                boom_at(g, f->wx, s->y, false);
            }
            break;
        }
    }
}

static void burst(cm_game *g, int32_t wx, int y) {
    boom_at(g, wx, y, true);
    for (int i = 0; i < CM_FOES; i++) {
        cm_foe *f = &g->foes[i];
        if (!f->alive) {
            continue;
        }
        if (iabs((int)(f->wx - wx)) > CM_BLAST) {
            continue;
        }
        if (iabs(cm_foe_y(g, f) - CM_BODY_H / 2 - y) > CM_BLAST) {
            continue;
        }
        f->hp = 0;
        kill_foe(g, f);
    }
}

static void move_nades(cm_game *g) {
    for (int i = 0; i < CM_NADES; i++) {
        cm_nade *n = &g->bombs[i];
        if (!n->live) {
            continue;
        }
        n->wx += n->vx;
        n->y = (int16_t)(n->y + n->vy);
        n->vy = (int16_t)(n->vy + CM_FALL);

        int s = cm_surface(g, n->wx);
        if (s != CM_PIT && n->vy > 0 && CM_PX(n->y) >= s) {
            n->live = false;
            burst(g, n->wx, s - 4);
            continue;
        }
        if (CM_PX(n->y) > PM_PANEL || n->wx > g->scroll + PM_PANEL + 16) {
            n->live = false;
        }
    }
}

/*
 * A grunt walks at the trooper and a gunner does not, and both of them stop at
 * anything they cannot walk over - so the ridge itself is what decides where
 * an enemy ends up standing, and a hole in front of one is a thing that keeps
 * it there rather than a thing that kills it.
 */
static void move_foes(cm_game *g) {
    for (int i = 0; i < CM_FOES; i++) {
        cm_foe *f = &g->foes[i];
        if (!f->alive) {
            continue;
        }
        if (f->wx < g->scroll - 24) {
            f->alive = false;
            continue;
        }
        int foot = cm_foe_y(g, f);

        if (f->kind == CM_F_GRUNT) {
            int ahead = cm_surface(g, f->wx - CM_FOE_RUN - CM_BODY_W / 2);
            if (ahead != CM_PIT && ahead <= foot + CM_CLIMB &&
                ahead >= foot - CM_CLIMB) {
                f->wx -= CM_FOE_RUN;
                f->step++;
            }
        }

        if (f->cool > 0) {
            f->cool--;
            continue;
        }
        if (g->phase != CM_RUNNING) {
            continue;
        }
        int32_t me = hero_wx(g);
        if (f->wx <= me || f->wx - me > CM_FOE_SIGHT) {
            continue;
        }
        if (iabs(foot - hero_foot(g)) > CM_LANE) {
            continue;
        }
        fire(g, f->wx - CM_BODY_W / 2, foot - CM_BODY_H / 2, -CM_FOE_SHOT, false);
        f->cool = (uint8_t)(CM_FOE_COOL - (int)(rnd(g) % 12));
    }
}

static void take_crates(cm_game *g) {
    int32_t me = hero_wx(g);
    int feet = hero_foot(g);

    for (int i = 0; i < CM_CRATES; i++) {
        cm_crate *c = &g->crates[i];
        if (!c->live) {
            continue;
        }
        if (c->wx < g->scroll - 24) {
            c->live = false;
            continue;
        }
        int s = cm_surface(g, c->wx);
        if (s == CM_PIT) {
            c->live = false;
            continue;
        }
        if (iabs((int)(c->wx - me)) > CM_BODY_W || iabs(s - feet) > CM_BODY_H) {
            continue;
        }
        c->live = false;
        g->nades = (uint8_t)(g->nades + 3 > CM_NADES_MAX ? CM_NADES_MAX : g->nades + 3);
        g->score += CM_V_CRATE;
        g->sfx |= CM_SFX_PICKUP;
    }
}

/* ------------------------------------------------------------------ */
/* lives                                                               */
/* ------------------------------------------------------------------ */

static void lose_life(cm_game *g, int cause) {
    g->cause = (uint8_t)cause;
    g->sfx |= CM_SFX_DEATH;
    g->phase = CM_DOWN;
    g->phase_timer = CM_DOWN_HOLD;
    boom_at(g, hero_wx(g), hero_foot(g) - CM_BODY_H / 2, true);
    if (g->lives > 0) {
        g->lives--;
    }
}

static void stand_hero(cm_game *g) {
    /* never put back down over a hole, whatever the scroll happened to stop on */
    while (cm_surface(g, hero_wx(g)) == CM_PIT) {
        g->scroll += CM_CHUNK / 2;
        ensure_world(g);
    }
    g->hero_y = (int16_t)(cm_surface(g, hero_wx(g)) * CM_SUB);
    g->hero_vy = 0;
    g->airborne = false;
    g->hero_step = 0;
    g->reload = 0;
}

static void respawn(cm_game *g) {
    stand_hero(g);
    g->invuln = CM_INVULN;
    /* whatever was standing over it gets the ground it was killed on back */
    for (int i = 0; i < CM_FOES; i++) {
        if (g->foes[i].alive && g->foes[i].wx - hero_wx(g) < CM_STANDOFF) {
            g->foes[i].alive = false;
        }
    }
    for (int i = 0; i < CM_SHOTS; i++) {
        if (!g->shots[i].mine) {
            g->shots[i].live = false;
        }
    }
    g->phase = CM_RUNNING;
}

static void new_run(cm_game *g) {
    memset(g->foes, 0, sizeof(g->foes));
    memset(g->shots, 0, sizeof(g->shots));
    memset(g->bombs, 0, sizeof(g->bombs));
    memset(g->crates, 0, sizeof(g->crates));

    g->scroll = CM_START;
    g->chunk0 = 0;
    g->last_level = 0;
    g->hold_level = 0;
    g->since_pit = 0;
    /* the ground the trooper starts on is flat and empty on purpose: a hole
     * or a gunner in the first second is a life lost before anybody watching
     * has looked at the panel */
    for (uint32_t k = 0; k < CM_SPAN; k++) {
        if (k < 8) {
            g->ground[k] = 0;
            g->last_level = 0;
            g->since_pit = 3;
        } else {
            make_chunk(g, k);
        }
    }

    g->score = 0;
    g->lives = 3;
    g->nades = CM_NADES_0;
    g->invuln = 0;
    g->cause = CM_D_NONE;
    stand_hero(g);
    g->phase = CM_READY;
    g->phase_timer = CM_READY_HOLD;
}

/*
 * Anything that would take the life this frame.  A grenade of the trooper's
 * own cannot: it is thrown up a step and forwards, and a trooper that could
 * blow itself up would need the whole of the brick field's escape machinery to
 * decide whether to throw at all.
 */
static void check_hero(cm_game *g) {
    int feet = hero_foot(g);
    int32_t me = hero_wx(g);

    if (feet >= PM_PANEL) {
        lose_life(g, CM_D_FELL);
        return;
    }
    if (g->invuln > 0) {
        return;
    }
    for (int i = 0; i < CM_SHOTS; i++) {
        cm_shot *s = &g->shots[i];
        if (!s->live || s->mine) {
            continue;
        }
        if (iabs((int)(s->wx - me)) > CM_BODY_W / 2) {
            continue;
        }
        if (s->y < feet - CM_BODY_H || s->y > feet) {
            continue;
        }
        s->live = false;
        lose_life(g, CM_D_SHOT);
        return;
    }
    for (int i = 0; i < CM_FOES; i++) {
        const cm_foe *f = &g->foes[i];
        if (!f->alive || iabs((int)(f->wx - me)) > CM_BODY_W) {
            continue;
        }
        /* half a body, not a whole one: something standing on the ledge above
         * is not touching the trooper, and it is not shooting at it either -
         * the lane check sees to that - so walking under it has to be safe */
        if (iabs(cm_foe_y(g, f) - feet) > CM_BODY_H / 2) {
            continue;
        }
        lose_life(g, CM_D_TOUCHED);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* the frame                                                           */
/* ------------------------------------------------------------------ */

static void tick_booms(cm_game *g) {
    for (int i = 0; i < CM_BOOMS; i++) {
        if (g->booms[i].age > 0) {
            g->booms[i].age--;
        }
    }
}

/* the ridge and everything on it, whether or not the trooper is running */
static void the_world(cm_game *g) {
    move_foes(g);
    move_shots(g);
    move_nades(g);
    tick_booms(g);
    ensure_world(g);
}

void cm_step(cm_game *g) {
    g->sfx = 0;
    g->frame++;

    if (g->reload > 0) {
        g->reload--;
    }
    if (g->invuln > 0) {
        g->invuln--;
    }

    switch (g->phase) {
    case CM_READY:
        the_world(g);
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            g->phase = CM_RUNNING;
        }
        break;
    case CM_RUNNING: {
        uint32_t was = (uint32_t)(g->scroll / CM_STRIDE);
        pilot(g);
        ensure_world(g);
        the_world(g);
        take_crates(g);
        check_hero(g);

        /* the ground covered is most of the score, because getting further is
         * what this kind of game is for */
        uint32_t now = (uint32_t)(g->scroll / CM_STRIDE);
        if (now > was) {
            g->score += now - was;
        }
        break;
    }
    case CM_DOWN:
        the_world(g);
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            if (g->lives == 0) {
                g->phase = CM_OVER;
                g->phase_timer = CM_OVER_HOLD;
            } else {
                respawn(g);
            }
        }
        break;
    default:
        the_world(g);
        if (g->phase_timer > 0) {
            g->phase_timer--;
        }
        if (g->phase_timer == 0) {
            new_run(g);
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* the outside                                                         */
/* ------------------------------------------------------------------ */

void cm_init(cm_game *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1u;
    g->speed = 4;
    new_run(g);
    g->redraw = true;
}

void cm_set_speed(cm_game *g, uint8_t gear) { g->speed = gear ? gear : 1; }

bool cm_firing(const cm_game *g) { return g->reload >= CM_RELOAD - 1; }

bool cm_hero_visible(const cm_game *g) {
    if (g->phase == CM_DOWN || g->phase == CM_OVER) {
        return false;
    }
    return g->invuln == 0 || ((g->invuln >> 1) & 1) == 0;
}

const char *cm_banner(const cm_game *g) {
    if (g->phase == CM_READY) {
        return "GO";
    }
    if (g->phase == CM_OVER) {
        return "GAME OVER";
    }
    return NULL;
}
