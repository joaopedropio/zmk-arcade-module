/*
 * The well dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "tempest_render.h"

/* the band has to hold at least one row of the panel, or paint() cannot split */
_Static_assert(ARC_PANEL <= ARC_BAND_PX, "panel.h's band is narrower than the panel");

static tp_palette pal;
static bool pal_ready;

void tp_render_default_palette(tp_palette *p) {
    p->site = arc_rgb565(0x000000);
    p->well = arc_rgb565(0x2040d0);
    p->rim = arc_rgb565(0x60a0ff);
    p->claw = arc_rgb565(0xffe030);
    p->shot = arc_rgb565(0xffffff);
    p->flipper = arc_rgb565(0xff3040);
    p->tanker = arc_rgb565(0x40e060);
    p->spiker = arc_rgb565(0x30e0e0);
    p->pulsar = arc_rgb565(0xff40e0);
    p->spike = arc_rgb565(0x9060ff);
    p->bolt = arc_rgb565(0xffa020);
    p->hud = arc_rgb565(0xffee00);
}

void tp_render_set_palette(const tp_palette *p) {
    pal = *p;
    pal_ready = true;
}

/* ------------------------------------------------------------------ */
/* arithmetic                                                          */
/* ------------------------------------------------------------------ */

static int iabs(int v) { return v < 0 ? -v : v; }

/* a shade for the far rim and the inside of a body: five eighths of a colour */
static uint16_t dim(uint16_t c) {
    return (uint16_t)(((c & 0xF800) * 5 / 8) & 0xF800) |
           (uint16_t)(((c & 0x07E0) * 5 / 8) & 0x07E0) |
           (uint16_t)(((c & 0x001F) * 5 / 8) & 0x001F);
}

/* and one halfway to white, for whatever a thing's bright edge is */
static uint16_t lift(uint16_t c) {
    uint16_t r = (uint16_t)(c >> 11), g = (uint16_t)((c >> 5) & 0x3F), b = (uint16_t)(c & 0x1F);

    r = (uint16_t)(r + (31 - r) / 2);
    g = (uint16_t)(g + (63 - g) / 2);
    b = (uint16_t)(b + (31 - b) / 2);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/*
 * Floor division, so a line has the same slope either side of the point it was
 * measured from.  Truncation towards zero puts a one pixel kink at that point,
 * and a kink in a spoke is a pixel that moves when the rectangle it is drawn in
 * changes - which is a stale pixel the repaint check would find.
 */
static int fdiv(int a, int b) {
    if (b < 0) {
        a = -a;
        b = -b;
    }
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/*
 * A line, clipped to the band along whichever axis it travels furthest on.
 * Every one of the sixteen spokes crosses most of the panel, so the clip is
 * what makes a thirty pixel rectangle cost thirty pixels of spoke rather than a
 * hundred and twenty pixels of arc_put() saying no.
 */
static void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 - x0, dy = y1 - y0;

    if (iabs(dx) >= iabs(dy)) {
        if (dx == 0) {
            arc_put(x0, y0, c);
            return;
        }
        int a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0;
        if (a < arc_bx) {
            a = arc_bx;
        }
        if (b > arc_bx + arc_bw - 1) {
            b = arc_bx + arc_bw - 1;
        }
        for (int x = a; x <= b; x++) {
            arc_put(x, y0 + fdiv((x - x0) * dy, dx), c);
        }
    } else {
        int a = y0 < y1 ? y0 : y1, b = y0 < y1 ? y1 : y0;
        if (a < arc_by) {
            a = arc_by;
        }
        if (b > arc_by + arc_bh - 1) {
            b = arc_by + arc_bh - 1;
        }
        for (int y = a; y <= b; y++) {
            arc_put(x0 + fdiv((y - y0) * dx, dy), y, c);
        }
    }
}

/* the same line a pixel thicker across its own direction, for the near rim and
 * the claw - the two things that have to stand off the well behind them */
static void line2(int x0, int y0, int x1, int y1, uint16_t c) {
    line(x0, y0, x1, y1, c);
    if (iabs(x1 - x0) >= iabs(y1 - y0)) {
        line(x0, y0 + 1, x1, y1 + 1, c);
    } else {
        line(x0 + 1, y0, x1 + 1, y1, c);
    }
}

/* whether anything between these two rows could land in the band at all */
static bool band_hits(int y, int h) { return !(y + h <= arc_by || y >= arc_by + arc_bh); }

/* ------------------------------------------------------------------ */
/* the well                                                            */
/* ------------------------------------------------------------------ */

/* the four corners of a stretch of one lane, which is what almost everything
 * here is drawn inside */
typedef struct {
    int x[4], y[4];
} tp_quad;

static void quad_of(const tp_shape *s, int a8, int b8, int d0, int d1, tp_quad *q) {
    tp_at(s, a8, d0, &q->x[0], &q->y[0]);
    tp_at(s, b8, d0, &q->x[1], &q->y[1]);
    tp_at(s, b8, d1, &q->x[2], &q->y[2]);
    tp_at(s, a8, d1, &q->x[3], &q->y[3]);
}

/* the first spoke strictly past a place on the rim, going round the way the
 * lanes are numbered */
static int next_spoke(int p8) { return p8 - (((p8 % TP_SUB) + TP_SUB) % TP_SUB) + TP_SUB; }

/*
 * What a stretch of the well covers on the panel.  This is not the box round
 * the four corners of the stretch, and the difference is the whole reason it is
 * a function: the rim is a polygon, so a claw sitting across a spoke bulges past
 * the line between its two ends by however sharp that corner is - a couple of
 * pixels on a circle and eight on the square's corner.  Everything the claw and
 * a tumbling flipper are drawn from lies between the rim points here, so
 * sampling the ends and every spoke between them is exactly enough; taking only
 * the ends leaves a row of pixels nothing ever repaints.
 */
static void span_box(const tp_shape *s, int a8, int b8, int d0, int d1, int pad, int *x, int *y,
                     int *w, int *h) {
    int at[4];
    int n = 0;

    if (a8 > b8) {
        int t = a8;
        a8 = b8;
        b8 = t;
    }
    at[n++] = a8;
    for (int p = next_spoke(a8); p < b8 && n < 3; p += TP_SUB) {
        at[n++] = p;
    }
    at[n++] = b8;

    int x0 = ARC_PANEL * 4, x1 = -ARC_PANEL * 4, y0 = x0, y1 = x1;
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 2; k++) {
            int px, py;
            tp_at(s, at[i], k ? d1 : d0, &px, &py);
            if (px < x0) {
                x0 = px;
            }
            if (px > x1) {
                x1 = px;
            }
            if (py < y0) {
                y0 = py;
            }
            if (py > y1) {
                y1 = py;
            }
        }
    }
    *x = x0 - pad;
    *y = y0 - pad;
    *w = x1 - x0 + 1 + 2 * pad;
    *h = y1 - y0 + 1 + 2 * pad;
}

/*
 * The spike a spiker leaves behind, up the middle of its lane with a barb on
 * the end.  It is drawn from the far rim upwards because that is how it grows,
 * and it is the one thing in the well that is still there when everything that
 * put it there is dead.
 */
static void draw_spike(const tp_shape *s, int lane, int top) {
    int x0, y0, x1, y1;

    tp_at(s, lane * TP_SUB + TP_SUB / 2, 0, &x0, &y0);
    tp_at(s, lane * TP_SUB + TP_SUB / 2, top, &x1, &y1);
    line(x0, y0, x1, y1, pal.spike);

    int ax, ay, bx, by;
    tp_at(s, lane * TP_SUB + 2, top - 18 > 0 ? top - 18 : 0, &ax, &ay);
    tp_at(s, lane * TP_SUB + TP_SUB - 2, top - 18 > 0 ? top - 18 : 0, &bx, &by);
    line(ax, ay, x1, y1, lift(pal.spike));
    line(bx, by, x1, y1, lift(pal.spike));
}

/*
 * The board: the two rims and the sixteen spokes between them, plus whatever
 * the spikers have built.  None of it moves, so it is never repainted on its
 * own - it is redrawn behind everything else, in whatever rectangle something
 * that does move has dirtied.
 *
 * A lane a pulsar is beating in is drawn in the pulsar's colour along its whole
 * length instead of the well's, which is the only warning there is that
 * standing there is about to be fatal.  The superzapper lights every spoke the
 * same way for a few frames.
 */
static void paint_site(const tp_game *g) {
    const tp_shape *s = tp_well(g);

    arc_fill(arc_bx, arc_by, arc_bw, arc_bh, pal.site);

    for (int i = 0; i < TP_SEGS; i++) {
        int fx, fy, nx, ny, fx2, fy2, nx2, ny2;
        tp_at(s, i * TP_SUB, 0, &fx, &fy);
        tp_at(s, (i + 1) * TP_SUB, 0, &fx2, &fy2);
        tp_at(s, i * TP_SUB, TP_DEPTH, &nx, &ny);
        tp_at(s, (i + 1) * TP_SUB, TP_DEPTH, &nx2, &ny2);

        bool hot = g->zap_t > 0 || tp_lane_hot(g, i);
        uint16_t c = hot ? pal.pulsar : pal.well;

        line(fx, fy, nx, ny, c);
        if (i == TP_SEGS - 1 && !s->closed) {
            line(fx2, fy2, nx2, ny2, hot ? pal.pulsar : pal.well);
        }
        line(fx, fy, fx2, fy2, hot ? pal.pulsar : pal.well);
        line2(nx, ny, nx2, ny2, hot ? pal.pulsar : pal.rim);
    }

    for (int i = 0; i < TP_SEGS; i++) {
        if (g->spike[i] > 0) {
            draw_spike(s, i, g->spike[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* what is in it                                                       */
/* ------------------------------------------------------------------ */

/*
 * The stretch of rim an enemy covers.  A settled one covers its lane; one
 * halfway across a spoke covers a narrowing sliver either side of it, which is
 * what a tumble looks like from in front of the well - it turns edge on in the
 * middle of the flip rather than sliding across.
 */
static void foe_span(const tp_enemy *e, int *a8, int *b8) {
    if (e->flip == 0) {
        *a8 = e->lane * TP_SUB;
        *b8 = *a8 + TP_SUB;
        return;
    }
    int done = TP_FLIP_T - (int)e->flip;
    int mid = e->lane * TP_SUB + TP_SUB / 2 + e->turn * done * TP_SUB / TP_FLIP_T;
    int half = (TP_SUB / 2) * iabs(TP_FLIP_T - 2 * done) / TP_FLIP_T;

    if (half < 2) {
        half = 2;
    }
    *a8 = mid - half;
    *b8 = mid + half;
}

/* how far up the well a body reaches from where it is, kept inside the tube so
 * that one at the rim does not draw itself outside the panel */
static void body_span(int d, int *d0, int *d1) {
    *d0 = d;
    *d1 = d + TP_BODY;
    if (*d1 > TP_DEPTH) {
        *d1 = TP_DEPTH;
        *d0 = TP_DEPTH - TP_BODY;
    }
    if (*d0 < 0) {
        *d0 = 0;
    }
}

static void draw_foe(const tp_game *g, const tp_enemy *e) {
    const tp_shape *s = tp_well(g);
    int a8, b8, d0, d1;

    foe_span(e, &a8, &b8);
    body_span(e->d, &d0, &d1);

    tp_quad q;
    quad_of(s, a8, b8, d0, d1, &q);
    int mx = (q.x[0] + q.x[1] + q.x[2] + q.x[3]) / 4;
    int my = (q.y[0] + q.y[1] + q.y[2] + q.y[3]) / 4;

    switch (e->kind) {
    case TP_E_TANKER:
        /* a box with its diagonals: the one thing here that is not pointed at
         * anybody, because it is not aiming at the claw */
        line(q.x[0], q.y[0], q.x[1], q.y[1], pal.tanker);
        line(q.x[1], q.y[1], q.x[2], q.y[2], pal.tanker);
        line(q.x[2], q.y[2], q.x[3], q.y[3], pal.tanker);
        line(q.x[3], q.y[3], q.x[0], q.y[0], pal.tanker);
        line(q.x[0], q.y[0], q.x[2], q.y[2], dim(pal.tanker));
        line(q.x[1], q.y[1], q.x[3], q.y[3], dim(pal.tanker));
        break;
    case TP_E_SPIKER: {
        /* a spiral, drawn as a zigzag across the lane - it is spinning on the
         * end of the spike it is paying out */
        int phase = (int)(g->clock / 3) & 1;
        for (int k = 0; k < 3; k++) {
            int ax, ay, bx, by;
            int dd = d0 + (d1 - d0) * k / 3;
            int ee = d0 + (d1 - d0) * (k + 1) / 3;
            tp_at(s, (k + phase) & 1 ? a8 : b8, dd, &ax, &ay);
            tp_at(s, (k + phase) & 1 ? b8 : a8, ee, &bx, &by);
            line(ax, ay, bx, by, pal.spiker);
        }
        break;
    }
    case TP_E_PULSAR: {
        /* a ladder, and every rung of it lit while the beat is on */
        bool hot = tp_lane_hot(g, e->lane);
        uint16_t c = hot ? lift(pal.pulsar) : pal.pulsar;
        line(q.x[0], q.y[0], q.x[1], q.y[1], c);
        line(q.x[3], q.y[3], q.x[2], q.y[2], c);
        for (int k = 1; k < 3; k++) {
            int ax, ay, bx, by;
            int dd = d0 + (d1 - d0) * k / 3;
            tp_at(s, a8, dd, &ax, &ay);
            tp_at(s, b8, dd, &bx, &by);
            line(ax, ay, bx, by, hot ? c : dim(pal.pulsar));
        }
        break;
    }
    default:
        /* the flipper: two triangles meeting in the middle of the lane, which
         * is the shape that reads as tumbling when the sliver it is drawn in
         * narrows to nothing and opens out the other side */
        line(q.x[0], q.y[0], mx, my, pal.flipper);
        line(q.x[1], q.y[1], mx, my, pal.flipper);
        line(q.x[2], q.y[2], mx, my, pal.flipper);
        line(q.x[3], q.y[3], mx, my, pal.flipper);
        line(q.x[0], q.y[0], q.x[1], q.y[1], lift(pal.flipper));
        line(q.x[2], q.y[2], q.x[3], q.y[3], dim(pal.flipper));
        break;
    }
}

/* a shot is a bar across its lane, so the eye follows it down the tube rather
 * than losing it against the spoke it is travelling beside */
static void draw_shot(const tp_shape *s, const tp_shot *sh, uint16_t c) {
    int ax, ay, bx, by;

    tp_at(s, sh->lane * TP_SUB + 2, sh->d, &ax, &ay);
    tp_at(s, sh->lane * TP_SUB + TP_SUB - 2, sh->d, &bx, &by);
    line(ax, ay, bx, by, c);
    line(ax, ay + 1, bx, by + 1, c);
}

/*
 * The claw, straddling one lane at the near rim: two prongs down the lane's
 * edges meeting a nose pointing into the well.  It is drawn from the same
 * tp_at() as everything else, so it sits on the rim of a circle, a square and a
 * cross without knowing which it is on - and it shrinks correctly on the way
 * down the well at the end of a level, because all that changes there is the
 * depth it is asked for.
 */
static void draw_claw(const tp_game *g) {
    const tp_shape *s = tp_well(g);
    int d = tp_claw_d(g);
    int deep = d - TP_CLAW_D;
    if (deep < 0) {
        deep = 0;
    }
    int notch = d - (d - deep) / 3;
    int ax, ay, bx, by, lx, ly, rx, ry, nx, ny;

    tp_at(s, g->pos, d, &ax, &ay);
    tp_at(s, g->pos + TP_SUB, d, &bx, &by);
    tp_at(s, g->pos + 2, deep, &lx, &ly);
    tp_at(s, g->pos + TP_SUB - 2, deep, &rx, &ry);
    tp_at(s, g->pos + TP_SUB / 2, notch, &nx, &ny);

    line2(ax, ay, bx, by, pal.claw);
    line(ax, ay, lx, ly, pal.claw);
    line(lx, ly, nx, ny, pal.claw);
    line(nx, ny, rx, ry, pal.claw);
    line(rx, ry, bx, by, pal.claw);
}

/* what is left of it: an asterisk opening out where it was standing */
static void draw_spin(const tp_game *g) {
    int age = tp_spin_age(g);
    int r = 3 + age / 3;
    int x, y;

    tp_at(tp_well(g), g->pos + TP_SUB / 2, tp_claw_d(g), &x, &y);
    for (int k = 0; k < 4; k++) {
        static const int8_t DX[4] = {1, 1, 0, -1};
        static const int8_t DY[4] = {0, 1, 1, 1};
        line(x - DX[k] * r, y - DY[k] * r, x + DX[k] * r, y + DY[k] * r,
             (age & 2) ? pal.claw : lift(pal.claw));
    }
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

static void draw_hud(const tp_game *g) {
    char buf[8];

    if (!band_hits(0, TP_TOP - 1)) {
        return;
    }
    arc_digits(g->score, 6, buf);
    arc_text(TP_HUD_X, TP_HUD_Y, 2, pal.hud, buf);

    for (int i = 0; i < (int)g->lives && i < 5; i++) {
        int x = TP_LIVES_X + i * 10;
        line(x, TP_LIVES_Y + 7, x + 3, TP_LIVES_Y, pal.claw);
        line(x + 3, TP_LIVES_Y, x + 6, TP_LIVES_Y + 7, pal.claw);
    }

    /* the superzapper, drawn as the bolt it is and put out once it is spent */
    uint16_t z = g->zap > 0 ? pal.bolt : dim(pal.site);
    line(TP_ZAP_X + 6, TP_ZAP_Y, TP_ZAP_X, TP_ZAP_Y + 4, z);
    line(TP_ZAP_X, TP_ZAP_Y + 4, TP_ZAP_X + 5, TP_ZAP_Y + 4, z);
    line(TP_ZAP_X + 5, TP_ZAP_Y + 4, TP_ZAP_X, TP_ZAP_Y + 8, z);

    arc_digits(g->level, 2, buf);
    arc_text(TP_LEVEL_X, TP_LEVEL_Y, 1, pal.hud, buf);
}

static void draw_banner(const tp_game *g) {
    const char *word = tp_banner(g);

    if (word == 0 || !band_hits(TP_BANNER_Y - 4, ARC_GLYPH_H * TP_BANNER_SCALE + 8)) {
        return;
    }
    int w = arc_text_w(word, TP_BANNER_SCALE);
    arc_fill((ARC_PANEL - w) / 2 - 4, TP_BANNER_Y - 4, w + 8, ARC_GLYPH_H * TP_BANNER_SCALE + 8,
            pal.site);
    arc_text((ARC_PANEL - w) / 2, TP_BANNER_Y, TP_BANNER_SCALE, pal.hud, word);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

static void paint_band(const tp_game *g, int x0, int y0, int w, int h) {
    arc_band_begin(x0, y0, w, h);
    paint_site(g);

    const tp_shape *s = tp_well(g);
    for (int i = 0; i < TP_BOLTS; i++) {
        if (g->bolt[i].on) {
            draw_shot(s, &g->bolt[i], pal.bolt);
        }
    }
    for (int i = 0; i < TP_SHOTS; i++) {
        if (g->shot[i].on) {
            draw_shot(s, &g->shot[i], pal.shot);
        }
    }
    for (int i = 0; i < TP_ENEMIES; i++) {
        if (g->foe[i].kind != TP_E_GONE) {
            draw_foe(g, &g->foe[i]);
        }
    }
    if (g->phase == TP_DYING) {
        draw_spin(g);
    } else if (tp_claw_visible(g)) {
        draw_claw(g);
    }

    draw_hud(g);
    draw_banner(g);

    arc_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, arc_band);
}

/*
 * A rectangle wider than the band is split into as many rows as fit rather
 * than being refused: a full repaint is one call here, and the caller should
 * not have to know how big the buffer it lands in happens to be.
 */
static void paint(const tp_game *g, int x0, int y0, int w, int h) {
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > ARC_PANEL) {
        w = ARC_PANEL - x0;
    }
    if (y0 + h > ARC_PANEL) {
        h = ARC_PANEL - y0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    int rows = ARC_BAND_PX / w;
    for (int y = y0; y < y0 + h; y += rows) {
        int band = y0 + h - y;
        paint_band(g, x0, y, w, band < rows ? band : rows);
    }
}

/* ------------------------------------------------------------------ */
/* what changed since last time                                        */
/* ------------------------------------------------------------------ */

/*
 * A box, and a checksum of everything it was drawn from.  The other renderers
 * get away with a rectangle and a byte saying what a sprite looked like, but
 * nothing here is a sprite: a flipper is a trapezoid cut out of the well, and
 * its far edge can slide a pixel down the tube while the box round it does not
 * move at all - the corner that moved was not the one the box was measured
 * from.  So `look` is not a look, it is a hash of the lane, the depths and the
 * state the shape was built out of, which changes whenever a pixel of it could
 * have.  Repainting a few boxes that turned out identical is much cheaper than
 * missing the one that did not.
 */
typedef struct {
    int16_t x, y, w, h;
    uint16_t look;
    bool on;
} tp_box;

static uint16_t mix(uint16_t k, int v) { return (uint16_t)(k * 33u + (unsigned)(v + 1024)); }

/*
 * One entry per thing that can be drawn, and one per lane on top of that.  A
 * lane's entry is the spike in it and whether a pulsar is beating in it - two
 * things that change without moving anything, and that between them cover the
 * whole length of the lane rather than a sprite's worth of it.  The well itself
 * and the readout are not in here: neither moves, so the well is only ever
 * redrawn behind something else and the readout when its words change.
 */
enum {
    TP_X_FOE = 0,
    TP_X_SHOT = TP_X_FOE + TP_ENEMIES,
    TP_X_BOLT = TP_X_SHOT + TP_SHOTS,
    TP_X_LANE = TP_X_BOLT + TP_BOLTS,
    TP_X_CLAW = TP_X_LANE + TP_SEGS,
    TP_BOXES,
};

static tp_box prev[TP_BOXES];
static bool prev_valid;
static char prev_hud[16];
static char prev_banner[16];

static tp_box box_of(const tp_game *g, int idx) {
    const tp_shape *s = tp_well(g);
    tp_box b = {0, 0, 0, 0, 0, false};
    int x, y, w, h;

    if (idx < TP_X_SHOT) {
        const tp_enemy *e = &g->foe[idx];
        if (e->kind == TP_E_GONE) {
            return b;
        }
        int a8, b8, d0, d1;
        foe_span(e, &a8, &b8);
        body_span(e->d, &d0, &d1);
        span_box(s, a8, b8, d0, d1, 2, &x, &y, &w, &h);
        b.look = mix(mix(mix(mix(mix(1, a8), b8), d0), d1), e->kind);
        if (e->kind == TP_E_SPIKER) {
            b.look = mix(b.look, ((int)g->clock / 3) & 1);
        }
        if (e->kind == TP_E_PULSAR) {
            b.look = mix(b.look, tp_lane_hot(g, e->lane));
        }
    } else if (idx < TP_X_LANE) {
        const tp_shot *sh = idx < TP_X_BOLT ? &g->shot[idx - TP_X_SHOT] : &g->bolt[idx - TP_X_BOLT];
        if (!sh->on) {
            return b;
        }
        span_box(s, sh->lane * TP_SUB + 2, sh->lane * TP_SUB + TP_SUB - 2, sh->d, sh->d, 2, &x,
                 &y, &w, &h);
        b.look = mix(mix(2, sh->lane), sh->d);
    } else if (idx < TP_X_CLAW) {
        int lane = idx - TP_X_LANE;
        bool hot = g->zap_t > 0 || tp_lane_hot(g, lane);
        int top = g->spike[lane];
        if (hot) {
            top = TP_DEPTH;
        }
        if (top == 0) {
            return b;
        }
        span_box(s, lane * TP_SUB, (lane + 1) * TP_SUB, 0, top, 2, &x, &y, &w, &h);
        b.look = mix(mix(3, g->spike[lane]), hot);
    } else {
        if (g->phase == TP_DYING) {
            int r = 4 + tp_spin_age(g) / 3;
            int cx, cy;
            tp_at(s, g->pos + TP_SUB / 2, tp_claw_d(g), &cx, &cy);
            x = cx - r;
            y = cy - r;
            w = h = 2 * r + 1;
            b.look = mix(4, tp_spin_age(g));
        } else {
            if (!tp_claw_visible(g)) {
                return b;
            }
            int deep = tp_claw_d(g) - TP_CLAW_D;
            if (deep < 0) {
                deep = 0;
            }
            span_box(s, g->pos, g->pos + TP_SUB, deep, tp_claw_d(g), 3, &x, &y, &w, &h);
            b.look = mix(mix(5, g->pos), tp_claw_d(g));
        }
    }

    b.x = (int16_t)x;
    b.y = (int16_t)y;
    b.w = (int16_t)w;
    b.h = (int16_t)h;
    b.on = true;
    return b;
}

static bool same_box(const tp_box *a, const tp_box *b) {
    return a->on == b->on && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
           a->look == b->look;
}

static bool overlap(const tp_box *a, const tp_box *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const tp_game *g, const tp_box *b) { paint(g, b->x, b->y, b->w, b->h); }

/*
 * Two boxes at once where they touch, one call each where they do not.  A
 * flipper that tumbled across the far end of the well moved four pixels and a
 * claw that ran round a closed rim moved two hundred, and painting the union of
 * the second would repaint the whole panel to move one sprite.
 */
static void paint_move(const tp_game *g, const tp_box *a, const tp_box *b) {
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

/* the score, the lives, the zapper and the level, as a string to compare
 * against last frame's - the whole band is repainted when any of them moves */
static void hud_key(const tp_game *g, char *buf) {
    arc_digits(g->score, 6, buf);
    buf[6] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    buf[7] = (char)('0' + g->zap);
    arc_digits(g->level, 2, buf + 8);
}

static void snapshot_text(const tp_game *g) {
    const char *word = tp_banner(g);

    hud_key(g, prev_hud);
    memcpy(prev_banner, word != 0 ? word : "", word != 0 ? strlen(word) + 1 : 1);
}

static void repaint_text(const tp_game *g) {
    char buf[16];

    hud_key(g, buf);
    if (memcmp(buf, prev_hud, 10) != 0) {
        memcpy(prev_hud, buf, 11);
        paint(g, 0, 0, ARC_PANEL, TP_TOP - 1);
    }

    const char *word = tp_banner(g);
    const char *now = word != 0 ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so the one going away is wiped either way */
        int was = arc_text_w(prev_banner, TP_BANNER_SCALE);
        int is = arc_text_w(now, TP_BANNER_SCALE);
        int w = (was > is ? was : is) + 10;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (ARC_PANEL - w) / 2, TP_BANNER_Y - 5, w, ARC_GLYPH_H * TP_BANNER_SCALE + 10);
    }
}

void tp_render_frame(tp_game *g) {
    if (!pal_ready) {
        tp_palette def;
        tp_render_default_palette(&def);
        tp_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, ARC_PANEL, ARC_PANEL);
        for (int i = 0; i < TP_BOXES; i++) {
            prev[i] = box_of(g, i);
        }
        snapshot_text(g);
        return;
    }

    for (int i = 0; i < TP_BOXES; i++) {
        tp_box now = box_of(g, i);
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

    repaint_text(g);
}
