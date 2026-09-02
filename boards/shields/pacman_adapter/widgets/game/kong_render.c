/*
 * Girders dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "kong_render.h"

/* the band has to hold at least one row of the panel, or paint() cannot split */
_Static_assert(PM_PANEL <= PM_BAND_PX, "panel.h's band is narrower than the panel");

static dk_palette pal;
static bool pal_ready;

void dk_render_default_palette(dk_palette *p) {
    p->site = pm_rgb565(0x000000);
    p->girder = pm_rgb565(0xd83030);
    p->ladder = pm_rgb565(0x30c0f0);
    p->climber = pm_rgb565(0xe03030);
    p->climber_trim = pm_rgb565(0x3060e0);
    p->barrel = pm_rgb565(0xd08020);
    p->ape = pm_rgb565(0xa05820);
    p->ape_trim = pm_rgb565(0xf0c088);
    p->lady = pm_rgb565(0xf060a0);
    p->hammer = pm_rgb565(0xf0d040);
    p->oil = pm_rgb565(0x2060d0);
    p->hud = pm_rgb565(0xffee00);
}

void dk_render_set_palette(const dk_palette *p) {
    pal = *p;
    pal_ready = true;
}

/* half towards black, and half towards white: every shade in here is one of
 * these two, so the eleven colours settle what the whole site looks like */
static uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }
static uint16_t lift(uint16_t c) { return (uint16_t)(((c >> 1) & 0x7BEF) + 0x8410); }

static bool band_hits(int y, int h) { return y < pm_by + pm_bh && y + h > pm_by; }

/* ------------------------------------------------------------------ */
/* the site                                                            */
/* ------------------------------------------------------------------ */

/*
 * A ladder: two rails and a rung every DK_RUNG pixels, running between the
 * girder it stands on and the one it reaches.  A broken one is missing the
 * middle third of both, which is what a climber reads as "not that one" and
 * what a barrel ignores.
 */
#define DK_RUNG 5

static void draw_ladder(const dk_ladder *l) {
    int x = l->x;
    int bottom = dk_floor_y(l->gap, x);
    int top = dk_floor_y(l->gap + 1, x) + DK_BEAM;

    if (!band_hits(top, bottom - top)) {
        return;
    }
    int gap0 = top + (bottom - top) / 3;
    int gap1 = top + 2 * (bottom - top) / 3;

    for (int y = top; y < bottom; y++) {
        if (l->broken && y >= gap0 && y < gap1) {
            continue;
        }
        pm_put(x - DK_LADDER_W / 2, y, pal.ladder);
        pm_put(x + DK_LADDER_W / 2 - 1, y, pal.ladder);
    }
    for (int y = top + 2; y < bottom - 1; y += DK_RUNG) {
        if (l->broken && y >= gap0 && y < gap1) {
            continue;
        }
        pm_fill(x - DK_LADDER_W / 2, y, DK_LADDER_W, 1, dim(pal.ladder));
    }
}

/*
 * A girder, column by column, because its surface is a different height under
 * every column.  Drawn as a lit top edge over a body with a rivet every eight
 * pixels: the top edge is what makes a sloped beam read as a slope rather than
 * as a staircase, since the step between one column and the next is a single
 * pixel and the eye follows the highlight.
 */
static void draw_girder(int f) {
    int x0 = pm_bx, x1 = pm_bx + pm_bw;

    for (int x = x0; x < x1; x++) {
        int y = dk_floor_y(f, x);
        if (!band_hits(y, DK_BEAM)) {
            continue;
        }
        pm_fill(x, y, 1, DK_BEAM, pal.girder);
        pm_put(x, y, lift(pal.girder));
        if ((x & 7) == 0) {
            pm_put(x, y + 2, dim(pal.girder));
        }
    }
}

/*
 * The drum the barrels end up in, standing on the low end of the bottom
 * girder, with a flame over it.  The flame is drawn in the barrel colour rather
 * than the drum's, because a flame the colour of the thing under it reads as
 * more drum.  It is the one part of the site that moves, and it moves off the
 * clock rather than off a counter of its own - the renderer may not keep state
 * the core has not got, or a repaint would draw a different frame from the one
 * that is on the panel.
 */
static void draw_drum(const dk_game *g) {
    int base = dk_floor_y(0, DK_DRUM_X + DK_DRUM_W / 2);
    int top = base - DK_DRUM_H;

    if (band_hits(top, DK_DRUM_H)) {
        pm_fill(DK_DRUM_X, top, DK_DRUM_W, DK_DRUM_H, pal.oil);
        pm_fill(DK_DRUM_X, top, DK_DRUM_W, 2, lift(pal.oil));
        for (int i = 1; i < 4; i++) {
            pm_fill(DK_DRUM_X, top + i * DK_DRUM_H / 4, DK_DRUM_W, 1, dim(pal.oil));
        }
    }

    int phase = (int)((g->clock + g->phase_timer) / 3) & 3;
    int h = 6 + (phase == 1 || phase == 3 ? 2 : 0);
    int y = top - h;
    if (!band_hits(y, h)) {
        return;
    }
    for (int i = 0; i < h; i++) {
        int w = DK_DRUM_W - 6 - (h - 1 - i); /* narrow at the tip, wide on the rim */
        if (w < 2) {
            w = 2;
        }
        pm_fill(DK_DRUM_X + 3 + (i & 1) + (DK_DRUM_W - 6 - w) / 2, y + i, w, 1,
                i < h / 2 ? lift(pal.barrel) : pal.barrel);
    }
}

static void paint_site(const dk_game *g) {
    pm_fill(pm_bx, pm_by, pm_bw, pm_bh, pal.site);
    if (band_hits(DK_TOP - 1, 1)) {
        pm_fill(pm_bx, DK_TOP - 1, pm_bw, 1, dim(pal.girder));
    }

    for (int i = 0; i < DK_LADDERS; i++) {
        draw_ladder(&DK_LADDER[i]);
    }
    for (int f = 0; f < DK_FLOORS; f++) {
        draw_girder(f);
    }
    draw_drum(g);
}

/* ------------------------------------------------------------------ */
/* the things on it                                                    */
/* ------------------------------------------------------------------ */

/* walking, with the legs both ways; climbing, with the hands on the rails;
 * and in the air, tucked up.  Four poses is every shape he takes. */
static const char *const HERO_WALK[2][DK_ART_H] = {
    {
        "..######..",
        ".########.",
        "...oooo...",
        "...oooo...",
        "..######..",
        ".#.####.#.",
        "#..####..#",
        "...####...",
        "..oooooo..",
        "..oo..oo..",
        "..oo..oo..",
        "..oo..oo..",
        ".###..###.",
        ".###..###.",
    },
    {
        "..######..",
        ".########.",
        "...oooo...",
        "...oooo...",
        "..######..",
        "..######..",
        ".#.####.#.",
        "#..####...",
        "..oooooo..",
        "..oo..oo..",
        ".oo....oo.",
        ".oo....oo.",
        "###....###",
        "##......##",
    },
};

static const char *const HERO_CLIMB[DK_ART_H] = {
    "#........#",
    "#.######.#",
    "#..oooo..#",
    "...oooo...",
    "..######..",
    "..######..",
    "..######..",
    "..######..",
    "..oooooo..",
    "..oo..oo..",
    ".oo....oo.",
    ".oo....oo.",
    "#oo....oo#",
    "##......##",
};

static const char *const HERO_JUMP[DK_ART_H] = {
    "#..####..#",
    "#.######.#",
    "#..oooo..#",
    "...oooo...",
    "..######..",
    ".#######..",
    "..######..",
    "..oooooo..",
    "..oooooo..",
    ".oo....oo.",
    "###....###",
    "##......##",
    "..........",
    "..........",
};

/*
 * The stencil, flipped to face the way he last walked.  Flipping the lookup
 * is one line and no second copy of the art; a sheet of both headings would be
 * twice the flash for a shape that is the same shape.
 */
static void draw_hero(const dk_game *g, int cx, int feet) {
    const dk_hero *h = &g->hero;
    const char *const *art = HERO_WALK[(h->step / 4) & 1];

    if (h->state == DK_ST_CLIMB) {
        art = HERO_CLIMB;
    } else if (h->state == DK_ST_JUMP) {
        art = HERO_JUMP;
    } else if (h->vx == 0) {
        art = HERO_WALK[0];
    }

    int x0 = cx - DK_ART_W / 2, y0 = feet - DK_ART_H;
    for (int j = 0; j < DK_ART_H; j++) {
        for (int i = 0; i < DK_ART_W; i++) {
            int src = h->facing < 0 ? DK_ART_W - 1 - i : i;
            char c = art[j][src];
            if (c == '#') {
                pm_put(x0 + i, y0 + j, pal.climber);
            } else if (c == 'o') {
                pm_put(x0 + i, y0 + j, pal.climber_trim);
            }
        }
    }

    /* the hammer he is carrying, over his head and then in front of him:
     * two frames, because a hammer that only ever went up would not read as
     * being swung at anything */
    if (h->hammer > 0) {
        bool up = ((h->hammer / DK_SWING) & 1) != 0;
        int hx = up ? cx - 5 : cx + (h->facing < 0 ? -13 : 3);
        int hy = up ? y0 - 7 : y0 + 3;
        pm_fill(hx, hy, 10, 5, pal.hammer);
        pm_fill(hx + 3, hy + 5, 4, 4, dim(pal.hammer));
    }
}

/*
 * A barrel, on its side and rolling.  The hoops are what make it turn: they
 * are placed from where it is rather than from a counter, so a repaint of the
 * same frame draws the same barrel.
 */
static void draw_barrel(const dk_barrel *b) {
    int cx = DK_PX(b->x), bottom = DK_PX(b->y);
    int x0 = cx - DK_BARREL_W / 2, y0 = bottom - DK_BARREL_H;

    pm_fill(x0 + 1, y0, DK_BARREL_W - 2, DK_BARREL_H, pal.barrel);
    pm_fill(x0, y0 + 2, 1, DK_BARREL_H - 4, pal.barrel);
    pm_fill(x0 + DK_BARREL_W - 1, y0 + 2, 1, DK_BARREL_H - 4, pal.barrel);

    if (b->state == DK_B_LADDER) {
        /* end on, coming down a ladder: two rings rather than two staves */
        pm_fill(x0 + 2, y0 + 2, DK_BARREL_W - 4, DK_BARREL_H - 4, dim(pal.barrel));
        return;
    }
    int phase = ((cx + bottom) / 3) & 3;
    for (int i = 0; i < 2; i++) {
        int x = x0 + 2 + ((phase + i * 5) % (DK_BARREL_W - 3));
        pm_fill(x, y0 + 1, 1, DK_BARREL_H - 2, dim(pal.barrel));
    }
    pm_fill(x0 + 1, y0 + 1, DK_BARREL_W - 2, 1, lift(pal.barrel));
}

/*
 * The ape, in blocks rather than in art: it is thirty pixels across and the
 * whole of what it has to say is "something up there is throwing these", which
 * a silhouette does and a stencil would only do more expensively.  Its arms go
 * up while it winds one up, which is the only reason it is not part of the
 * site.
 */
static void draw_ape(const dk_game *g) {
    int x = DK_APE_X, base = dk_floor_y(DK_FLOORS - 1, DK_APE_X + DK_APE_W / 2);
    int y = base - DK_APE_H;
    int arms = g->ape > 0 ? y - 4 : y + 8;

    pm_fill(x + 8, y + 9, DK_APE_W - 16, 13, pal.ape);
    pm_fill(x + 11, y + 12, DK_APE_W - 22, 8, pal.ape_trim);
    pm_fill(x + 10, y, 14, 10, pal.ape);
    pm_fill(x + 13, y + 4, 8, 5, pal.ape_trim);
    pm_fill(x + 12, y + 2, 2, 2, pal.site);
    pm_fill(x + 20, y + 2, 2, 2, pal.site);
    pm_fill(x + 7, y + 1, 3, 4, pal.ape);
    pm_fill(x + 24, y + 1, 3, 4, pal.ape);
    pm_fill(x, arms, 8, 11, pal.ape);
    pm_fill(x + DK_APE_W - 8, arms, 8, 11, pal.ape);
    pm_fill(x + 8, y + 22, 7, 6, pal.ape);
    pm_fill(x + DK_APE_W - 15, y + 22, 7, 6, pal.ape);
}

/* her, at the far end of the top girder, with the hearts up when he gets there */
static void draw_lady(const dk_game *g) {
    int base = dk_floor_y(DK_FLOORS - 1, DK_LADY_X);
    int x = DK_LADY_X - DK_LADY_W / 2, y = base - DK_LADY_H;

    pm_fill(x + 3, y, 6, 5, dim(pal.lady));
    pm_fill(x + 4, y + 2, 4, 3, lift(pal.lady));
    pm_fill(x + 4, y + 5, 4, 3, pal.lady);
    for (int i = 0; i < 7; i++) {
        pm_fill(x + 4 - i / 2, y + 8 + i, 4 + 2 * (i / 2), 1, pal.lady);
    }
    if (g->phase != DK_WON || ((g->phase_timer / 6) & 1) == 0) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        int hx = x + (i ? DK_LADY_W - 1 : -3), hy = y - 6;
        pm_fill(hx, hy + 1, 4, 3, pal.lady);
        pm_fill(hx + 1, hy, 1, 1, pal.lady);
        pm_fill(hx + 2, hy, 1, 1, pal.lady);
        pm_fill(hx + 1, hy + 4, 2, 1, pal.lady);
    }
}

/* the hammers still hanging over the site */
static void draw_hammer(int i) {
    int x = DK_HAMMER[i].x;
    int y = dk_floor_y(DK_HAMMER[i].floor, x) - DK_HAMMER_UP;

    pm_fill(x - 5, y, 10, 5, pal.hammer);
    pm_fill(x - 1, y + 5, 3, 6, dim(pal.hammer));
}

/*
 * What is left where he was: a star that opens out over the death, the same
 * shape the crossing's splat uses and for the same reason - something has to
 * mark the spot for long enough to be seen at fifteen frames a second.
 */
static void draw_spin(const dk_game *g, int cx, int cy) {
    int r = 3 + dk_spin_age(g) / 5;

    for (int i = 0; i < 4; i++) {
        int dx = (i == 0) ? 0 : (i == 1 ? 1 : (i == 2 ? 1 : -1));
        int dy = (i == 0) ? 1 : (i == 1 ? 0 : 1);
        for (int t = -r; t <= r; t++) {
            pm_put(cx + dx * t, cy + dy * t, pal.climber);
        }
    }
    pm_fill(cx - 1, cy - 1, 3, 3, pal.climber_trim);
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

static void draw_hud(const dk_game *g) {
    char buf[8];

    if (!band_hits(0, DK_TOP)) {
        return;
    }
    pm_digits(g->score, 6, buf);
    pm_text(DK_HUD_X, DK_HUD_Y, 2, pal.hud, buf);

    for (int i = 0; i < g->lives && i < 3; i++) {
        int x = DK_LIVES_X + i * 8;
        pm_fill(x + 1, DK_LIVES_Y, 4, 3, pal.climber);
        pm_fill(x, DK_LIVES_Y + 3, 6, 4, pal.climber_trim);
        pm_fill(x + 1, DK_LIVES_Y + 7, 2, 2, pal.climber);
        pm_fill(x + 3, DK_LIVES_Y + 7, 2, 2, pal.climber);
    }

    pm_digits(g->level, 2, buf);
    pm_text(DK_LEVEL_X, DK_LEVEL_Y, 1, pal.hud, buf);
}

/*
 * The bonus, as a bar.  The last quarter of it is drawn in the girder colour
 * rather than the readout's, which is the only warning there is that the
 * clock is what is about to kill him.
 */
static void draw_clock(const dk_game *g) {
    int w = (int)((uint32_t)DK_CLOCK_W * g->clock / DK_TIME);

    if (!band_hits(DK_CLOCK_Y, DK_CLOCK_H)) {
        return;
    }
    pm_fill(DK_CLOCK_X, DK_CLOCK_Y, DK_CLOCK_W, DK_CLOCK_H, dim(pal.site));
    pm_fill(DK_CLOCK_X, DK_CLOCK_Y, w, DK_CLOCK_H,
            w * 4 < DK_CLOCK_W ? pal.girder : pal.hud);
}

static void draw_banner(const dk_game *g) {
    const char *word = dk_banner(g);

    if (word == NULL || !band_hits(DK_BANNER_Y - 4, PM_GLYPH_H * DK_BANNER_SCALE + 8)) {
        return;
    }
    int w = pm_text_w(word, DK_BANNER_SCALE);
    pm_fill((PM_PANEL - w) / 2 - 4, DK_BANNER_Y - 4, w + 8, PM_GLYPH_H * DK_BANNER_SCALE + 8,
            pal.site);
    pm_text((PM_PANEL - w) / 2, DK_BANNER_Y, DK_BANNER_SCALE, pal.hud, word);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

static void paint_band(const dk_game *g, int x0, int y0, int w, int h) {
    pm_band_begin(x0, y0, w, h);
    paint_site(g);

    for (int i = 0; i < DK_HAMMERS; i++) {
        if (g->hammer_up[i]) {
            draw_hammer(i);
        }
    }
    draw_ape(g);
    draw_lady(g);

    for (int i = 0; i < DK_BARRELS; i++) {
        if (g->barrel[i].state != DK_B_GONE) {
            draw_barrel(&g->barrel[i]);
        }
    }

    if (g->phase == DK_DYING) {
        draw_spin(g, DK_PX(g->hero.x), DK_PX(g->hero.y) - DK_HERO_H / 2);
    } else if (dk_hero_visible(g)) {
        draw_hero(g, DK_PX(g->hero.x), DK_PX(g->hero.y));
    }

    draw_hud(g);
    draw_clock(g);
    draw_banner(g);

    pm_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, pm_band);
}

/*
 * A rectangle wider than the band is split into as many rows as fit rather
 * than being refused: a full repaint is one call here, and the caller should
 * not have to know how big the buffer it lands in happens to be.
 */
static void paint(const dk_game *g, int x0, int y0, int w, int h) {
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
    uint8_t look; /* anything about it that changes without moving */
    bool on;
} dk_box;

/*
 * One entry per thing that can be drawn: the barrels, the two hammers, the
 * climber, the ape, her, and the flame in the drum.  The site and the readout
 * are not in here - neither moves, so the site is never repainted on its own
 * and the readout is repainted when its words change.
 */
enum {
    DK_X_BARREL = 0,
    DK_X_HAMMER = DK_X_BARREL + DK_BARRELS,
    DK_X_HERO = DK_X_HAMMER + DK_HAMMERS,
    DK_X_APE,
    DK_X_LADY,
    DK_X_FLAME,
    DK_BOXES,
};

static dk_box prev[DK_BOXES];
static bool prev_valid;
static char prev_hud[16];
static char prev_banner[12];
static int prev_clock = -1;

static dk_box box_of(const dk_game *g, int idx) {
    dk_box b = {0, 0, 0, 0, 0, false};

    if (idx < DK_X_HAMMER) {
        const dk_barrel *r = &g->barrel[idx];
        if (r->state == DK_B_GONE) {
            return b;
        }
        int cx = DK_PX(r->x), bottom = DK_PX(r->y);
        b.x = (int16_t)(cx - DK_BARREL_W / 2);
        b.y = (int16_t)(bottom - DK_BARREL_H);
        b.w = DK_BARREL_W;
        b.h = DK_BARREL_H;
        b.look = (uint8_t)((((cx + bottom) / 3) & 3) | (r->state == DK_B_LADDER ? 4 : 0));
    } else if (idx < DK_X_HERO) {
        int i = idx - DK_X_HAMMER;
        if (!g->hammer_up[i]) {
            return b;
        }
        b.x = (int16_t)(DK_HAMMER[i].x - 5);
        b.y = (int16_t)(dk_floor_y(DK_HAMMER[i].floor, DK_HAMMER[i].x) - DK_HAMMER_UP);
        b.w = 10;
        b.h = 11;
    } else if (idx == DK_X_HERO) {
        const dk_hero *h = &g->hero;
        if (g->phase == DK_DYING) {
            int r = 4 + dk_spin_age(g) / 5;
            b.x = (int16_t)(DK_PX(h->x) - r);
            b.y = (int16_t)(DK_PX(h->y) - DK_HERO_H / 2 - r);
            b.w = b.h = (int16_t)(2 * r + 1);
            b.look = (uint8_t)(dk_spin_age(g) | 0x80);
        } else {
            if (!dk_hero_visible(g)) {
                return b;
            }
            b.x = (int16_t)(DK_PX(h->x) - DK_ART_W / 2);
            b.y = (int16_t)(DK_PX(h->y) - DK_ART_H);
            b.w = DK_ART_W;
            b.h = DK_ART_H;
            /* the hammer reaches above his head and out in front of him, so
             * it is the box as well as the look */
            if (h->hammer > 0) {
                b.x -= 8;
                b.y -= 8;
                b.w += 16;
                b.h += 10;
            }
            uint8_t pose = (uint8_t)(h->state == DK_ST_CLIMB   ? 4
                                     : h->state == DK_ST_JUMP  ? 5
                                     : h->vx == 0              ? 0
                                                               : 1 + ((h->step / 4) & 1));
            b.look = (uint8_t)(pose | (h->facing < 0 ? 8 : 0) |
                               (h->hammer > 0 ? (((h->hammer / DK_SWING) & 1) ? 16 : 32) : 0));
        }
    } else if (idx == DK_X_APE) {
        b.x = DK_APE_X - 1;
        b.y = (int16_t)(dk_floor_y(DK_FLOORS - 1, DK_APE_X + DK_APE_W / 2) - DK_APE_H - 5);
        b.w = DK_APE_W + 2;
        b.h = DK_APE_H + 6;
        b.look = (uint8_t)(g->ape > 0);
    } else if (idx == DK_X_LADY) {
        int base = dk_floor_y(DK_FLOORS - 1, DK_LADY_X);
        b.x = (int16_t)(DK_LADY_X - DK_LADY_W / 2 - 4);
        b.y = (int16_t)(base - DK_LADY_H - 7);
        b.w = DK_LADY_W + 8;
        b.h = DK_LADY_H + 7;
        b.look = (uint8_t)(g->phase == DK_WON ? 1 + ((g->phase_timer / 6) & 1) : 0);
    } else {
        int base = dk_floor_y(0, DK_DRUM_X + DK_DRUM_W / 2);
        b.x = DK_DRUM_X;
        b.y = (int16_t)(base - DK_DRUM_H - 8);
        b.w = DK_DRUM_W;
        b.h = 8;
        b.look = (uint8_t)((((int)g->clock + (int)g->phase_timer) / 3) & 3);
    }
    b.on = true;
    return b;
}

static bool same_box(const dk_box *a, const dk_box *b) {
    return a->on == b->on && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
           a->look == b->look;
}

static bool overlap(const dk_box *a, const dk_box *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const dk_game *g, const dk_box *b) { paint(g, b->x, b->y, b->w, b->h); }

/*
 * Two boxes at once where they touch, one call each where they do not.  A
 * barrel that has just been thrown is the second case, and painting the union
 * there would repaint the diagonal of the whole site it appeared across.
 */
static void paint_move(const dk_game *g, const dk_box *a, const dk_box *b) {
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

/* the score, the lives and the level, as a string to compare against last
 * frame's - the whole band is repainted when any of the three moves */
static void hud_key(const dk_game *g, char *buf) {
    pm_digits(g->score, 6, buf);
    buf[6] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    pm_digits(g->level, 2, buf + 7);
}

static void snapshot_text(const dk_game *g) {
    const char *word = dk_banner(g);

    hud_key(g, prev_hud);
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
    prev_clock = (int)((uint32_t)DK_CLOCK_W * g->clock / DK_TIME);
}

/*
 * The bonus bar loses a pixel every seven frames, so what is repainted is the
 * strip between where it ended last frame and where it ends now rather than
 * the bar.  A bar that grew - a fresh life - repaints all of it, and so does
 * one that crossed the quarter mark, since that changes its colour.
 */
static void repaint_clock(const dk_game *g) {
    int now = (int)((uint32_t)DK_CLOCK_W * g->clock / DK_TIME);

    if (now == prev_clock) {
        return;
    }
    int lo = now < prev_clock ? now : prev_clock;
    int hi = now > prev_clock ? now : prev_clock;
    if ((now * 4 < DK_CLOCK_W) != (prev_clock * 4 < DK_CLOCK_W)) {
        lo = 0;
    }
    prev_clock = now;
    paint(g, DK_CLOCK_X + lo, DK_CLOCK_Y, hi - lo + 1, DK_CLOCK_H);
}

static void repaint_text(const dk_game *g) {
    char buf[16];

    hud_key(g, buf);
    if (memcmp(buf, prev_hud, 9) != 0) {
        memcpy(prev_hud, buf, 10);
        paint(g, 0, 0, PM_PANEL, DK_TOP - 1);
    }

    const char *word = dk_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so the one going away is wiped either way */
        int was = pm_text_w(prev_banner, DK_BANNER_SCALE);
        int is = pm_text_w(now, DK_BANNER_SCALE);
        int w = (was > is ? was : is) + 10;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (PM_PANEL - w) / 2, DK_BANNER_Y - 5, w, PM_GLYPH_H * DK_BANNER_SCALE + 10);
    }
}

void dk_render_frame(dk_game *g) {
    if (!pal_ready) {
        dk_palette def;
        dk_render_default_palette(&def);
        dk_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, PM_PANEL, PM_PANEL);
        for (int i = 0; i < DK_BOXES; i++) {
            prev[i] = box_of(g, i);
        }
        snapshot_text(g);
        return;
    }

    for (int i = 0; i < DK_BOXES; i++) {
        dk_box now = box_of(g, i);
        if (same_box(&prev[i], &now)) {
            continue; /* it did not move and it does not look different */
        }
        if (prev[i].on && now.on) {
            paint_move(g, &prev[i], &now);
        } else if (prev[i].on) {
            paint_box(g, &prev[i]);
        } else {
            paint_box(g, &now);
        }
        prev[i] = now;
    }

    repaint_clock(g);
    repaint_text(g);
}
