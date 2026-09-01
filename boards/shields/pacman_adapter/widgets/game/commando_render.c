/*
 * Metal Slug dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "commando_render.h"

/* a full repaint is split into rows, so the band only has to hold one */
_Static_assert(PM_PANEL <= PM_BAND_PX, "panel.h's band is narrower than the panel");

static cm_palette pal;
static bool pal_ready;

void cm_render_default_palette(cm_palette *p) {
    p->sky = pm_rgb565(0x2c4a72);
    p->hill = pm_rgb565(0x3f5f4a);
    p->ground = pm_rgb565(0x6b5233);
    p->edge = pm_rgb565(0x9e8b52);
    p->hero = pm_rgb565(0xd8e0c0);
    p->hero_trim = pm_rgb565(0x2f6b2f);
    p->grunt = pm_rgb565(0xc44a3a);
    p->grunt_trim = pm_rgb565(0x2a1a14);
    p->shot = pm_rgb565(0xffe066);
    p->grenade = pm_rgb565(0x4a4a4a);
    p->boom = pm_rgb565(0xff7a1f);
    p->crate = pm_rgb565(0x00c8b4);
    p->hud = pm_rgb565(0xffee00);
}

void cm_render_set_palette(const cm_palette *p) {
    pal = *p;
    pal_ready = true;
}

/* the same shift the other renderers use, rather than more settings for
 * "that colour, darker" */
static uint16_t shade(uint16_t c, int num, int den) {
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;

    r = r * num / den;
    g = g * num / den;
    b = b * num / den;
    if (r > 31) {
        r = 31;
    }
    if (g > 63) {
        g = 63;
    }
    if (b > 31) {
        b = 31;
    }
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* ------------------------------------------------------------------ */
/* the ridge                                                           */
/* ------------------------------------------------------------------ */

/*
 * One column of the world, top to bottom.  Everything in it is a function of
 * two numbers - where the hill behind starts and where the ground in front
 * does - which is exactly what the incremental redraw below compares, so
 * anything drawn here from anything else would be a column that goes stale.
 */
static void col_span(int x, int y0, int y1, uint16_t c) {
    if (y0 < pm_by) {
        y0 = pm_by;
    }
    if (y1 > pm_by + pm_bh) {
        y1 = pm_by + pm_bh;
    }
    for (int y = y0; y < y1; y++) {
        pm_put(x, y, c);
    }
}

static void draw_column(const cm_game *g, int x) {
    int hill = cm_hill(g, x);
    int surf = cm_surface(g, g->scroll + x);
    /* the flat between the hills and the ridge, which a hole opens onto */
    uint16_t haze = shade(pal.sky, 5, 4);

    col_span(x, CM_OY, hill, pal.sky);
    col_span(x, hill, CM_HORIZON, pal.hill);
    col_span(x, CM_HORIZON, surf == CM_PIT ? PM_PANEL : surf, haze);
    if (surf == CM_PIT) {
        return;
    }
    col_span(x, surf, surf + CM_EDGE, pal.edge);
    col_span(x, surf + CM_EDGE, PM_PANEL, pal.ground);
    /* one seam below the lip, so eighty pixels of flat ground has something in
     * it.  It follows the surface rather than the world, which is what keeps
     * it free: over a flat stretch it is the same rows in every column */
    col_span(x, surf + CM_SKIN - 2, surf + CM_SKIN, shade(pal.ground, 5, 6));
}

/*
 * A sun, nailed to the panel rather than to the world.  Everything else here
 * moves with the scroll and it does not, which is exactly right for something
 * that far away - and it costs nothing, because a rectangle that is repainted
 * has it drawn back on afterwards like any other part of the sky.  Without it
 * the top third of the panel is a flat colour with nothing in it at all.
 */
#define CM_SUN_X 196
#define CM_SUN_Y 54
#define CM_SUN_R 12

_Static_assert(CM_SUN_Y + CM_SUN_R < CM_HORIZON - 12 - 2 * 13,
               "the sun would be drawn over the hills");

static void draw_sun(void) {
    uint16_t face = shade(pal.sky, 9, 5);

    for (int j = -CM_SUN_R; j <= CM_SUN_R; j++) {
        for (int i = -CM_SUN_R; i <= CM_SUN_R; i++) {
            if (i * i + j * j <= CM_SUN_R * CM_SUN_R) {
                pm_put(CM_SUN_X + i, CM_SUN_Y + j, face);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* who is on it                                                        */
/* ------------------------------------------------------------------ */

/* where a world position lands on the panel */
static int screen_x(const cm_game *g, int32_t wx) { return (int)(wx - g->scroll); }

/*
 * The trooper: helmet, head, tunic, two legs and the rifle it is holding out
 * in front.  It always faces the way it is running, which is the only way it
 * ever runs, so there is no second version of this - and the rifle is drawn
 * from the same chest height the shots leave at, so what is under fire is
 * whatever is on the line the barrel is pointing along.
 */
static void draw_hero(const cm_game *g) {
    int cx = CM_HERO_X, fy = CM_PX(g->hero_y);
    int stride = (g->hero_step >> 2) & 1;
    int chest = fy - CM_BODY_H / 2;

    /* legs: apart while running, together in the air */
    if (g->airborne) {
        pm_fill(cx - 6, fy - 9, 5, 7, pal.hero);
        pm_fill(cx + 1, fy - 11, 5, 7, pal.hero);
    } else {
        pm_fill(cx - 6 + (stride ? 3 : 0), fy - 9, 4, 9, pal.hero);
        pm_fill(cx + 2 - (stride ? 3 : 0), fy - 9, 4, 9, pal.hero);
    }
    pm_fill(cx - 5, fy - 20, 10, 11, pal.hero);
    pm_fill(cx - 5, fy - 20, 10, 2, shade(pal.hero, 5, 4));

    /* head, then the helmet over it with a brim out front */
    pm_fill(cx - 4, fy - 26, 8, 6, pal.hero);
    pm_fill(cx - 5, fy - 26, 10, 3, pal.hero_trim);
    pm_fill(cx + 4, fy - 24, 3, 2, pal.hero_trim);

    pm_fill(cx - 2, chest - 1, 12, 3, pal.hero_trim);
    if (cm_firing(g)) {
        pm_fill(cx + 10, chest - 2, 4, 5, pal.shot);
    }
}

/*
 * A grunt and a gunner, and the shapes have to differ more than the colours
 * do: which of the two is coming is the difference between shooting it and
 * having to put a grenade on it, and a preset is free to make both of them the
 * same colour.  So a grunt is upright and walks, and a gunner is squat, dug in
 * behind something and never moves.
 */
static void draw_foe(const cm_game *g, const cm_foe *f) {
    int cx = screen_x(g, f->wx), fy = cm_foe_y(g, f);
    int stride = (f->step >> 2) & 1;

    if (f->kind == CM_F_GUNNER) {
        pm_fill(cx - 7, fy - 14, 14, 14, pal.grunt);
        pm_fill(cx - 7, fy - 14, 14, 3, shade(pal.grunt, 5, 4));
        pm_fill(cx - 4, fy - 20, 8, 6, pal.grunt);
        pm_fill(cx - 4, fy - 20, 8, 2, pal.grunt_trim);
        pm_fill(cx - 12, fy - 9, 7, 3, pal.grunt_trim);
        return;
    }

    pm_fill(cx - 5 + (stride ? -3 : 0), fy - 9, 4, 9, pal.grunt);
    pm_fill(cx + 1 + (stride ? 3 : 0), fy - 9, 4, 9, pal.grunt);
    pm_fill(cx - 5, fy - 20, 10, 11, pal.grunt);
    pm_fill(cx - 4, fy - 26, 8, 6, pal.grunt);
    pm_fill(cx - 6, fy - 26, 10, 3, pal.grunt_trim);
    pm_fill(cx - 11, fy - 15, 10, 3, pal.grunt_trim);
}

/* a crate of grenades, which is the only thing on the ridge worth walking into */
static void draw_crate(const cm_game *g, const cm_crate *c) {
    int cx = screen_x(g, c->wx), fy = cm_surface(g, c->wx);

    if (fy == CM_PIT) {
        return;
    }
    pm_fill(cx - 6, fy - 12, 12, 12, pal.crate);
    pm_fill(cx - 6, fy - 12, 12, 2, shade(pal.crate, 5, 4));
    pm_fill(cx - 6, fy - 3, 12, 3, shade(pal.crate, 1, 2));
    pm_fill(cx - 1, fy - 10, 2, 8, shade(pal.crate, 1, 3));
    pm_fill(cx - 5, fy - 7, 10, 2, shade(pal.crate, 1, 3));
}

static void draw_shot(const cm_game *g, const cm_shot *s) {
    int cx = screen_x(g, s->wx);

    pm_fill(s->vx > 0 ? cx - 4 : cx, s->y - 1, 5, 2, pal.shot);
    pm_fill(cx - 1, s->y - 1, 2, 2, shade(pal.shot, 5, 4));
}

static void draw_nade(const cm_game *g, const cm_nade *n) {
    int cx = screen_x(g, n->wx), cy = CM_PX(n->y);

    pm_fill(cx - 2, cy - 3, 5, 6, pal.grenade);
    pm_fill(cx - 1, cy - 4, 3, 2, shade(pal.grenade, 5, 3));
}

/* a burst, as a ring that opens out: a filled disc at this size reads as a
 * ball somebody dropped rather than as something going off */
static void draw_boom(const cm_game *g, const cm_boom *b) {
    int cx = screen_x(g, b->wx), cy = b->y;
    int r = (b->big ? 3 : 2) + (int)(b->big ? 8 - b->age : 4 - b->age) * 2;
    int inner = r - 3;

    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            int d2 = i * i + j * j;
            if (d2 > r * r || (inner > 0 && d2 < inner * inner)) {
                continue;
            }
            if (((cx + i) * 5 + (cy + j) * 3 + b->age) % 4 == 0) {
                continue; /* the gaps that make it debris rather than a ring */
            }
            pm_put(cx + i, cy + j, ((i + j) & 2) ? pal.boom : shade(pal.boom, 5, 4));
        }
    }
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

static void draw_mark(int x, int y, int which) {
    if (which == 0) { /* a life, as the trooper's helmet */
        pm_fill(x, y + 2, 9, 3, pal.hero_trim);
        pm_fill(x + 1, y, 7, 2, pal.hero_trim);
        pm_fill(x + 1, y + 5, 7, 3, pal.hero);
        return;
    }
    pm_fill(x + 2, y + 2, 5, 6, pal.grenade); /* and a grenade */
    pm_fill(x + 3, y, 3, 2, shade(pal.grenade, 5, 3));
}

static void draw_hud(const cm_game *g) {
    char buf[8];

    pm_digits(g->score, 6, buf);
    pm_text(CM_HUD_X, CM_HUD_Y, 2, pal.hud, buf);

    for (int i = 0; i < g->lives && i < 4; i++) {
        draw_mark(PM_PANEL - CM_ICON - 4 - i * (CM_ICON + 3), CM_HUD_Y + 2, 0);
    }
    int gx = CM_GRENADE_X;
    draw_mark(gx, CM_HUD_Y + 2, 1);
    pm_digits(g->nades, 1, buf);
    pm_text(gx + CM_ICON + 2, CM_HUD_Y + 2, 2, pal.hud, buf);
}

static void draw_banner(const cm_game *g) {
    const char *word = cm_banner(g);

    if (word != NULL) {
        pm_text((PM_PANEL - pm_text_w(word, CM_BANNER_SCALE)) / 2, CM_BANNER_Y,
                CM_BANNER_SCALE, pal.hud, word);
    }
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

/*
 * Ground, then the crates standing on it, then whoever is walking about, then
 * everything in the air over the lot.  The trooper goes in front of the
 * enemies on purpose: the frames where the two are on the same pixels are the
 * frames where a life is being lost, and the thing that has to be seen then is
 * the trooper.
 */
static void paint_band(const cm_game *g, int x0, int y0, int w, int h) {
    pm_band_begin(x0, y0, w, h);

    /* the band the readout is written on; the columns start below it, so
     * nothing else on the panel ever paints these rows */
    pm_fill(x0, 0, w, CM_OY, shade(pal.ground, 1, 3));

    for (int x = x0; x < x0 + w; x++) {
        draw_column(g, x);
    }
    draw_sun();

    for (int i = 0; i < CM_CRATES; i++) {
        if (g->crates[i].live) {
            draw_crate(g, &g->crates[i]);
        }
    }
    for (int i = 0; i < CM_FOES; i++) {
        if (g->foes[i].alive) {
            draw_foe(g, &g->foes[i]);
        }
    }
    if (cm_hero_visible(g)) {
        draw_hero(g);
    }
    for (int i = 0; i < CM_SHOTS; i++) {
        if (g->shots[i].live) {
            draw_shot(g, &g->shots[i]);
        }
    }
    for (int i = 0; i < CM_NADES; i++) {
        if (g->bombs[i].live) {
            draw_nade(g, &g->bombs[i]);
        }
    }
    for (int i = 0; i < CM_BOOMS; i++) {
        if (g->booms[i].age > 0) {
            draw_boom(g, &g->booms[i]);
        }
    }

    if (y0 < CM_OY) {
        draw_hud(g);
    }
    draw_banner(g);

    pm_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, pm_band);
}

/* a rectangle wider than the band is split into as many rows as fit */
static void paint(const cm_game *g, int x0, int y0, int w, int h) {
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > PM_PANEL) {
        w = PM_PANEL - x0;
    }
    if (y0 + h > PM_PANEL) {
        h = PM_PANEL - y0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    int rows = PM_BAND_PX / w;
    for (int y = y0; y < y0 + h; y += rows) {
        int band = y0 + h - y;
        paint_band(g, x0, y, w, band < rows ? band : rows);
    }
}

/* ------------------------------------------------------------------ */
/* what changed since last time                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t x, y, w, h;
    uint16_t look;
    bool on;
} cm_box;

enum {
    CM_B_CRATE = 0,
    CM_B_FOE = CM_B_CRATE + CM_CRATES,
    CM_B_HERO = CM_B_FOE + CM_FOES,
    CM_B_SHOT = CM_B_HERO + 1,
    CM_B_NADE = CM_B_SHOT + CM_SHOTS,
    CM_B_BOOM = CM_B_NADE + CM_NADES,
    CM_BOXES = CM_B_BOOM + CM_BOOMS,
};

static uint8_t prev_surf[PM_PANEL];
static uint8_t prev_hill[PM_PANEL];
static cm_box prev[CM_BOXES];
static bool prev_valid;
static char prev_banner[12];

static uint8_t surf_look(const cm_game *g, int x) {
    int s = cm_surface(g, g->scroll + x);

    return (uint8_t)(s == CM_PIT ? CM_PIT : s);
}

static uint8_t hill_look(const cm_game *g, int x) { return (uint8_t)cm_hill(g, x); }

/*
 * How far up and down a column has to be repainted, given what it looked like
 * and what it looks like now.  This is the whole saving: a hill edge sliding
 * past changes twenty rows of a column and not the other two hundred, and a
 * step in the ground changes the twenty-seven between the two heights.  A
 * column whose outline did not move is not in here at all.
 */
static void span_of(int sp, int sn, int hp, int hn, int *lo, int *hi) {
    *lo = PM_PANEL;
    *hi = 0;

    if (hp != hn) {
        *lo = hp < hn ? hp : hn;
        *hi = hp > hn ? hp : hn;
    }
    if (sp != sn) {
        int gl, gh;
        if (sp == CM_PIT || sn == CM_PIT) {
            /* the whole of the ground below whichever of them was solid */
            gl = sp == CM_PIT ? sn : sp;
            gh = PM_PANEL;
        } else {
            gl = sp < sn ? sp : sn;
            gh = (sp > sn ? sp : sn) + CM_SKIN;
        }
        if (gl < *lo) {
            *lo = gl;
        }
        if (gh > *hi) {
            *hi = gh;
        }
    }
}

static void repaint_ground(const cm_game *g) {
    int x = 0;

    while (x < PM_PANEL) {
        uint8_t s = surf_look(g, x), h = hill_look(g, x);
        if (s == prev_surf[x] && h == prev_hill[x]) {
            x++;
            continue;
        }

        int lo, hi, l, r;
        span_of(prev_surf[x], s, prev_hill[x], h, &lo, &hi);
        int x0 = x;
        prev_surf[x] = s;
        prev_hill[x] = h;
        x++;

        /* one blit for a run of columns that all moved, which is what a slope
         * sliding past the camera always is */
        while (x < PM_PANEL) {
            s = surf_look(g, x);
            h = hill_look(g, x);
            if (s == prev_surf[x] && h == prev_hill[x]) {
                break;
            }
            span_of(prev_surf[x], s, prev_hill[x], h, &l, &r);
            if (l < lo) {
                lo = l;
            }
            if (r > hi) {
                hi = r;
            }
            prev_surf[x] = s;
            prev_hill[x] = h;
            x++;
        }
        if (hi > lo) {
            paint(g, x0, lo, x - x0, hi - lo);
        }
    }
}

static cm_box box_of(const cm_game *g, int idx) {
    cm_box b = {0, 0, 0, 0, 0, false};

    if (idx < CM_B_FOE) {
        const cm_crate *c = &g->crates[idx - CM_B_CRATE];
        int s = c->live ? cm_surface(g, c->wx) : CM_PIT;
        if (!c->live || s == CM_PIT) {
            return b;
        }
        b.x = (int16_t)(screen_x(g, c->wx) - 7);
        b.y = (int16_t)(s - 13);
        b.w = 14;
        b.h = 14;
    } else if (idx < CM_B_HERO) {
        const cm_foe *f = &g->foes[idx - CM_B_FOE];
        if (!f->alive) {
            return b;
        }
        int cx = screen_x(g, f->wx), fy = cm_foe_y(g, f);
        b.x = (int16_t)(cx - 13);
        b.y = (int16_t)(fy - 27);
        b.w = 27;
        b.h = 28;
        b.look = (uint16_t)(f->kind | (((f->step >> 2) & 1) << 1));
    } else if (idx == CM_B_HERO) {
        if (!cm_hero_visible(g)) {
            return b;
        }
        int fy = CM_PX(g->hero_y);
        b.x = CM_HERO_X - 7;
        b.y = (int16_t)(fy - CM_BODY_H - 1);
        b.w = 22;
        b.h = CM_BODY_H + 2;
        b.look = (uint16_t)(((g->hero_step >> 2) & 1) | (g->airborne << 1) |
                            (cm_firing(g) << 2));
    } else if (idx < CM_B_NADE) {
        const cm_shot *s = &g->shots[idx - CM_B_SHOT];
        if (!s->live) {
            return b;
        }
        b.x = (int16_t)(screen_x(g, s->wx) - 5);
        b.y = (int16_t)(s->y - 2);
        b.w = 11;
        b.h = 4;
        b.look = (uint16_t)(s->vx > 0);
    } else if (idx < CM_B_BOOM) {
        const cm_nade *n = &g->bombs[idx - CM_B_NADE];
        if (!n->live) {
            return b;
        }
        b.x = (int16_t)(screen_x(g, n->wx) - 3);
        b.y = (int16_t)(CM_PX(n->y) - 5);
        b.w = 7;
        b.h = 10;
    } else {
        const cm_boom *bo = &g->booms[idx - CM_B_BOOM];
        if (bo->age == 0) {
            return b;
        }
        int r = (bo->big ? 3 : 2) + (int)(bo->big ? 8 - bo->age : 4 - bo->age) * 2;
        b.x = (int16_t)(screen_x(g, bo->wx) - r - 1);
        b.y = (int16_t)(bo->y - r - 1);
        b.w = b.h = (int16_t)(2 * r + 3);
        b.look = bo->age;
    }
    b.on = true;
    return b;
}

static bool overlap(const cm_box *a, const cm_box *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const cm_game *g, const cm_box *b) { paint(g, b->x, b->y, b->w, b->h); }

static void paint_move(const cm_game *g, const cm_box *a, const cm_box *b) {
    if (!overlap(a, b)) {
        paint_box(g, a);
        paint_box(g, b);
        return;
    }
    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    paint(g, x0, y0, x1 - x0, y1 - y0);
}

/*
 * The readout is compared in three pieces rather than as a band, because the
 * score in it counts the ground covered: it moves every third frame the
 * trooper is running, and repainting the whole band each of those was more
 * pixels than the ridge and everything on it put together.
 */
enum { CM_T_SCORE = 0, CM_T_LIVES, CM_T_NADES, CM_TEXTS };

static char prev_text[CM_TEXTS][10];

static void text_key(const cm_game *g, int which, char *buf) {
    if (which == CM_T_SCORE) {
        pm_digits(g->score, 6, buf);
        return;
    }
    buf[0] = (char)('0' + (which == CM_T_LIVES ? (g->lives > 9 ? 9 : g->lives) : g->nades));
    buf[1] = '\0';
}

static void text_rect(int which, int *x, int *y, int *w, int *h) {
    *y = CM_HUD_Y - 1;
    *h = 2 * PM_GLYPH_H + 3;
    if (which == CM_T_SCORE) {
        *x = CM_HUD_X - 1;
        *w = pm_text_w("000000", 2) + 2;
        return;
    }
    if (which == CM_T_LIVES) {
        *w = 4 * (CM_ICON + 3);
        *x = PM_PANEL - *w - 2;
        return;
    }
    *x = CM_GRENADE_X - 1;
    *w = CM_ICON + 4 + pm_text_w("0", 2);
}

static void repaint_text(const cm_game *g) {
    char buf[12];

    for (int i = 0; i < CM_TEXTS; i++) {
        text_key(g, i, buf);
        if (strcmp(buf, prev_text[i]) == 0) {
            continue;
        }
        memcpy(prev_text[i], buf, strlen(buf) + 1);

        int x, y, w, h;
        text_rect(i, &x, &y, &w, &h);
        paint(g, x, y, w, h);
    }

    const char *word = cm_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so whichever is going away is wiped as well */
        int was = pm_text_w(prev_banner, CM_BANNER_SCALE);
        int is = pm_text_w(now, CM_BANNER_SCALE);
        int w = (was > is ? was : is) + 2;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (PM_PANEL - w) / 2, CM_BANNER_Y - 1, w, PM_GLYPH_H * CM_BANNER_SCALE + 2);
    }
}

static void snapshot(const cm_game *g) {
    const char *word = cm_banner(g);

    for (int x = 0; x < PM_PANEL; x++) {
        prev_surf[x] = surf_look(g, x);
        prev_hill[x] = hill_look(g, x);
    }
    for (int i = 0; i < CM_BOXES; i++) {
        prev[i] = box_of(g, i);
    }
    for (int i = 0; i < CM_TEXTS; i++) {
        text_key(g, i, prev_text[i]);
    }
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
}

void cm_render_frame(cm_game *g) {
    if (!pal_ready) {
        cm_palette def;
        cm_render_default_palette(&def);
        cm_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, PM_PANEL, PM_PANEL);
        snapshot(g);
        return;
    }

    repaint_ground(g);

    for (int i = 0; i < CM_BOXES; i++) {
        cm_box now = box_of(g, i);
        if (prev[i].on && now.on) {
            if (prev[i].x != now.x || prev[i].y != now.y || prev[i].w != now.w ||
                prev[i].h != now.h || prev[i].look != now.look) {
                paint_move(g, &prev[i], &now);
            }
        } else if (prev[i].on) {
            paint_box(g, &prev[i]);
        } else if (now.on) {
            paint_box(g, &now);
        }
        prev[i] = now;
    }

    repaint_text(g);
}
