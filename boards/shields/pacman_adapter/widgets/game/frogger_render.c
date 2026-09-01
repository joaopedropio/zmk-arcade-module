/*
 * Crossing dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "frogger_render.h"

/* the band has to hold at least one row of the panel, or paint() cannot split */
_Static_assert(PM_PANEL <= PM_BAND_PX, "panel.h's band is narrower than the panel");

static fr_palette pal;
static bool pal_ready;

void fr_render_default_palette(fr_palette *p) {
    p->water = pm_rgb565(0x1030a8);
    p->road = pm_rgb565(0x101014);
    p->bank = pm_rgb565(0x1f7a3a);
    p->hedge = pm_rgb565(0x0f5a2a);
    p->frog = pm_rgb565(0x7bf03c);
    p->frog_eye = pm_rgb565(0xffffff);
    p->log = pm_rgb565(0x8a5a2b);
    p->turtle = pm_rgb565(0xf0a020);
    p->car = pm_rgb565(0xe03c3c);
    p->truck = pm_rgb565(0xd8d8e0);
    p->splat = pm_rgb565(0xffffff);
    p->fly = pm_rgb565(0xff40c0);
    p->hud = pm_rgb565(0xffee00);
}

void fr_render_set_palette(const fr_palette *p) {
    pal = *p;
    pal_ready = true;
}

/*
 * Half and half again towards white.  Every shade this game needs beyond the
 * thirteen colours is one of these two - the ripples in the water, the grain
 * in a log, the windows of a car, a raft on its way under - so setting the
 * water colour sets what the water looks like rather than only what it is.
 */
static uint16_t dim(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }
static uint16_t lift(uint16_t c) { return (uint16_t)(((c >> 1) & 0x7BEF) + 0x8410); }

/* ------------------------------------------------------------------ */
/* the ground                                                          */
/* ------------------------------------------------------------------ */

static bool band_hits(int y, int h) { return y < pm_by + pm_bh && y + h > pm_by; }

/*
 * A run of dashes across the panel, clipped to the band before it is walked
 * rather than after.  Every texture here is one of these, and a dirty
 * rectangle fifty pixels wide should not cost fifteen calls that miss.
 */
static void dashes(int y, int stride, int len, int phase, uint16_t c) {
    if (!band_hits(y, 1)) {
        return;
    }
    int first = pm_bx - len + 1 - phase;
    first = (first > 0 ? first / stride : (first - stride + 1) / stride) * stride + phase;
    for (int x = first; x < pm_bx + pm_bw; x += stride) {
        pm_fill(x, y, len, 1, c);
    }
}

/*
 * What a row is made of.  Dispatched on the row rather than on the colour it
 * came out as: two of the four are free to be set to the same value, and a
 * texture picked by comparing colours would put ripples on the road the moment
 * somebody made the river black.
 */
enum { FR_G_HEDGE = 0, FR_G_WATER, FR_G_ROAD, FR_G_BANK };

static int row_ground(int row) {
    if (row == FR_ROW_HOME) {
        return FR_G_HEDGE;
    }
    if (row >= FR_ROW_RIVER && row < FR_ROW_MEDIAN) {
        return FR_G_WATER;
    }
    if (row >= FR_ROW_ROAD && row < FR_ROW_START) {
        return FR_G_ROAD;
    }
    return FR_G_BANK;
}

static uint16_t ground_color(int ground) {
    switch (ground) {
    case FR_G_HEDGE:
        return pal.hedge;
    case FR_G_WATER:
        return pal.water;
    case FR_G_ROAD:
        return pal.road;
    default:
        return pal.bank;
    }
}

/*
 * The board with nothing on it: two readout bands, thirteen rows of ground,
 * and the five bays cut out of the hedge.  It is drawn from the layout alone
 * and so is the same every time, which is what lets everything else be a
 * rectangle of it with a sprite stamped back on top.
 */
static void paint_ground(void) {
    if (band_hits(0, FR_TOP)) {
        pm_fill(pm_bx, 0, pm_bw, FR_TOP, pal.road);
    }
    if (band_hits(FR_FOOT, PM_PANEL - FR_FOOT)) {
        pm_fill(pm_bx, FR_FOOT, pm_bw, PM_PANEL - FR_FOOT, pal.road);
    }

    for (int row = 0; row < FR_ROWS; row++) {
        int y = FR_ROW_Y(row);
        if (!band_hits(y, FR_CELL)) {
            continue;
        }
        int ground = row_ground(row);
        uint16_t base = ground_color(ground);
        pm_fill(pm_bx, y, pm_bw, FR_CELL, base);

        if (ground == FR_G_WATER) {
            /* three ripples a row, offset row by row so the river does not
             * come out as a grid of dashes */
            for (int i = 0; i < 3; i++) {
                dashes(y + 3 + 5 * i, 16, 5, (row * 5 + i * 7) % 16, dim(base));
            }
        } else if (ground == FR_G_BANK) {
            dashes(y + 4, 7, 2, 2, dim(base));
            dashes(y + 11, 7, 2, 5, dim(base));
        } else if (ground == FR_G_HEDGE) {
            for (int i = 0; i < 4; i++) {
                dashes(y + 2 + 4 * i, 8, 3, (i & 1) ? 4 : 0, dim(base));
            }
        }
    }

    /* the bays, which are the river seen through the hedge */
    int y = FR_ROW_Y(FR_ROW_HOME);
    if (!band_hits(y, FR_CELL)) {
        return;
    }
    for (int b = 0; b < FR_BAYS; b++) {
        int x = FR_BAY_COL(b) * FR_CELL;
        pm_fill(x, y, FR_CELL, FR_CELL, pal.water);
        pm_fill(x, y, FR_CELL, 1, dim(pal.hedge));
    }
}

/* ------------------------------------------------------------------ */
/* the things on it                                                    */
/* ------------------------------------------------------------------ */

/*
 * Frog, sitting and mid-hop, facing up.  '#' is body, 'o' is eye; the hopping
 * pose has the same body with its legs thrown out, which at twelve pixels is
 * the whole difference between a frog and a green square.
 */
static const char *const FROG_SIT[FR_ART] = {
    "..o......o..",
    "..oo.##.oo..",
    "...######...",
    ".#.######.#.",
    "##.######.##",
    "#..######..#",
    "...######...",
    "...######...",
    "..########..",
    ".##..##..##.",
    "##...##...##",
    "#....##....#",
};

static const char *const FROG_HOP[FR_ART] = {
    "..o......o..",
    "..oo.##.oo..",
    "#..######..#",
    "#..######..#",
    "##.######.##",
    ".#.######.#.",
    "...######...",
    "..########..",
    ".#.######.#.",
    "##..####..##",
    "#...####...#",
    "#...####...#",
};

/*
 * The stencil turned to face the way it last hopped.  Turning the lookup is
 * four lines and no second copy of the art; a sprite sheet of four headings
 * would be four times the flash for a shape that is the same shape.
 */
static void draw_frog(int cx, int cy, int facing, bool hopping, uint16_t body,
                      uint16_t eye) {
    const char *const *art = hopping ? FROG_HOP : FROG_SIT;
    int x0 = cx - FR_ART / 2, y0 = cy - FR_ART / 2;

    for (int v = 0; v < FR_ART; v++) {
        for (int u = 0; u < FR_ART; u++) {
            char c = art[v][u];
            if (c == '.') {
                continue;
            }
            int x, y;
            switch (facing) {
            case 1:
                x = FR_ART - 1 - v;
                y = u;
                break;
            case 2:
                x = FR_ART - 1 - u;
                y = FR_ART - 1 - v;
                break;
            case 3:
                x = v;
                y = FR_ART - 1 - u;
                break;
            default:
                x = u;
                y = v;
                break;
            }
            pm_put(x0 + x, y0 + y, c == 'o' ? eye : body);
        }
    }
}

/* a log: square ends would read as a crate, so the corners come off */
static void draw_log(int x, int y, int w, int phase) {
    for (int j = 0; j < FR_SPRITE_H; j++) {
        int inset = (j == 0 || j == FR_SPRITE_H - 1) ? 2 : (j == 1 || j == FR_SPRITE_H - 2 ? 1 : 0);
        pm_fill(x + inset, y + j, w - 2 * inset, 1, pal.log);
    }
    /* grain along it, and the two cut ends */
    for (int j = 3; j < FR_SPRITE_H - 2; j += 3) {
        for (int i = 4 + (phase + j) % 5; i < w - 4; i += 7) {
            pm_fill(x + i, y + j, 3, 1, dim(pal.log));
        }
    }
    pm_fill(x + 2, y + 2, 1, FR_SPRITE_H - 4, dim(pal.log));
    pm_fill(x + w - 3, y + 2, 1, FR_SPRITE_H - 4, dim(pal.log));
}

/*
 * A raft of turtles, one shell each.  Wider than they are tall, because a lane
 * is ten pixels and a circle that fits in ten is smaller than the gaps between
 * them - an ellipse fills the raft at the height there is.  A sinking one is
 * drawn smaller and darker for the dozen frames before it goes under, which is
 * the only warning there is, and the only reason a frog ever hops off a
 * perfectly good raft.
 */
#define FR_TURT_A 6 /* half axes: across the lane, and down it */
#define FR_TURT_B 4

_Static_assert(2 * FR_TURT_B + 1 <= FR_SPRITE_H, "a turtle is taller than its lane");
_Static_assert(2 * FR_TURT_A + 2 <= FR_W_TURT, "a turtle is wider than its place in the raft");

static void draw_raft(int x, int y, int w, int sunk, bool rightward) {
    int a = FR_TURT_A - sunk * 3 / FR_DIVE_WARN;
    int b = FR_TURT_B - sunk * 2 / FR_DIVE_WARN;
    uint16_t shell = sunk > 0 ? dim(pal.turtle) : pal.turtle;
    int cy = y + FR_SPRITE_H / 2;
    int span = a * a * b * b;

    for (int t = 0; t < w / FR_W_TURT; t++) {
        int cx = x + t * FR_W_TURT + FR_W_TURT / 2;
        /* the head, so a raft has a front and the lane has a direction */
        pm_fill(rightward ? cx + a : cx - a - 1, cy - 1, 2, 2, shell);
        for (int j = -b; j <= b; j++) {
            for (int i = -a; i <= a; i++) {
                int q = i * i * b * b + j * j * a * a;
                if (q > span) {
                    continue;
                }
                /* the boss of the shell, which is what tells one from a stone */
                pm_put(cx + i, cy + j, q * 4 <= span ? lift(shell) : shell);
            }
        }
    }
}

/*
 * The traffic.  Three shapes out of two colours: a car is a box with windows,
 * a truck is a cab and a trailer with a gap between them, and a racer is a
 * lighter car with its nose drawn out to a point.  The racer takes a shade of
 * the car's colour rather than a colour of its own because eleven settings for
 * one game is already a long list, and a lighter red still reads as a
 * different vehicle where a second red would not.
 */
static void draw_car(int x, int y, int w, int kind, bool rightward) {
    uint16_t body = kind == FR_K_TRUCK ? pal.truck : (kind == FR_K_RACER ? lift(pal.car) : pal.car);
    uint16_t glass = dim(body);

    if (kind == FR_K_TRUCK) {
        int cab = 10;
        int nose = rightward ? x + w - cab : x;
        int box = rightward ? x : x + cab + 2;
        pm_fill(box, y + 1, w - cab - 2, FR_SPRITE_H - 2, body);
        pm_fill(nose, y, cab, FR_SPRITE_H, body);
        pm_fill(nose + 2, y + 2, cab - 4, 3, glass);
        for (int i = 4; i < w - cab - 6; i += 6) {
            pm_fill(box + i, y + 3, 3, FR_SPRITE_H - 6, glass);
        }
        return;
    }

    for (int j = 0; j < FR_SPRITE_H; j++) {
        int inset = (j == 0 || j == FR_SPRITE_H - 1) ? 1 : 0;
        /* the racer's leading end is drawn out to a point: the further a row
         * is from the middle of the car, the further back along it that row
         * starts.  It is the only cue on the panel for which way a lane runs
         * before anything reaches an edge */
        int lead = 0;
        if (kind == FR_K_RACER) {
            lead = j < FR_SPRITE_H / 2 ? FR_SPRITE_H / 2 - 1 - j : j - FR_SPRITE_H / 2;
        }
        pm_fill(x + inset + (rightward ? 0 : lead), y + j, w - 2 * inset - lead, 1, body);
    }
    pm_fill(x + w / 2 - 3, y + 2, 6, FR_SPRITE_H - 4, glass);
}

/* the fly, which is worth going out of the way for and has to look like it */
static void draw_fly(int cx, int cy) {
    pm_fill(cx - 2, cy - 1, 5, 3, pal.fly);
    pm_fill(cx - 4, cy - 2, 2, 1, lift(pal.fly));
    pm_fill(cx + 3, cy - 2, 2, 1, lift(pal.fly));
    pm_put(cx, cy - 2, pal.fly);
}

/*
 * What is left where the frog was.  A ring of spokes rather than a blob: at
 * twelve pixels a filled circle reads as a coin, where spokes read as
 * something that used to be a frog.
 */
static void draw_splat(int cx, int cy, int age) {
    int r = 3 + age / 4;

    for (int i = -r; i <= r; i++) {
        pm_put(cx + i, cy, pal.splat);
        pm_put(cx, cy + i, pal.splat);
        if ((i & 1) == 0) {
            pm_put(cx + i, cy + i, pal.splat);
            pm_put(cx + i, cy - i, pal.splat);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

/* one life, as a frog's head seen from above */
static void draw_life(int x, int y) {
    pm_fill(x, y + 2, 8, 5, pal.frog);
    pm_fill(x + 1, y, 2, 2, pal.frog);
    pm_fill(x + 5, y, 2, 2, pal.frog);
    pm_put(x + 1, y, pal.frog_eye);
    pm_put(x + 6, y, pal.frog_eye);
}

static void draw_hud(const fr_game *g) {
    char buf[8];

    pm_digits(g->score, 6, buf);
    pm_text(FR_HUD_X, FR_HUD_Y, 2, pal.hud, buf);

    for (int i = 0; i < g->lives && i < 5; i++) {
        draw_life(PM_PANEL - 10 - i * 10, FR_HUD_Y + 3);
    }
}

static void draw_clock(const fr_game *g) {
    int w = (int)((uint32_t)FR_CLOCK_W * g->clock / FR_TIME);

    if (w > FR_CLOCK_W) {
        w = FR_CLOCK_W;
    }
    if (w > 0) {
        /* it goes from the bank's green to the splat's white as it runs out,
         * because a bar that only gets shorter is a bar nobody looks at */
        pm_fill(FR_CLOCK_X, FR_CLOCK_Y, w, FR_CLOCK_H,
                w * 4 < FR_CLOCK_W ? pal.splat : pal.bank);
    }

    char buf[6] = {'L', 'V', ' ', 0, 0, 0};
    pm_digits(g->level, 2, buf + 3);
    pm_text(PM_PANEL - FR_HUD_X - pm_text_w(buf, 1), FR_CLOCK_Y - 1, 1, pal.hud, buf);
}

static void draw_banner(const fr_game *g) {
    const char *word = fr_banner(g);

    if (word == NULL) {
        return;
    }
    int w = pm_text_w(word, FR_BANNER_SCALE);
    pm_fill((PM_PANEL - w) / 2 - 4, FR_BANNER_Y - 4, w + 8, PM_GLYPH_H * FR_BANNER_SCALE + 8,
            pal.road);
    pm_text((PM_PANEL - w) / 2, FR_BANNER_Y, FR_BANNER_SCALE, pal.hud, word);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

/* the bays' contents, which are the only things drawn in the home row */
static void paint_bays(const fr_game *g) {
    int y = FR_ROW_Y(FR_ROW_HOME);

    for (int b = 0; b < FR_BAYS; b++) {
        int cx = FR_BAY_COL(b) * FR_CELL + FR_CELL / 2;
        if (g->bay[b]) {
            /* they flash together while the level is being cleared */
            bool lit = g->phase != FR_LEVEL || ((g->phase_timer / 6) & 1);
            draw_frog(cx, y + FR_CELL / 2, 0, false, lit ? pal.frog : pal.hedge,
                      lit ? pal.frog_eye : pal.hedge);
        } else if (g->fly == b) {
            draw_fly(cx, y + FR_CELL / 2);
        }
    }
}

static void paint_band(const fr_game *g, int x0, int y0, int w, int h) {
    pm_band_begin(x0, y0, w, h);
    paint_ground();
    paint_bays(g);

    for (int row = 0; row < FR_ROWS; row++) {
        const fr_lane *l = &g->lanes[row];
        int y = FR_ROW_Y(row) + (FR_CELL - FR_SPRITE_H) / 2;

        if (l->count == 0 || !band_hits(y, FR_SPRITE_H)) {
            continue;
        }
        for (int i = 0; i < l->count; i++) {
            int x = fr_mover_x(g, l, i, 0);
            if (x >= pm_bx + pm_bw || x + (int)l->span[i] <= pm_bx) {
                continue;
            }
            switch (l->kind) {
            case FR_K_LOG:
                draw_log(x, y, l->span[i], l->phase[i]);
                break;
            case FR_K_TURTLE: {
                int sunk = fr_turtle_sunk(g, l, i, 0);
                if (sunk < FR_DIVE_WARN) {
                    draw_raft(x, y, l->span[i], sunk, l->speed > 0);
                }
                break;
            }
            default:
                draw_car(x, y, l->span[i], l->kind, l->speed > 0);
                break;
            }
        }
    }

    if (g->phase == FR_DYING) {
        draw_splat(FR_PX(g->frog.x), FR_PX(g->frog.y), fr_splat_age(g));
    } else if (fr_frog_visible(g)) {
        draw_frog(FR_PX(g->frog.x), FR_PX(g->frog.y), g->frog.facing, g->frog.hop > 0, pal.frog,
                  pal.frog_eye);
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
static void paint(const fr_game *g, int x0, int y0, int w, int h) {
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
} fr_box;

/*
 * One entry per thing that can be drawn: every place in every lane, the five
 * bays, and the frog.  The ground and the readout are not in here - neither
 * moves, so the ground is never repainted on its own and the readout is
 * repainted when its words change.
 */
enum {
    FR_B_LANE = 0,
    FR_B_BAY = FR_B_LANE + FR_ROWS * FR_LANE_MAX,
    FR_B_FROG = FR_B_BAY + FR_BAYS,
    FR_BOXES,
};

static fr_box prev[FR_BOXES];
static bool prev_valid;
static char prev_hud[16];
static char prev_level[8];
static char prev_banner[12];
static int prev_clock = -1;

static fr_box box_of(const fr_game *g, int idx) {
    fr_box b = {0, 0, 0, 0, 0, false};

    if (idx < FR_B_BAY) {
        const fr_lane *l = &g->lanes[idx / FR_LANE_MAX];
        int i = idx % FR_LANE_MAX;
        if (i >= l->count) {
            return b;
        }
        int sunk = fr_turtle_sunk(g, l, i, 0);
        if (sunk >= FR_DIVE_WARN) {
            return b; /* under the water, and so nothing to draw */
        }
        b.x = (int16_t)fr_mover_x(g, l, i, 0);
        b.y = (int16_t)(FR_ROW_Y(idx / FR_LANE_MAX) + (FR_CELL - FR_SPRITE_H) / 2);
        b.w = (int16_t)l->span[i];
        b.h = FR_SPRITE_H;
        b.look = (uint8_t)sunk;
    } else if (idx < FR_B_FROG) {
        int bay = idx - FR_B_BAY;
        if (!g->bay[bay] && g->fly != bay) {
            return b;
        }
        b.x = (int16_t)(FR_BAY_COL(bay) * FR_CELL + (FR_CELL - FR_ART) / 2);
        b.y = (int16_t)(FR_ROW_Y(FR_ROW_HOME) + (FR_CELL - FR_ART) / 2);
        b.w = b.h = FR_ART;
        /* full, or a fly, or one of the two frames of the level flash */
        b.look = (uint8_t)(g->bay[bay] ? (g->phase == FR_LEVEL ? 2 + ((g->phase_timer / 6) & 1) : 1)
                                       : 4);
    } else {
        if (g->phase == FR_DYING) {
            int r = 3 + fr_splat_age(g) / 4 + 1;
            b.x = (int16_t)(FR_PX(g->frog.x) - r);
            b.y = (int16_t)(FR_PX(g->frog.y) - r);
            b.w = b.h = (int16_t)(2 * r + 1);
            b.look = (uint8_t)(fr_splat_age(g) | 0x80);
        } else {
            if (!fr_frog_visible(g)) {
                return b;
            }
            b.x = (int16_t)(FR_PX(g->frog.x) - FR_ART / 2);
            b.y = (int16_t)(FR_PX(g->frog.y) - FR_ART / 2);
            b.w = b.h = FR_ART;
            b.look = (uint8_t)(g->frog.facing | (g->frog.hop > 0 ? 8 : 0));
        }
    }
    b.on = true;
    return b;
}

static bool same_box(const fr_box *a, const fr_box *b) {
    return a->on == b->on && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h &&
           a->look == b->look;
}

static bool overlap(const fr_box *a, const fr_box *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const fr_game *g, const fr_box *b) { paint(g, b->x, b->y, b->w, b->h); }

/*
 * Two boxes at once where they touch, one call each where they do not.  A
 * mover that has just wrapped round the loop is the second case, and painting
 * the union there would repaint the whole lane it crossed.
 */
static void paint_move(const fr_game *g, const fr_box *a, const fr_box *b) {
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

/* the score and the lives, as a string to compare against last frame's */
static void hud_key(const fr_game *g, char *buf) {
    pm_digits(g->score, 6, buf);
    buf[6] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    buf[7] = '\0';
}

static void level_key(const fr_game *g, char *buf) { pm_digits(g->level, 2, buf); }

static void snapshot_text(const fr_game *g) {
    const char *word = fr_banner(g);

    hud_key(g, prev_hud);
    level_key(g, prev_level);
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
    prev_clock = (int)((uint32_t)FR_CLOCK_W * g->clock / FR_TIME);
}

/*
 * The clock is the one part of the readout that changes on its own, and it
 * only ever loses a pixel or two at a time - so what is repainted is the strip
 * between where the bar ended last frame and where it ends now, rather than
 * the bar.  A bar that grew (a new life) repaints all of it.
 */
static void repaint_clock(const fr_game *g) {
    int now = (int)((uint32_t)FR_CLOCK_W * g->clock / FR_TIME);

    if (now == prev_clock) {
        return;
    }
    int lo = now < prev_clock ? now : prev_clock;
    int hi = now > prev_clock ? now : prev_clock;
    /* the last quarter is drawn in a different colour, so crossing that mark
     * either way repaints the whole bar rather than only the end of it - and
     * it is crossed the other way every time a life starts */
    if ((now * 4 < FR_CLOCK_W) != (prev_clock * 4 < FR_CLOCK_W)) {
        lo = 0;
    }
    prev_clock = now;
    paint(g, FR_CLOCK_X + lo, FR_CLOCK_Y, hi - lo + 1, FR_CLOCK_H);
}

static void repaint_text(const fr_game *g) {
    char buf[16];

    hud_key(g, buf);
    if (strcmp(buf, prev_hud) != 0) {
        memcpy(prev_hud, buf, strlen(buf) + 1);
        paint(g, 0, 0, PM_PANEL, FR_TOP);
    }

    level_key(g, buf);
    if (strcmp(buf, prev_level) != 0) {
        memcpy(prev_level, buf, strlen(buf) + 1);
        paint(g, PM_PANEL / 2, FR_FOOT, PM_PANEL / 2, PM_PANEL - FR_FOOT);
    }

    const char *word = fr_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so the one going away is wiped either way */
        int was = pm_text_w(prev_banner, FR_BANNER_SCALE);
        int is = pm_text_w(now, FR_BANNER_SCALE);
        int w = (was > is ? was : is) + 10;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (PM_PANEL - w) / 2, FR_BANNER_Y - 5, w, PM_GLYPH_H * FR_BANNER_SCALE + 10);
    }
}

void fr_render_frame(fr_game *g) {
    if (!pal_ready) {
        fr_palette def;
        fr_render_default_palette(&def);
        fr_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, PM_PANEL, PM_PANEL);
        for (int i = 0; i < FR_BOXES; i++) {
            prev[i] = box_of(g, i);
        }
        snapshot_text(g);
        return;
    }

    for (int i = 0; i < FR_BOXES; i++) {
        fr_box now = box_of(g, i);
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
