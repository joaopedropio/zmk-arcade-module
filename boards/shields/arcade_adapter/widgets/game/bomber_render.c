/*
 * Bomberman dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "bomber_render.h"

/* a full repaint is split into rows, so the band only has to hold one */
_Static_assert(ARC_PANEL <= ARC_BAND_PX, "panel.h's band is narrower than the panel");

static bb_palette pal;
static bool pal_ready;

void bb_render_default_palette(bb_palette *p) {
    p->floor = arc_rgb565(0x1c6b2a);
    p->solid = arc_rgb565(0x9aa2ad);
    p->brick = arc_rgb565(0xb5561e);
    p->brick_edge = arc_rgb565(0x6e2f10);
    p->bomb = arc_rgb565(0x14161c);
    p->flame = arc_rgb565(0xff6a00);
    p->flame_hot = arc_rgb565(0xffe066);
    p->bomber = arc_rgb565(0xf2f2f2);
    p->bomber_trim = arc_rgb565(0x1e73ff);
    p->foe = arc_rgb565(0xe23b6d);
    p->foe_eye = arc_rgb565(0xffffff);
    p->pickup = arc_rgb565(0x00d2ff);
    p->door = arc_rgb565(0xffd400);
    p->hud = arc_rgb565(0xffee00);
}

void bb_render_set_palette(const bb_palette *p) {
    pal = *p;
    pal_ready = true;
}

/*
 * A pillar wants a lit edge and a shaded one, and the floor wants to be two
 * tones so a board of it does not read as a painted wall.  Both are the same
 * colour moved rather than a colour of their own: four more settings to say
 * "the same green, slightly darker" would be four more things to get wrong in
 * a preset, and the shift is exact in 5-6-5 because each channel is scaled
 * whole.
 */
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
/* the ground                                                          */
/* ------------------------------------------------------------------ */

/* where a cell lands on the panel; the board is pushed down by the readout */
static int cell_x(int c) { return c * BB_CELL; }
static int cell_y(int r) { return BB_OY + r * BB_CELL; }

static void draw_floor(int x, int y, int r, int c) {
    /* a checker of the same colour, which is what gives a big open board its
     * scale back once the bricks that used to give it one have been blown */
    uint16_t base = ((r + c) & 1) ? shade(pal.floor, 7, 8) : pal.floor;
    arc_fill(x, y, BB_CELL, BB_CELL, base);
}

static void draw_solid(int x, int y, bool flash) {
    uint16_t face = flash ? pal.flame_hot : pal.solid;

    arc_fill(x, y, BB_CELL, BB_CELL, face);
    arc_fill(x, y, BB_CELL, 2, shade(face, 5, 4));
    arc_fill(x, y, 2, BB_CELL, shade(face, 5, 4));
    arc_fill(x, y + BB_CELL - 2, BB_CELL, 2, shade(face, 5, 8));
    arc_fill(x + BB_CELL - 2, y, 2, BB_CELL, shade(face, 5, 8));
}

/*
 * Soft wall, in a running bond: two courses of three bricks with the joints
 * staggered.  It is drawn rather than stencilled because the joint colour is a
 * setting, and a preset that makes the joints the floor colour gets loose
 * bricks on grass, which is a look worth being able to ask for.
 */
static void draw_brick(int x, int y, bool flash) {
    uint16_t face = flash ? pal.flame_hot : pal.brick;

    arc_fill(x, y, BB_CELL, BB_CELL, face);
    arc_fill(x, y, BB_CELL, 1, pal.brick_edge);
    arc_fill(x, y + 8, BB_CELL, 1, pal.brick_edge);
    arc_fill(x + 4, y + 1, 1, 7, pal.brick_edge);
    arc_fill(x + 12, y + 1, 1, 7, pal.brick_edge);
    arc_fill(x + 8, y + 9, 1, 7, pal.brick_edge);
    arc_fill(x, y + 9, 1, 7, pal.brick_edge);
}

/*
 * The three pickups, as a tile with the shape cut out of it, and the door as
 * an archway.  All of them are drawn inside twelve pixels rather than sixteen,
 * so that a cell holding one still reads as a cell somebody can walk into.
 */
static void draw_item(int x, int y, int kind, bool usable) {
    if (kind == BB_I_DOOR) {
        uint16_t face = usable ? pal.door : shade(pal.door, 1, 2);
        arc_fill(x + 2, y + 3, 12, 13, face);
        arc_fill(x + 4, y + 6, 8, 10, shade(face, 1, 3));
        arc_fill(x + 5, y + 5, 6, 2, shade(face, 1, 3));
        return;
    }

    arc_fill(x + 2, y + 2, 12, 12, pal.pickup);
    arc_fill(x + 2, y + 2, 12, 1, shade(pal.pickup, 5, 4));
    arc_fill(x + 2, y + 13, 12, 1, shade(pal.pickup, 1, 2));

    uint16_t ink = shade(pal.pickup, 1, 5);
    switch (kind) {
    case BB_I_BOMB: /* one more of them at a time: a bomb */
        arc_fill(x + 5, y + 7, 6, 5, ink);
        arc_fill(x + 6, y + 6, 4, 1, ink);
        arc_fill(x + 9, y + 4, 2, 2, ink);
        break;
    case BB_I_FLAME: /* one more cell of reach: a flame */
        arc_fill(x + 7, y + 4, 2, 8, ink);
        arc_fill(x + 5, y + 6, 6, 2, ink);
        arc_fill(x + 4, y + 10, 8, 2, ink);
        break;
    default: /* a pixel a frame quicker: a pair of chevrons */
        for (int i = 0; i < 4; i++) {
            arc_fill(x + 4 + i, y + 4 + i, 1, 2, ink);
            arc_fill(x + 4 + i, y + 10 - i, 1, 2, ink);
            arc_fill(x + 8 + i, y + 4 + i, 1, 2, ink);
            arc_fill(x + 8 + i, y + 10 - i, 1, 2, ink);
        }
        break;
    }
}

/*
 * A bomb, breathing.  The pulse is the only thing on the board that says how
 * long is left, and it doubles in rate under half a second - which is the
 * whole warning anybody watching gets, and the reason the fuse is drawn at all
 * rather than the bomb being a black circle.
 */
static void draw_bomb(int x, int y, int phase) {
    int r = phase > 1 ? 7 : 6;

    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            if (i * i + j * j <= r * r) {
                arc_put(x + 8 + i, y + 8 + j, pal.bomb);
            }
        }
    }
    /* the highlight that stops a dark disc reading as a hole in the floor */
    arc_fill(x + 5, y + 5, 2, 2, shade(pal.bomb, 9, 4));
    arc_fill(x + 9, y + 2, 2, 2, pal.flame_hot);
    arc_fill(x + 8, y + 3, 1, 2, shade(pal.flame_hot, 1, 2));
}

/*
 * The flame, as bars running out of the cell towards whichever neighbours are
 * alight.  A cell at the end of an arm stops the bar at its own middle, so an
 * arm has a squared-off end rather than spilling into the wall that stopped
 * it, and a cell with nothing beside it - a bomb that reached nothing - is a
 * blob.  It narrows as it burns out, which is what makes a blast read as one
 * event rather than as a rectangle that appears and disappears.
 */
static void draw_fire(int x, int y, int life, int mask) {
    int th = 2 + (life + 1) / 2;
    if (th > 6) {
        th = 6;
    }

    int x0 = (mask & (1 << BB_LEFT)) ? 0 : 8 - th;
    int x1 = (mask & (1 << BB_RIGHT)) ? BB_CELL : 8 + th;
    int y0 = (mask & (1 << BB_UP)) ? 0 : 8 - th;
    int y1 = (mask & (1 << BB_DOWN)) ? BB_CELL : 8 + th;

    arc_fill(x + x0, y + 8 - th, x1 - x0, th * 2, pal.flame);
    arc_fill(x + 8 - th, y + y0, th * 2, y1 - y0, pal.flame);

    int hot = th - 2;
    if (hot > 0) {
        arc_fill(x + x0, y + 8 - hot, x1 - x0, hot * 2, pal.flame_hot);
        arc_fill(x + 8 - hot, y + y0, hot * 2, y1 - y0, pal.flame_hot);
    }
}

/* ------------------------------------------------------------------ */
/* who is standing on it                                               */
/* ------------------------------------------------------------------ */

/* a filled disc, which is most of both the bomber and a drifter */
static void disc(int cx, int cy, int r, uint16_t colour) {
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            if (i * i + j * j <= r * r) {
                arc_put(cx + i, cy + j, colour);
            }
        }
    }
}

/*
 * The bomber is a helmet with a visor, and the visor is on whichever side it
 * is walking towards - which is the only thing that says where it is going, a
 * round sprite having no front.  The feet swap every half cell so that walking
 * and standing still can be told apart, which matters when the pilot is
 * waiting out a fuse.
 */
static void draw_bomber(int x, int y, int dir, int stride) {
    disc(x + 8, y + 7, 6, pal.bomber);
    arc_fill(x + 3, y + 12, 4, 3, stride ? pal.bomber : shade(pal.bomber, 1, 2));
    arc_fill(x + 9, y + 12, 4, 3, stride ? shade(pal.bomber, 1, 2) : pal.bomber);

    switch (dir) {
    case BB_UP:
        arc_fill(x + 4, y + 2, 8, 3, pal.bomber_trim);
        break;
    case BB_DOWN:
        arc_fill(x + 4, y + 8, 8, 3, pal.bomber_trim);
        arc_fill(x + 5, y + 5, 2, 2, pal.bomber_trim);
        arc_fill(x + 9, y + 5, 2, 2, pal.bomber_trim);
        break;
    case BB_LEFT:
        arc_fill(x + 2, y + 5, 5, 4, pal.bomber_trim);
        break;
    default:
        arc_fill(x + 9, y + 5, 5, 4, pal.bomber_trim);
        break;
    }
}

/*
 * Two enemies and two silhouettes, because two colours would not survive a
 * preset: a drifter is a round thing with big eyes and a hunter is a spiked
 * diamond, and which of them is coming down the corridor has to be readable
 * from the shape alone.
 */
static void draw_foe(int x, int y, int kind, int stride) {
    if (kind == BB_F_DRIFT) {
        disc(x + 8, y + 8, 6, pal.foe);
        arc_fill(x + 2 + stride, y + 13, 3, 2, shade(pal.foe, 1, 2));
        arc_fill(x + 11 - stride, y + 13, 3, 2, shade(pal.foe, 1, 2));
    } else {
        for (int j = 0; j < 14; j++) {
            int half = j < 7 ? j : 13 - j;
            arc_fill(x + 7 - half, y + 1 + j, half * 2 + 2, 1, pal.foe);
        }
        /* the spikes, which is what a diamond needs to stop reading as a kite */
        arc_fill(x + 1, y + 7 - stride, 2, 2, pal.foe);
        arc_fill(x + 13, y + 7 + stride, 2, 2, pal.foe);
    }
    arc_fill(x + 5, y + 6, 2, 3, pal.foe_eye);
    arc_fill(x + 9, y + 6, 2, 3, pal.foe_eye);
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

/*
 * Three counts along the right of the band, each behind a five pixel mark of
 * its own.  A row of bare digits would be unreadable - nothing says which of
 * them is lives and which is reach - and a word apiece would not fit beside a
 * six digit score.
 */
#define BB_TALLY_R 216 /* where the rightmost of the three sits */
#define BB_TALLY_W 26  /* and how far apart they are */

static void draw_mark(int x, int y, int which) {
    switch (which) {
    case 0: /* lives: the bomber's helmet */
        disc(x + 4, y + 4, 4, pal.bomber);
        arc_fill(x + 1, y + 3, 7, 2, pal.bomber_trim);
        break;
    case 1: /* bombs at once, rimmed: the bomb's own colour is nearly the
             * colour of the band it is drawn on, and a preset is free to make
             * that exact */
        disc(x + 4, y + 5, 5, pal.hud);
        disc(x + 4, y + 5, 4, pal.bomb);
        arc_fill(x + 5, y, 2, 2, pal.flame_hot);
        break;
    default: /* how far an arm reaches */
        arc_fill(x + 3, y, 2, 9, pal.flame);
        arc_fill(x + 1, y + 3, 6, 2, pal.flame);
        arc_fill(x + 3, y + 3, 2, 2, pal.flame_hot);
        break;
    }
}

/* how much of the board's clock is left, as a bar the width of the score */
static int clock_bar(const bb_game *g) { return (int)g->clock * 70 / BB_CLOCK; }

static void draw_hud(const bb_game *g) {
    char buf[8];

    arc_digits(g->score, 6, buf);
    arc_text(BB_HUD_X, BB_HUD_Y, 2, pal.hud, buf);

    /* clear of the digits' baseline, or the bar reads as an underline of the
     * score rather than as the thing running out */
    int bar = clock_bar(g), by = BB_HUD_Y + 2 * ARC_GLYPH_H + 4;
    arc_fill(BB_HUD_X, by, 70, 3, shade(pal.hud, 1, 4));
    if (bar > 0) {
        arc_fill(BB_HUD_X, by, bar, 3, pal.hud);
    }

    const uint8_t tally[3] = {g->lives, g->bombs_max, g->range};
    for (int i = 0; i < 3; i++) {
        int x = BB_TALLY_R - (2 - i) * BB_TALLY_W;
        draw_mark(x, BB_HUD_Y + 2, i);
        arc_digits(tally[i], 1, buf);
        arc_text(x + BB_ICON + 2, BB_HUD_Y + 2, 2, pal.hud, buf);
    }
}

static void draw_banner(const bb_game *g) {
    const char *word = bb_banner(g);

    if (word != NULL) {
        arc_text((ARC_PANEL - arc_text_w(word, BB_BANNER_SCALE)) / 2, BB_BANNER_Y,
                BB_BANNER_SCALE, pal.hud, word);
    }
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

/* which neighbours of a burning cell are burning too, so the arms join up */
static int fire_mask(const bb_game *g, int r, int c) {
    static const int8_t dr[BB_DIRS] = {-1, 0, 1, 0};
    static const int8_t dc[BB_DIRS] = {0, 1, 0, -1};
    int mask = 0;

    for (int d = 0; d < BB_DIRS; d++) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr >= 0 && nr < BB_ROWS && nc >= 0 && nc < BB_COLS && g->fire[nr][nc] > 0) {
            mask |= 1 << d;
        }
    }
    return mask;
}

static int bomb_phase(const bb_game *g, int r, int c) {
    for (int i = 0; i < BB_BOMBS; i++) {
        const bb_bomb *b = &g->bombs[i];
        if (!b->live || b->r != r || b->c != c) {
            continue;
        }
        /* twice as fast under half a second left, which is the whole warning */
        return 1 + (b->fuse < 8 ? (b->fuse & 1) : ((b->fuse >> 2) & 1));
    }
    return 0;
}

static int item_shown(const bb_game *g, int r, int c) {
    if (!bb_item_visible(g, r, c)) {
        return 0;
    }
    int kind = g->item[r][c];
    /* the door is drawn lit once there is nothing left to stop it being used */
    if (kind == BB_I_DOOR && g->foes_left == 0) {
        return BB_I_KINDS;
    }
    return kind;
}

/*
 * Everything about one cell that can change what it looks like, in one number.
 * The whole of the incremental redraw for the board is comparing this against
 * what it was last frame, so anything drawn from state that is not in here is
 * a cell that goes stale - a pickup appearing, a fuse pulsing, a flame
 * narrowing, the board flashing on the way out.
 */
static uint16_t cell_look(const bb_game *g, int r, int c) {
    uint16_t v = (uint16_t)(g->cell[r][c] & 3);

    v |= (uint16_t)(item_shown(g, r, c) << 2);
    if (g->fire[r][c] > 0) {
        v |= (uint16_t)((g->fire[r][c] & 7) << 5);
        v |= (uint16_t)(fire_mask(g, r, c) << 8);
    }
    v |= (uint16_t)(bomb_phase(g, r, c) << 12);
    if (g->flash) {
        v |= 1u << 14;
    }
    return v;
}

static void draw_cell(const bb_game *g, int r, int c) {
    int x = cell_x(c), y = cell_y(r);

    switch (g->cell[r][c]) {
    case BB_C_SOLID:
        draw_solid(x, y, g->flash);
        return;
    case BB_C_BRICK:
        draw_brick(x, y, g->flash);
        return;
    default:
        break;
    }

    draw_floor(x, y, r, c);

    int item = item_shown(g, r, c);
    if (item != 0) {
        draw_item(x, y, item == BB_I_KINDS ? BB_I_DOOR : item, item == BB_I_KINDS);
    }
    int phase = bomb_phase(g, r, c);
    if (phase != 0) {
        draw_bomb(x, y, phase);
    }
}

/*
 * Ground, then whoever is standing on it, then the fire over the lot.  Flames
 * go last on purpose: a bomber caught in one should be seen going up in it
 * rather than standing in front of it, and drawing them in the cell pass would
 * put the actors on top.
 */
static void paint_band(const bb_game *g, int x0, int y0, int w, int h) {
    arc_band_begin(x0, y0, w, h);

    /* the readout's ground; every board cell below paints over this */
    arc_fill(x0, y0, w, h, shade(pal.solid, 1, 3));

    int c0 = x0 / BB_CELL, c1 = (x0 + w - 1) / BB_CELL;
    int r0 = (y0 - BB_OY) / BB_CELL, r1 = (y0 + h - 1 - BB_OY) / BB_CELL;
    if (r0 < 0) {
        r0 = 0;
    }
    if (c1 > BB_COLS - 1) {
        c1 = BB_COLS - 1;
    }
    if (r1 > BB_ROWS - 1) {
        r1 = BB_ROWS - 1;
    }

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            draw_cell(g, r, c);
        }
    }

    for (int i = 0; i < BB_FOES; i++) {
        const bb_foe *f = &g->foes[i];
        if (f->alive) {
            draw_foe(f->a.x, BB_OY + f->a.y, f->kind, (f->a.step >> 3) & 1);
        }
    }
    if (bb_bomber_visible(g)) {
        draw_bomber(g->bomber.x, BB_OY + g->bomber.y, g->bomber.dir,
                    (g->bomber.step >> 3) & 1);
    }

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (g->fire[r][c] > 0) {
                draw_fire(cell_x(c), cell_y(r), g->fire[r][c], fire_mask(g, r, c));
            }
        }
    }

    if (y0 < BB_OY) {
        draw_hud(g);
    }
    draw_banner(g);

    arc_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, arc_band);
}

/* a rectangle wider than the band is split into as many rows as fit */
static void paint(const bb_game *g, int x0, int y0, int w, int h) {
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

typedef struct {
    int16_t x, y;
    uint8_t look;
    bool on;
} bb_box;

enum { BB_B_FOE = 0, BB_B_BOMBER = BB_FOES, BB_BOXES };

static uint16_t prev_cell[BB_ROWS][BB_COLS];
static bb_box prev[BB_BOXES];
static bool prev_valid;
static char prev_hud[16];
static char prev_banner[12];

/*
 * An actor's box is always one cell, wherever between two cells it happens to
 * be, so there is nothing to remember but where it was and how it looked.  The
 * look is in here rather than being taken for granted because both sprites
 * change without moving - the bomber turns on the spot at a junction, and both
 * of them swap feet halfway across a cell.
 */
static bb_box box_of(const bb_game *g, int idx) {
    bb_box b = {0, 0, 0, false};
    const bb_actor *a;
    int look;

    if (idx < BB_B_BOMBER) {
        const bb_foe *f = &g->foes[idx];
        if (!f->alive) {
            return b;
        }
        a = &f->a;
        look = (int)(f->kind << 1) | ((a->step >> 3) & 1);
    } else {
        if (!bb_bomber_visible(g)) {
            return b;
        }
        a = &g->bomber;
        look = (int)((a->dir & 3) << 1) | ((a->step >> 3) & 1);
    }

    b.x = a->x;
    b.y = (int16_t)(BB_OY + a->y);
    b.look = (uint8_t)look;
    b.on = true;
    return b;
}

static bool overlap(const bb_box *a, const bb_box *b) {
    return !(a->x + BB_CELL <= b->x || b->x + BB_CELL <= a->x || a->y + BB_CELL <= b->y ||
             b->y + BB_CELL <= a->y);
}

static void paint_box(const bb_game *g, const bb_box *b) {
    paint(g, b->x, b->y, BB_CELL, BB_CELL);
}

/* the union where the two touch, which they almost always do: an actor moves a
 * few pixels a frame, so painting them apart would be two blits for one sprite */
static void paint_move(const bb_game *g, const bb_box *a, const bb_box *b) {
    if (!overlap(a, b)) {
        paint_box(g, a);
        paint_box(g, b);
        return;
    }
    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = (a->x > b->x ? a->x : b->x) + BB_CELL;
    int y1 = (a->y > b->y ? a->y : b->y) + BB_CELL;
    paint(g, x0, y0, x1 - x0, y1 - y0);
}

/*
 * What the readout says, as a string to compare against what it said last
 * frame.  The clock is in it as the width of its own bar rather than as a
 * count, so the band is repainted the seventy times the bar actually moves
 * instead of on every one of the three thousand frames it counts down.
 */
static void hud_key(const bb_game *g, char *buf) {
    arc_digits(g->score, 6, buf);
    buf[6] = (char)('0' + (g->lives > 9 ? 9 : g->lives));
    buf[7] = (char)('0' + g->bombs_max);
    buf[8] = (char)('0' + g->range);
    buf[9] = (char)('0' + clock_bar(g) / 10);
    buf[10] = (char)('0' + clock_bar(g) % 10);
    buf[11] = '\0';
}

static void repaint_text(const bb_game *g) {
    char buf[16];

    hud_key(g, buf);
    if (strcmp(buf, prev_hud) != 0) {
        memcpy(prev_hud, buf, strlen(buf) + 1);
        paint(g, 0, 0, ARC_PANEL, BB_OY);
    }

    const char *word = bb_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so whichever is going away is wiped as well */
        int was = arc_text_w(prev_banner, BB_BANNER_SCALE);
        int is = arc_text_w(now, BB_BANNER_SCALE);
        int w = (was > is ? was : is) + 2;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (ARC_PANEL - w) / 2, BB_BANNER_Y - 1, w, ARC_GLYPH_H * BB_BANNER_SCALE + 2);
    }
}

static void snapshot(const bb_game *g) {
    const char *word = bb_banner(g);

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            prev_cell[r][c] = cell_look(g, r, c);
        }
    }
    for (int i = 0; i < BB_BOXES; i++) {
        prev[i] = box_of(g, i);
    }
    hud_key(g, prev_hud);
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
}

void bb_render_frame(bb_game *g) {
    if (!pal_ready) {
        bb_palette def;
        bb_render_default_palette(&def);
        bb_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, ARC_PANEL, ARC_PANEL);
        snapshot(g);
        return;
    }

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < BB_COLS; c++) {
            uint16_t look = cell_look(g, r, c);
            if (look != prev_cell[r][c]) {
                prev_cell[r][c] = look;
                paint(g, cell_x(c), cell_y(r), BB_CELL, BB_CELL);
            }
        }
    }

    for (int i = 0; i < BB_BOXES; i++) {
        bb_box now = box_of(g, i);
        if (prev[i].on && now.on) {
            if (prev[i].x != now.x || prev[i].y != now.y || prev[i].look != now.look) {
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
