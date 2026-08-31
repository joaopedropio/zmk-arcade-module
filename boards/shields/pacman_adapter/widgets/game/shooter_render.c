/*
 * Space Shooter dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "shooter_render.h"

/* the band has to hold at least one row of the panel, or paint() cannot split */
_Static_assert(PM_PANEL <= PM_BAND_PX, "panel.h's band is narrower than the panel");

static ss_palette pal;
static bool pal_ready;

void ss_render_default_palette(ss_palette *p) {
    p->space = pm_rgb565(0x05060f);
    p->star = pm_rgb565(0x8899bb);
    p->ship = pm_rgb565(0x6ee7ff);
    p->trim = pm_rgb565(0xffffff);
    p->thruster = pm_rgb565(0xff8a1f);
    p->bullet = pm_rgb565(0xfff36b);
    p->rock = pm_rgb565(0x5a5f7a);
    p->rock_edge = pm_rgb565(0xa3adc9);
    p->blast = pm_rgb565(0xff5a2b);
    p->power = pm_rgb565(0x39ff9e);
    p->hud = pm_rgb565(0xffee00);
}

void ss_render_set_palette(const ss_palette *p) {
    pal = *p;
    pal_ready = true;
}

/* ------------------------------------------------------------------ */
/* the band being painted                                              */
/* ------------------------------------------------------------------ */

/*
 * Everything below stamps into whatever rectangle paint_band() is filling and
 * clips against it, rather than asking "what colour is this pixel" the way the
 * maze's renderer does.  A meteor is a few hundred pixels in a band that may
 * be the whole panel, so walking the sprites is much cheaper than walking the
 * pixels - and the answer is the same either way, because the stamps go down
 * in a fixed order and none of them reads back what is already there.
 */
static int bx, by, bw, bh;

static void put(int x, int y, uint16_t c) {
    if (x < bx || x >= bx + bw || y < by || y >= by + bh) {
        return;
    }
    uint8_t *p = pm_band + 2 * ((y - by) * bw + (x - bx));
    p[0] = (uint8_t)(c >> 8);
    p[1] = (uint8_t)(c & 0xFF);
}

static void fill(int x, int y, int w, int h, uint16_t c) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            put(i, j, c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* text                                                                */
/* ------------------------------------------------------------------ */

/*
 * Five by seven, one byte per column with the top row in bit 0 - the shape
 * every small display font has had since they were burnt into character ROMs.
 * The dashboard's fonts live in helpers/, which is Zephyr's side of the fence;
 * this file may not reach over it, and a readout of a score and two words does
 * not need more than the alphabet and the digits.
 */
#define SS_GLYPH_W 5
#define SS_GLYPH_H 7
#define SS_GLYPH_ADV 6 /* one column of air between letters */

static const uint8_t FONT[37][SS_GLYPH_W] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

static int glyph_of(char c) {
    if (c >= '0' && c <= '9') {
        return 1 + (c - '0');
    }
    if (c >= 'A' && c <= 'Z') {
        return 11 + (c - 'A');
    }
    return 0;
}

static int text_w(const char *s, int scale) {
    int n = (int)strlen(s);
    return n == 0 ? 0 : (n * SS_GLYPH_ADV - 1) * scale;
}

static void draw_text(int x, int y, int scale, uint16_t c, const char *s) {
    for (; *s != '\0'; s++, x += SS_GLYPH_ADV * scale) {
        const uint8_t *g = FONT[glyph_of(*s)];
        for (int i = 0; i < SS_GLYPH_W; i++) {
            for (int j = 0; j < SS_GLYPH_H; j++) {
                if (g[i] & (1u << j)) {
                    fill(x + i * scale, y + j * scale, scale, scale, c);
                }
            }
        }
    }
}

/* six digits, zero padded, so the readout never changes width under itself */
static void score_text(uint32_t score, char *buf) {
    for (int i = 5; i >= 0; i--) {
        buf[i] = (char)('0' + score % 10);
        score /= 10;
    }
    buf[6] = '\0';
}

/* ------------------------------------------------------------------ */
/* the things on the panel                                             */
/* ------------------------------------------------------------------ */

/*
 * The ship, as four points in its own frame with +x out of the nose: a
 * triangle with a notch cut out of the back, which is the shape this game has
 * had since it was drawn on a vector tube.  Turning it is turning those four
 * points, so there is no sprite sheet of headings and no angle it cannot be
 * at - which is the whole reason the ship is described this way rather than
 * stencilled like the sprites in the maze.
 */
static int cross(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* a point is inside a triangle when it is the same side of all three edges */
static bool in_tri(int px, int py, const int *v) {
    int d0 = cross(v[0], v[1], v[2], v[3], px, py);
    int d1 = cross(v[2], v[3], v[4], v[5], px, py);
    int d2 = cross(v[4], v[5], v[0], v[1], px, py);
    bool neg = d0 < 0 || d1 < 0 || d2 < 0;
    bool pos = d0 > 0 || d1 > 0 || d2 > 0;
    return !(neg && pos);
}

/*
 * Three bites out of a disc make a meteor.  Each is a circle in sixteenths of
 * the meteor's own radius - a centre roughly on the rim and a radius near half
 * of it - so the same table carves a six pixel rock and a fifteen pixel one,
 * and a size added later would need nothing new here.  The four rows are four
 * rocks; `spin` turns whichever one a meteor drew by a quarter at a time, so a
 * dozen of them on screen are never the same rock twice.
 */
static const int8_t ROCK_BITE[4][3][3] = {
    {{-15, -6, 8}, {11, 12, 7}, {3, -17, 6}},
    {{16, -4, 9}, {-9, 14, 6}, {-14, -12, 5}},
    {{-4, -17, 7}, {15, 8, 8}, {-16, 6, 6}},
    {{13, -13, 7}, {-13, -2, 6}, {2, 17, 8}},
};

enum { SS_RPX_OUT = 0, SS_RPX_EDGE, SS_RPX_FILL };

static int rock_px(const ss_rock *rk, int dx, int dy) {
    int r = ss_rock_r[rk->size];
    int d2 = dx * dx + dy * dy;

    if (d2 > r * r) {
        return SS_RPX_OUT;
    }
    /* a two pixel rim would be a third of a small meteor, so it gets one */
    int inner = r - (r >= 10 ? 2 : 1);
    bool edge = d2 > inner * inner;

    for (int b = 0; b < 3; b++) {
        int ox = ROCK_BITE[rk->shape][b][0];
        int oy = ROCK_BITE[rk->shape][b][1];
        int cx, cy;
        switch (rk->spin & 3) {
        case 0:
            cx = ox;
            cy = oy;
            break;
        case 1:
            cx = -oy;
            cy = ox;
            break;
        case 2:
            cx = -ox;
            cy = -oy;
            break;
        default:
            cx = oy;
            cy = -ox;
            break;
        }
        cx = cx * r / 16;
        cy = cy * r / 16;
        int br = ROCK_BITE[rk->shape][b][2] * r / 16;
        int ex = dx - cx, ey = dy - cy;
        int bd2 = ex * ex + ey * ey;
        if (bd2 <= br * br) {
            return SS_RPX_OUT;
        }
        if (bd2 <= (br + 1) * (br + 1)) {
            edge = true;
        }
    }
    return edge ? SS_RPX_EDGE : SS_RPX_FILL;
}

static void draw_rock(const ss_rock *rk) {
    int r = ss_rock_r[rk->size];
    int cx = SS_PX(rk->x), cy = SS_PX(rk->y);

    for (int y = cy - r; y <= cy + r; y++) {
        if (y < by || y >= by + bh) {
            continue;
        }
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < bx || x >= bx + bw) {
                continue;
            }
            int px = rock_px(rk, x - cx, y - cy);
            if (px != SS_RPX_OUT) {
                put(x, y, px == SS_RPX_EDGE ? pal.rock_edge : pal.rock);
            }
        }
    }
}

/* how far a blast has reached at each age; the ring behind it is 3 thick */
#define SS_BLAST_R(age) (3 + (age) * 3)

/*
 * An expanding ring rather than a filled disc: a solid ball of colour over a
 * meteor that has just gone reads as a bug, where a ring reads as the rock
 * coming apart.  The gaps are cut by position rather than at random, so the
 * same frame drawn twice comes out the same - which is what the simulator's
 * repaint check needs.
 */
static void draw_blast(const ss_blast *b) {
    int r = SS_BLAST_R(b->age);
    int inner = r - 3;

    for (int y = b->y - r; y <= b->y + r; y++) {
        if (y < by || y >= by + bh) {
            continue;
        }
        for (int x = b->x - r; x <= b->x + r; x++) {
            if (x < bx || x >= bx + bw) {
                continue;
            }
            int dx = x - b->x, dy = y - b->y;
            int d2 = dx * dx + dy * dy;
            if (d2 > r * r || (b->age > 1 && d2 < inner * inner)) {
                continue;
            }
            if (((x * 5 + y * 3 + b->age) & 3) == 0) {
                continue; /* the debris the ring breaks into */
            }
            put(x, y, pal.blast);
        }
    }
}

/*
 * A shot is a head with a short tail dragged out behind it along its own
 * course.  A single dot at seven pixels a frame reads as a flicker; the tail
 * is the frame it came from, which is what makes the direction visible on a
 * panel this size.
 */
#define SS_TAIL 3

static void draw_shot(const ss_shot *s) {
    int hx = SS_PX(s->x), hy = SS_PX(s->y);

    fill(hx, hy, 2, 2, pal.bullet);
    for (int k = 1; k <= SS_TAIL; k++) {
        put(hx - (int)s->vx * k / (4 * SS_SUB), hy - (int)s->vy * k / (4 * SS_SUB),
            pal.bullet);
    }
}

/* the letter that says which pickup it is, in a diamond that catches the eye */
static const char POWER_LETTER[SS_POWERS] = {' ', 'R', 'W', 'S'};

static void draw_drop(const ss_game *g) {
    int cx = SS_PX(g->drop.x), cy = SS_PX(g->drop.y);
    /* it turns over as it falls, so a pickup is never mistaken for a meteor */
    int r = 6 + (int)((g->frame >> 2) & 1);

    for (int y = cy - r; y <= cy + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (d <= r && d > r - 2) {
                put(x, y, pal.power);
            }
        }
    }
    char word[2] = {POWER_LETTER[g->drop.kind], '\0'};
    draw_text(cx - 2, cy - 3, 1, pal.power, word);
}

/*
 * How far from its middle the ship reaches this frame, which is both what the
 * drawing below clips to and what its dirty rectangle is sized from.  It moves
 * with the exhaust and the shield rather than being a constant, because the
 * flame is half again as long as the hull and paying for it on every frame the
 * engine is off would be a third of the ship's cost for nothing.
 */
static int ss_ship_half(const ss_game *g) {
    int half = SS_HULL_R;

    if (g->ship.thrusting && SS_FLAME_R > half) {
        half = SS_FLAME_R;
    }
    if (g->power == SS_P_SHIELD && SS_SHIELD_R + 1 > half) {
        half = SS_SHIELD_R + 1;
    }
    return half + 1;
}

/*
 * Hull, cockpit, exhaust and shield, in that order - each one over the last,
 * so the flame comes out from under the tail rather than across it.  The
 * points are turned once and then every pixel of the bounding box is asked
 * which of the shapes it is in, which is a handful of cross products on a box
 * of forty pixels a side.
 */
static void draw_ship(const ss_game *g) {
    int cx = SS_PX(g->ship.x), cy = SS_PX(g->ship.y);
    int co = ss_cos(g->ship.angle), si = ss_sin(g->ship.angle);
    int half = ss_ship_half(g);

    /* one local point onto the panel */
#define SS_TURNED(fx, fy, into, at)                                                                    do {                                                                                                   (into)[at] = cx + ((fx) * co - (fy) * si) / 1024;                                                   (into)[(at) + 1] = cy + ((fx) * si + (fy) * co) / 1024;                                         } while (0)

    int hull[6], notch[6], pit[6], flame[6];
    SS_TURNED(SS_NOSE, 0, hull, 0);
    SS_TURNED(SS_REAR, -SS_REAR_SIDE, hull, 2);
    SS_TURNED(SS_REAR, SS_REAR_SIDE, hull, 4);

    /* the bite out of the tail: the two rear corners and a point in front */
    notch[0] = hull[2];
    notch[1] = hull[3];
    notch[2] = hull[4];
    notch[3] = hull[5];
    SS_TURNED(SS_NOTCH, 0, notch, 4);

    /* the cockpit: a small mark well forward, so the nose is the bright end.
     * Any bigger and a white triangle inside a cyan one stops reading as a
     * canopy and starts reading as a hole. */
    SS_TURNED(SS_NOSE / 2, 0, pit, 0);
    SS_TURNED(0, -3, pit, 2);
    SS_TURNED(0, 3, pit, 4);

    /* and the flame, a cone out of the notch that flickers on a three frame
     * cycle - the only part of the ship that moves while the ship does not */
    int len = SS_FLAME_R - 7 + (int)(g->frame % 3) * 3;
    SS_TURNED(SS_FLAME_BASE, -SS_FLAME_HALF, flame, 0);
    SS_TURNED(SS_FLAME_BASE, SS_FLAME_HALF, flame, 2);
    SS_TURNED(-len, 0, flame, 4);
#undef SS_TURNED

    int x0 = cx - half, x1 = cx + half, y0 = cy - half, y1 = cy + half;
    if (x0 < bx) {
        x0 = bx;
    }
    if (y0 < by) {
        y0 = by;
    }
    if (x1 >= bx + bw) {
        x1 = bx + bw - 1;
    }
    if (y1 >= by + bh) {
        y1 = by + bh - 1;
    }

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (g->ship.thrusting && in_tri(x, y, flame)) {
                put(x, y, pal.thruster);
            }
            if (!in_tri(x, y, hull) || in_tri(x, y, notch)) {
                continue;
            }
            put(x, y, in_tri(x, y, pit) ? pal.trim : pal.ship);
        }
    }

    if (g->power != SS_P_SHIELD) {
        return;
    }
    /* a bubble that turns, so it reads as a field rather than a drawn circle */
    for (int y = cy - SS_SHIELD_R; y <= cy + SS_SHIELD_R; y++) {
        for (int x = cx - SS_SHIELD_R; x <= cx + SS_SHIELD_R; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 > SS_SHIELD_R * SS_SHIELD_R ||
                d2 < (SS_SHIELD_R - 1) * (SS_SHIELD_R - 1)) {
                continue;
            }
            if (((x + y + (int)g->frame) & 3) == 0) {
                continue;
            }
            put(x, y, pal.power);
        }
    }
}

/* one life, as the nose of a ship */
static void draw_life(int x, int y) {
    for (int j = 0; j < 5; j++) {
        int w = 1 + 2 * j;
        if (w > 7) {
            w = 7;
        }
        fill(x + (7 - w) / 2, y + j, w, 1, pal.ship);
    }
}

static void draw_hud(const ss_game *g) {
    char buf[8];

    score_text(g->score, buf);
    draw_text(SS_HUD_X, SS_HUD_Y, 2, pal.hud, buf);

    for (int i = 0; i < g->lives && i < 5; i++) {
        draw_life(PM_PANEL - 9 - i * 10, SS_HUD_Y + 4);
    }

    const char *power = ss_power_name(g);
    if (power != NULL) {
        draw_text(PM_PANEL - SS_HUD_X - text_w(power, 1), SS_FOOT_Y, 1, pal.power, power);
    }
}

static void draw_banner(const ss_game *g) {
    const char *word = ss_banner(g);
    if (word == NULL) {
        return;
    }
    draw_text((PM_PANEL - text_w(word, SS_BANNER_SCALE)) / 2, SS_BANNER_Y, SS_BANNER_SCALE,
              pal.hud, word);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

static void paint_band(const ss_game *g, int x0, int y0, int w, int h) {
    bx = x0;
    by = y0;
    bw = w;
    bh = h;

    fill(x0, y0, w, h, pal.space);

    for (int i = 0; i < SS_STARS; i++) {
        if (ss_star_lit(g, i)) {
            put(g->stars[i].x, g->stars[i].y, pal.star);
        }
    }
    for (int i = 0; i < SS_ROCKS; i++) {
        if (g->rocks[i].alive) {
            draw_rock(&g->rocks[i]);
        }
    }
    if (g->drop.alive) {
        draw_drop(g);
    }
    for (int i = 0; i < SS_SHOTS; i++) {
        if (g->shots[i].alive) {
            draw_shot(&g->shots[i]);
        }
    }
    if (ss_ship_visible(g)) {
        draw_ship(g);
    }
    for (int i = 0; i < SS_BLASTS; i++) {
        if (g->blasts[i].alive) {
            draw_blast(&g->blasts[i]);
        }
    }
    draw_hud(g);
    draw_banner(g);

    pm_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, pm_band);
}

/*
 * A rectangle wider than the band is split into as many rows as fit rather
 * than being refused: a full repaint is one call here, and the caller should
 * not have to know how big the buffer it lands in happens to be.
 */
static void paint(const ss_game *g, int x0, int y0, int w, int h) {
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
    bool on;
} ss_box;

/*
 * One entry per thing that can be drawn and moved.  The starfield, the readout
 * and the banner are not in here: none of them moves, so each is repainted
 * when what it says changes rather than every frame.
 */
enum { SS_B_ROCK = 0, SS_B_SHOT = SS_B_ROCK + SS_ROCKS, SS_B_BLAST = SS_B_SHOT + SS_SHOTS,
       SS_B_DROP = SS_B_BLAST + SS_BLASTS, SS_B_SHIP, SS_BOXES };

static ss_box prev[SS_BOXES];
static bool prev_valid;
static bool prev_star[SS_STARS];
static char prev_hud[16];
static char prev_foot[16];
static char prev_banner[12];

static ss_box box_of(const ss_game *g, int idx) {
    ss_box b = {0, 0, 0, 0, false};

    if (idx < SS_B_SHOT) {
        const ss_rock *r = &g->rocks[idx - SS_B_ROCK];
        if (!r->alive) {
            return b;
        }
        int rad = ss_rock_r[r->size];
        b.x = (int16_t)(SS_PX(r->x) - rad);
        b.y = (int16_t)(SS_PX(r->y) - rad);
        b.w = b.h = (int16_t)(2 * rad + 1);
    } else if (idx < SS_B_BLAST) {
        const ss_shot *s = &g->shots[idx - SS_B_SHOT];
        if (!s->alive) {
            return b;
        }
        /* the head and the far end of its tail, whichever way it is going */
        int hx = SS_PX(s->x), hy = SS_PX(s->y);
        int tx = hx - (int)s->vx * SS_TAIL / (4 * SS_SUB);
        int ty = hy - (int)s->vy * SS_TAIL / (4 * SS_SUB);
        b.x = (int16_t)(hx < tx ? hx : tx);
        b.y = (int16_t)(hy < ty ? hy : ty);
        b.w = (int16_t)((hx > tx ? hx : tx) + 2 - b.x);
        b.h = (int16_t)((hy > ty ? hy : ty) + 2 - b.y);
    } else if (idx < SS_B_DROP) {
        const ss_blast *bl = &g->blasts[idx - SS_B_BLAST];
        if (!bl->alive) {
            return b;
        }
        int r = SS_BLAST_R(bl->age);
        b.x = (int16_t)(bl->x - r);
        b.y = (int16_t)(bl->y - r);
        b.w = b.h = (int16_t)(2 * r + 1);
    } else if (idx == SS_B_DROP) {
        if (!g->drop.alive) {
            return b;
        }
        b.x = (int16_t)(SS_PX(g->drop.x) - 8);
        b.y = (int16_t)(SS_PX(g->drop.y) - 8);
        b.w = b.h = 17;
    } else {
        if (!ss_ship_visible(g)) {
            return b;
        }
        /* square, because the ship turns: whichever way it is pointing, the
         * same box holds it, and a box that changed shape with the heading
         * would leave a corner of the last one unpainted */
        int half = ss_ship_half(g);
        b.x = (int16_t)(SS_PX(g->ship.x) - half);
        b.y = (int16_t)(SS_PX(g->ship.y) - half);
        b.w = b.h = (int16_t)(2 * half + 1);
    }
    b.on = true;
    return b;
}

static bool overlap(const ss_box *a, const ss_box *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const ss_game *g, const ss_box *b) {
    paint(g, b->x, b->y, b->w, b->h);
}

/*
 * Two boxes at once where they touch, one call each where they do not.  A
 * meteor that came round the bottom of the panel is the second case, and
 * painting the union there would repaint the whole column it fell down.
 */
static void paint_move(const ss_game *g, const ss_box *a, const ss_box *b) {
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
 * What the three pieces of writing on the panel say, as strings to compare
 * against what they said last frame.  Anything that moves under them repaints
 * them on its way past - paint_band() draws them last - so all these have to
 * catch is the words themselves changing.
 */
static void hud_key(const ss_game *g, char *buf) {
    score_text(g->score, buf);
    buf[6] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    buf[7] = '\0';
}

static void foot_key(const ss_game *g, char *buf) {
    const char *power = ss_power_name(g);

    memcpy(buf, power != NULL ? power : "", power != NULL ? strlen(power) + 1 : 1);
}

static void snapshot_text(const ss_game *g) {
    const char *word = ss_banner(g);

    hud_key(g, prev_hud);
    foot_key(g, prev_foot);
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
}

static void repaint_text(const ss_game *g) {
    char buf[16];

    hud_key(g, buf);
    if (strcmp(buf, prev_hud) != 0) {
        memcpy(prev_hud, buf, strlen(buf) + 1);
        paint(g, 0, 0, PM_PANEL, SS_HUD_Y + 2 * SS_GLYPH_H + 2);
    }

    foot_key(g, buf);
    if (strcmp(buf, prev_foot) != 0) {
        memcpy(prev_foot, buf, strlen(buf) + 1);
        paint(g, 0, SS_FOOT_Y - 1, PM_PANEL, SS_GLYPH_H + 2);
    }

    const char *word = ss_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so the one going away is wiped either way */
        int was = text_w(prev_banner, SS_BANNER_SCALE);
        int is = text_w(now, SS_BANNER_SCALE);
        int w = (was > is ? was : is) + 2;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (PM_PANEL - w) / 2, SS_BANNER_Y - 1, w, SS_GLYPH_H * SS_BANNER_SCALE + 2);
    }
}

void ss_render_frame(ss_game *g) {
    if (!pal_ready) {
        ss_palette def;
        ss_render_default_palette(&def);
        ss_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, PM_PANEL, PM_PANEL);
        for (int i = 0; i < SS_BOXES; i++) {
            prev[i] = box_of(g, i);
        }
        for (int i = 0; i < SS_STARS; i++) {
            prev_star[i] = ss_star_lit(g, i);
        }
        snapshot_text(g);
        return;
    }

    for (int i = 0; i < SS_BOXES; i++) {
        ss_box now = box_of(g, i);
        if (prev[i].on && now.on) {
            paint_move(g, &prev[i], &now);
        } else if (prev[i].on) {
            paint_box(g, &prev[i]);
        } else if (now.on) {
            paint_box(g, &now);
        }
        prev[i] = now;
    }

    for (int i = 0; i < SS_STARS; i++) {
        bool lit = ss_star_lit(g, i);
        if (lit != prev_star[i]) {
            prev_star[i] = lit;
            paint(g, g->stars[i].x, g->stars[i].y, 1, 1);
        }
    }

    repaint_text(g);
}
