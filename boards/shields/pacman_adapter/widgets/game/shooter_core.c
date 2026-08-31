/*
 * Space Shooter dongle - game core (portable).
 *
 * A wave of meteors drifts down, the ship breaks them into smaller ones, and
 * the wave after that is faster and holds more.  Nobody is playing it: the
 * ship works out which meteor will reach its row first, leads it, and shoots
 * where it is going to be - unless something is about to land on it, in which
 * case getting out of the way outranks the shot.
 *
 * SPDX-License-Identifier: MIT
 */

#include "shooter_core.h"

const uint8_t ss_rock_r[SS_SIZES] = {6, 10, 15};

/* how long each thing lasts, in frames */
#define SS_BANNER_FRAMES 45
#define SS_DEAD_FRAMES   30
#define SS_OVER_FRAMES   90
#define SS_INVULN        60
#define SS_POWER_FRAMES  400
#define SS_BLAST_AGES    6

#define SS_COOLDOWN       9
#define SS_COOLDOWN_RAPID 3
#define SS_SHOT_VY        (7 * SS_SUB)
#define SS_SPREAD_VX      (2 * SS_SUB)

/*
 * A wave that nobody can finish would leave the panel showing the same two
 * meteors until the dongle is unplugged, so after this many frames without a
 * hit the lowest meteor burns up on its own.  It is the same bargain
 * pacman_core.c's `hungry` makes, and it should never fire in a soak.
 */
#define SS_PATIENCE 900

#define SS_LIVES 3

static uint32_t rnd(ss_game *g) {
    g->rng = g->rng * 1664525u + 1013904223u;
    return g->rng >> 8;
}

static int range(ss_game *g, int lo, int hi) {
    return lo + (int)(rnd(g) % (uint32_t)(hi - lo + 1));
}

/* ------------------------------------------------------------------ */
/* spawning                                                            */
/* ------------------------------------------------------------------ */

static ss_rock *free_rock(ss_game *g) {
    for (int i = 0; i < SS_ROCKS; i++) {
        if (!g->rocks[i].alive) {
            return &g->rocks[i];
        }
    }
    return NULL;
}

static int rocks_alive(const ss_game *g) {
    int n = 0;
    for (int i = 0; i < SS_ROCKS; i++) {
        n += g->rocks[i].alive ? 1 : 0;
    }
    return n;
}

/*
 * Meteors fall faster every wave, but only up to a point: past about two
 * pixels a frame one can cross the gap between two frames of the ship's own
 * movement, and the dodge stops being something the ship can win.
 */
static int wave_fall(const ss_game *g) {
    int v = 8 + (int)g->wave;
    return v > 18 ? 18 : v;
}

static void spawn_rock(ss_game *g, uint8_t size) {
    ss_rock *r = free_rock(g);
    if (r == NULL) {
        return;
    }
    int rad = ss_rock_r[size];

    r->alive = true;
    r->size = size;
    r->shape = (uint8_t)range(g, 0, 3);
    r->spin = (uint8_t)range(g, 0, 3);
    r->x = (int16_t)(range(g, rad, PM_PANEL - rad) * SS_SUB);
    /* above the top edge, spread out so a wave arrives as a shower */
    r->y = (int16_t)(-range(g, rad, rad + 70) * SS_SUB);
    r->vx = (int16_t)range(g, -7, 7);
    /* the big ones are the slow ones, the way a heavier rock reads */
    r->vy = (int16_t)(wave_fall(g) - size * 2 + range(g, 0, 3));
    if (r->vy < 2) {
        r->vy = 2;
    }
}

static void start_wave(ss_game *g) {
    int big = 2 + (int)g->wave / 2;
    if (big > SS_WAVE_MAX) {
        big = SS_WAVE_MAX;
    }
    for (int i = 0; i < big; i++) {
        spawn_rock(g, SS_BIG);
    }
    g->phase = SS_READY;
    g->phase_timer = SS_BANNER_FRAMES;
    g->patient = 0;
}

/*
 * A pickup every third wave.  Often enough that one is usually running, rare
 * enough that seeing one drop is still an event.
 */
static void maybe_drop(ss_game *g) {
    if (g->drop.alive || (g->wave % 3) != 0) {
        return;
    }
    g->drop.alive = true;
    g->drop.kind = (uint8_t)range(g, SS_P_RAPID, SS_POWERS - 1);
    g->drop.x = (int16_t)(range(g, 20, PM_PANEL - 20) * SS_SUB);
    g->drop.y = (int16_t)(-12 * SS_SUB);
    g->drop.vy = 8;
}

static void blast_at(ss_game *g, int x, int y) {
    ss_blast *oldest = &g->blasts[0];
    for (int i = 0; i < SS_BLASTS; i++) {
        if (!g->blasts[i].alive) {
            oldest = &g->blasts[i];
            break;
        }
        if (g->blasts[i].age > oldest->age) {
            oldest = &g->blasts[i];
        }
    }
    oldest->alive = true;
    oldest->age = 0;
    oldest->x = (int16_t)x;
    oldest->y = (int16_t)y;
}

static const uint16_t ROCK_SCORE[SS_SIZES] = {100, 50, 20};

/*
 * Breaking a meteor: two of the size below, thrown apart sideways so the pair
 * does not simply carry on as one.  A small one leaves nothing, which is what
 * makes a wave finite.
 */
static void break_rock(ss_game *g, ss_rock *r) {
    int x = r->x, y = r->y, vx = r->vx, vy = r->vy;
    uint8_t size = r->size;

    g->score += ROCK_SCORE[size];
    g->patient = 0;
    r->alive = false;
    blast_at(g, SS_PX(x), SS_PX(y));

    if (size == SS_SMALL) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        ss_rock *c = free_rock(g);
        if (c == NULL) {
            return;
        }
        c->alive = true;
        c->size = (uint8_t)(size - 1);
        c->shape = (uint8_t)range(g, 0, 3);
        c->spin = (uint8_t)range(g, 0, 3);
        c->x = (int16_t)x;
        c->y = (int16_t)y;
        c->vx = (int16_t)(vx + (i == 0 ? -range(g, 3, 7) : range(g, 3, 7)));
        c->vy = (int16_t)(vy + range(g, 1, 3));
    }
}

/* ------------------------------------------------------------------ */
/* the ship's own mind                                                 */
/* ------------------------------------------------------------------ */

/*
 * Everything the ship decides is built on one question: where will that meteor
 * be in n frames.  Where to stand is wherever they will not be, and when to
 * shoot is when one of them will be in front of the barrel by the time the
 * shot gets there.  Both need the answer folded off the side walls, because a
 * meteor bounces on the way and aiming at the unfolded number puts the shot
 * through the wall it came off.
 */
static int iabs(int v) { return v < 0 ? -v : v; }

static int fold(int x, int lo, int hi) {
    while (x < lo || x > hi) {
        if (x < lo) {
            x = 2 * lo - x;
        }
        if (x > hi) {
            x = 2 * hi - x;
        }
    }
    return x;
}

static int rock_x_at(const ss_rock *r, int t) {
    int lo = ss_rock_r[r->size] * SS_SUB;
    int hi = (PM_PANEL - ss_rock_r[r->size]) * SS_SUB;
    return fold(r->x + (int)r->vx * t, lo, hi);
}

/* frames until it crosses the ship's row; -1 for one that is already past */
static int frames_to_row(const ss_rock *r) {
    int dy = SS_SHIP_MID * SS_SUB - r->y;
    if (r->vy <= 0 || dy < 0) {
        return -1;
    }
    return dy / r->vy;
}

/*
 * How far ahead the ship bothers to look.  Past this the meteors have bounced
 * enough times that the prediction is guesswork, and planning against
 * guesswork just makes the ship twitch.
 */
#define SS_HORIZON 100

/*
 * Clearance past which one column is no better than another.  Without a cap
 * the ship would spend the whole wave chasing the single safest pixel on the
 * panel and never stop to line a shot up; with one, anywhere safe enough is
 * equally good and the aim below is what decides between them.
 */
#define SS_SAFE_ENOUGH (28 * SS_SUB)

/* how close the nearest meteor comes if the ship waits at x */
static int safety(const ss_game *g, int x) {
    int worst = SS_SAFE_ENOUGH;

    for (int i = 0; i < SS_ROCKS; i++) {
        const ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int t = frames_to_row(r);
        if (t < 0 || t > SS_HORIZON) {
            continue;
        }
        int miss = iabs(rock_x_at(r, t) - x) -
                   (ss_rock_r[r->size] + SS_SHIP_R + 3) * SS_SUB;
        /* one still a long way up is less of a problem than one arriving */
        miss += t * SS_SUB / 3;
        if (miss < worst) {
            worst = miss;
        }
    }
    return worst;
}

/* how well a shot fired from x would land, in eighths of overlap */
static int aim(const ss_game *g, int x) {
    int best = 0;
    /* the fanned shots cover a wider column than the single one */
    int slack = g->power == SS_P_SPREAD ? 10 * SS_SUB : 0;

    for (int i = 0; i < SS_ROCKS; i++) {
        const ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int dy = SS_SHIP_Y * SS_SUB - r->y;
        if (dy <= 0) {
            continue; /* already level with the ship; shooting up misses it */
        }
        /* the shot rises while the meteor falls, so they close at the sum */
        int t = dy / (SS_SHOT_VY + r->vy);
        if (t > SS_HORIZON) {
            continue;
        }
        int hit = ss_rock_r[r->size] * SS_SUB + slack - iabs(rock_x_at(r, t) - x);
        if (hit > best) {
            best = hit;
        }
    }
    return best;
}

/*
 * Which column to be in: the one that scores best on staying alive first and
 * hitting something second, with a little weight left over for not crossing
 * the panel to get there.  Sampling every few pixels rather than every one is
 * plenty - the ship cannot travel further than that in a frame anyway - and it
 * keeps the whole decision to a few hundred comparisons.
 */
#define SS_PLAN_STEP (5 * SS_SUB)

static int ship_target(const ss_game *g) {
    int lo = (SS_SHIP_W / 2) * SS_SUB;
    int hi = (PM_PANEL - SS_SHIP_W / 2 - 1) * SS_SUB;

    /* a pickup is worth crossing the panel for, so long as it is safe there */
    if (g->drop.alive && g->drop.y > 0 && safety(g, g->drop.x) > 0) {
        return g->drop.x;
    }

    int best = g->ship_x;
    int best_score = safety(g, g->ship_x) * 4 + aim(g, g->ship_x);

    for (int x = lo; x <= hi; x += SS_PLAN_STEP) {
        int score = safety(g, x) * 4 + aim(g, x) - iabs(x - g->ship_x) / 6;
        if (score > best_score) {
            best_score = score;
            best = x;
        }
    }
    return best;
}

static void fire(ss_game *g) {
    int fired = 0;
    int want = g->power == SS_P_SPREAD ? 3 : 1;

    for (int i = 0; i < SS_SHOTS && fired < want; i++) {
        if (g->shots[i].alive) {
            continue;
        }
        g->shots[i].alive = true;
        g->shots[i].x = g->ship_x;
        g->shots[i].y = (int16_t)(SS_SHIP_Y * SS_SUB);
        g->shots[i].vx = (int16_t)(want == 1 ? 0 : (fired - 1) * SS_SPREAD_VX);
        fired++;
    }
    g->cooldown = g->power == SS_P_RAPID ? SS_COOLDOWN_RAPID : SS_COOLDOWN;
}

/* ------------------------------------------------------------------ */
/* one frame                                                           */
/* ------------------------------------------------------------------ */

static void move_ship(ss_game *g) {
    int want = ship_target(g);
    int step = g->speed * SS_SUB;
    int dx = want - g->ship_x;

    if (dx > step) {
        dx = step;
    }
    if (dx < -step) {
        dx = -step;
    }
    g->ship_x = (int16_t)(g->ship_x + dx);

    int lo = (SS_SHIP_W / 2) * SS_SUB;
    int hi = (PM_PANEL - SS_SHIP_W / 2 - 1) * SS_SUB;
    if (g->ship_x < lo) {
        g->ship_x = (int16_t)lo;
    }
    if (g->ship_x > hi) {
        g->ship_x = (int16_t)hi;
    }
}

static void move_rocks(ss_game *g) {
    for (int i = 0; i < SS_ROCKS; i++) {
        ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        r->x = (int16_t)(r->x + r->vx);
        r->y = (int16_t)(r->y + r->vy);

        int rad = ss_rock_r[r->size] * SS_SUB;
        if (r->x < rad && r->vx < 0) {
            r->x = (int16_t)rad;
            r->vx = (int16_t)(-r->vx);
        }
        if (r->x > PM_PANEL * SS_SUB - rad && r->vx > 0) {
            r->x = (int16_t)(PM_PANEL * SS_SUB - rad);
            r->vx = (int16_t)(-r->vx);
        }

        /*
         * A meteor that reaches the bottom comes round again rather than being
         * counted as cleared: a wave ends when it is shot, not when it is
         * outlasted, or the ship would learn that standing still works.
         */
        if (r->y > (PM_PANEL + ss_rock_r[r->size]) * SS_SUB) {
            r->y = (int16_t)(-ss_rock_r[r->size] * SS_SUB);
            r->x = (int16_t)(range(g, ss_rock_r[r->size], PM_PANEL - ss_rock_r[r->size]) * SS_SUB);
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
        s->y = (int16_t)(s->y - SS_SHOT_VY);
        s->x = (int16_t)(s->x + s->vx);
        if (s->y < -6 * SS_SUB || s->x < 0 || s->x > PM_PANEL * SS_SUB) {
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

static void take_drop(ss_game *g) {
    if (!g->drop.alive) {
        return;
    }
    g->drop.y = (int16_t)(g->drop.y + g->drop.vy);
    if (g->drop.y > (PM_PANEL + 12) * SS_SUB) {
        g->drop.alive = false;
        return;
    }
    if (!ss_ship_visible(g)) {
        return;
    }
    int dx = SS_PX(g->drop.x - g->ship_x), dy = SS_PX(g->drop.y) - SS_SHIP_MID;
    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }
    if (dx < SS_SHIP_W / 2 && dy < SS_SHIP_H) {
        g->drop.alive = false;
        g->power = (ss_power)g->drop.kind;
        g->power_left = SS_POWER_FRAMES;
    }
}

static void lose_ship(ss_game *g) {
    blast_at(g, SS_PX(g->ship_x), SS_SHIP_MID);
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
        int dx = SS_PX(r->x - g->ship_x), dy = SS_PX(r->y) - SS_SHIP_MID;
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

/* the wave nobody could finish: burn the lowest meteor and carry on */
static void break_stalemate(ss_game *g) {
    ss_rock *low = NULL;
    for (int i = 0; i < SS_ROCKS; i++) {
        if (g->rocks[i].alive && (low == NULL || g->rocks[i].y > low->y)) {
            low = &g->rocks[i];
        }
    }
    if (low != NULL) {
        break_rock(g, low);
    }
}

static void respawn(ss_game *g) {
    g->ship_x = (int16_t)(PM_PANEL * SS_SUB / 2);
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

    move_rocks(g);
    move_shots(g);
    age_blasts(g);
    hit_rocks(g);
    take_drop(g);

    if (ss_ship_visible(g)) {
        move_ship(g);
        if (g->cooldown == 0 && aim(g, g->ship_x) > 0) {
            fire(g);
        }
        hit_ship(g);
    }

    if (++g->patient >= SS_PATIENCE) {
        g->patient = 0;
        break_stalemate(g);
    }

    switch (g->phase) {
    case SS_READY:
        if (g->phase_timer > 0 && --g->phase_timer == 0) {
            g->phase = SS_FLY;
        }
        break;
    case SS_FLY:
        if (rocks_alive(g) == 0) {
            g->wave++;
            maybe_drop(g);
            start_wave(g);
        }
        break;
    case SS_DEAD:
        if (--g->phase_timer == 0) {
            respawn(g);
            g->phase = SS_FLY;
        }
        break;
    case SS_OVER:
        if (--g->phase_timer == 0) {
            uint32_t seed = g->rng;
            ss_init(g, seed);
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
        g->stars[i].x = (uint8_t)range(g, 1, PM_PANEL - 2);
        g->stars[i].y = (uint8_t)range(g, 1, PM_PANEL - 2);
        /* two thirds of them hold steady; the rest blink, slowly and apart */
        g->stars[i].period = (uint8_t)(range(g, 0, 2) == 0 ? range(g, 24, 90) : 0);
        g->stars[i].phase = (uint8_t)range(g, 0, 89);
    }
}

void ss_init(ss_game *g, uint32_t seed) {
    /* whatever the last words per minute set, which outlives a restart */
    uint8_t speed = g->speed;

    for (unsigned i = 0; i < sizeof(*g); i++) {
        ((uint8_t *)g)[i] = 0;
    }
    g->rng = seed ? seed : 1u;
    g->speed = speed ? speed : 4;
    g->lives = SS_LIVES;
    g->wave = 1;
    g->redraw = true;

    scatter_stars(g);
    respawn(g);
    start_wave(g);
}

void ss_set_speed(ss_game *g, uint8_t px) {
    g->speed = px ? px : 1;
}

bool ss_ship_visible(const ss_game *g) {
    if (g->phase == SS_DEAD || g->phase == SS_OVER) {
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

const char *ss_banner(const ss_game *g, char *buf, int len) {
    if (g->phase == SS_OVER) {
        return "GAME OVER";
    }
    if (g->phase != SS_READY || len < 9) {
        return NULL;
    }

    static const char word[] = "WAVE ";
    int n = 0;
    while (word[n] != '\0') {
        buf[n] = word[n];
        n++;
    }
    unsigned wave = g->wave > 99 ? 99 : g->wave;
    if (wave >= 10) {
        buf[n++] = (char)('0' + wave / 10);
    }
    buf[n++] = (char)('0' + wave % 10);
    buf[n] = '\0';
    return buf;
}
