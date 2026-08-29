/*
 * Pac-Man dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include "pacman_render.h"

#define BAND_BYTES (PM_WIDTH * PM_TILE * 2)

/* tile-relative sizes, so the grid can be rescaled without retuning them */
/*
 * The border line: it lives in the margin when the grid leaves one, and is
 * drawn over the outermost pixels of the maze when it does not.
 */
#define PM_BORDER    (PM_MARGIN > PM_WALL_LINE ? PM_MARGIN : PM_WALL_LINE)

#define PM_PELLET    (PM_TILE / 4)                      /* pellet side, 2px at 10px tiles */
#define PM_PELLET_AT ((PM_TILE - PM_PELLET) / 2)
#define PM_DOOR_AT   ((PM_TILE - PM_WALL_LINE) / 2)
#define PM_POWER_R2  ((PM_TILE * PM_TILE * 40) / 100)   /* power pellet radius^2 */

static uint8_t scratch[BAND_BYTES];
static pm_palette pal;
static bool pal_ready;

static int16_t prev_x[PM_ACTORS];
static int16_t prev_y[PM_ACTORS];
static bool prev_vis[PM_ACTORS];
static bool prev_valid;
static bool last_blink;

uint16_t pm_rgb565(uint32_t rgb888) {
    uint16_t r = (uint16_t)(((rgb888 >> 16) & 0xFF) * 31 / 255);
    uint16_t g = (uint16_t)(((rgb888 >> 8) & 0xFF) * 63 / 255);
    uint16_t b = (uint16_t)((rgb888 & 0xFF) * 31 / 255);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void pm_render_default_palette(pm_palette *p) {
    p->bg = pm_rgb565(0x000000);
    p->wall_fill = pm_rgb565(0x000000);
    p->wall_edge = pm_rgb565(0x2121de);
    p->wall_flash = pm_rgb565(0xf8f8f8);
    p->house_fill = pm_rgb565(0x000000);
    p->house_edge = pm_rgb565(0x6d6dff);
    p->door = pm_rgb565(0xffb8ff);
    p->pellet = pm_rgb565(0xffb897);
    p->pac = pm_rgb565(0xffee00);
    p->ghost[0] = pm_rgb565(0xff0000);
    p->ghost[1] = pm_rgb565(0xffb8ff);
    p->ghost[2] = pm_rgb565(0x00ffff);
    p->ghost[3] = pm_rgb565(0xffb852);
    p->fright_body = pm_rgb565(0x2121de);
    p->fright_face = pm_rgb565(0xffffff);
    p->flash_body = pm_rgb565(0xf8f8f8);
    p->flash_face = pm_rgb565(0xff0000);
    p->eye = pm_rgb565(0xffffff);
    p->pupil = pm_rgb565(0x2121de);
}

void pm_render_set_palette(const pm_palette *p) {
    pal = *p;
    pal_ready = true;
}

/* ------------------------------------------------------------------ */
/* maze pixels                                                         */
/* ------------------------------------------------------------------ */

static bool solid_tile(const pm_game *g, int tx, int ty) {
    if (tx < 0 || tx >= PM_COLS || ty < 0 || ty >= PM_ROWS) {
        return true; /* off screen counts as wall so the border has no outline */
    }
    uint8_t t = g->tiles[ty][tx];
    return t == PM_T_WALL || t == PM_T_HWALL;
}

/*
 * A wall tile is drawn as a line along each side that faces open space, so a
 * block of wall comes out as a hollow tube instead of a solid slab.
 *
 * Where two open sides meet, the outline turns through a quarter circle of
 * radius PM_WALL_R instead of a right angle, and the tile's own corner outside
 * that arc drops back to the background - the arcade's rounded wall ends.  The
 * pixel is therefore one of three things, not two: outside the wall, on its
 * outline, or filling it.  Outside has to be the background rather than the
 * fill colour, or a wall drawn with PACMAN_WALL_FILL_COLOR set would keep its
 * square corners.
 *
 * A corner where only the diagonal is open is the other kind, the inside of an
 * elbow.  Rounding that one means bulging into the corridor it wraps, so it
 * keeps the single-pixel nub that closes the two lines up instead.
 */
enum { PM_WPX_OUT = 0, PM_WPX_LINE, PM_WPX_FILL };

/*
 * The widest a corner box gets, when neither side faces a door.  The four
 * boxes of a tile have to stay apart, or a pixel would fall in two of them and
 * only the first would be drawn.
 */
#define PM_CORNER (PM_WALL_INSET + PM_WALL_R)
_Static_assert(2 * PM_CORNER <= PM_TILE, "PM_WALL_INSET + PM_WALL_R too big for PM_TILE");

/*
 * An outside corner, where two open sides meet: a quarter circle tangent to
 * both drawn faces.  cx, cy are where those faces put the centre - one radius
 * in from each - which lets the two sides carry different insets.
 */
static int corner_px(int dx, int dy, int cx, int cy) {
    int a = cx - dx;
    int b = cy - dy;
    int d2 = a * a + b * b;
    int inner = PM_WALL_R - PM_WALL_LINE;

    if (d2 > PM_WALL_R * PM_WALL_R) {
        return PM_WPX_OUT;
    }
    return d2 > inner * inner ? PM_WPX_LINE : PM_WPX_FILL;
}

/*
 * An inside corner, where both neighbours are wall but the diagonal is open.
 * Nothing has to be drawn from the open tile's side to round it: the inset
 * already stands the wall off the corridor, so the whole turn happens inside
 * the wall tiles and the corridor keeps its full width.  What matters is the
 * distance to that open tile rather than to any one side, which rounds the
 * corner off on its own as the inset grows.  Distances are squared to keep it
 * integer: the nearest pixel of the diagonal tile is one further out than dx,
 * dy count.
 */
static int diagonal_px(int dx, int dy) {
    int d2 = (dx + 1) * (dx + 1) + (dy + 1) * (dy + 1);
    int out = PM_WALL_INSET + 1;
    int line = PM_WALL_INSET + PM_WALL_LINE + 1;

    if (d2 < out * out) {
        return PM_WPX_OUT;
    }
    return d2 < line * line ? PM_WPX_LINE : PM_WPX_FILL;
}

/*
 * The ghost house door is not solid - ghosts pass through it - but it is part
 * of the wall that closes the house, and it is drawn as a line spanning its
 * own tile.  A wall standing back from it would leave the two ends of that
 * line hanging in the gap, so a face looking at a door keeps its outline but
 * takes no inset.
 */
static bool door_face(const pm_game *g, int tx, int ty) {
    if (tx < 0 || tx >= PM_COLS || ty < 0 || ty >= PM_ROWS) {
        return false;
    }
    return g->tiles[ty][tx] == PM_T_DOOR;
}

static int wall_px(const pm_game *g, int tx, int ty, int ix, int iy) {
    bool open_l = !solid_tile(g, tx - 1, ty);
    bool open_r = !solid_tile(g, tx + 1, ty);
    bool open_u = !solid_tile(g, tx, ty - 1);
    bool open_d = !solid_tile(g, tx, ty + 1);

    /* how far the wall stands back from each side it faces */
    int il = door_face(g, tx - 1, ty) ? 0 : PM_WALL_INSET;
    int ir = door_face(g, tx + 1, ty) ? 0 : PM_WALL_INSET;
    int iu = door_face(g, tx, ty - 1) ? 0 : PM_WALL_INSET;
    int id = door_face(g, tx, ty + 1) ? 0 : PM_WALL_INSET;

    /* distances in from each of the four sides */
    int dl = ix, dr = PM_TILE - 1 - ix;
    int du = iy, dd = PM_TILE - 1 - iy;

    if ((open_l && dl < il) || (open_r && dr < ir) ||
        (open_u && du < iu) || (open_d && dd < id)) {
        return PM_WPX_OUT;
    }

    /* corner boxes are at most PM_CORNER, so only one can hold the pixel */
    if (open_l && open_u && dl < il + PM_WALL_R && du < iu + PM_WALL_R) {
        return corner_px(dl, du, il + PM_WALL_R, iu + PM_WALL_R);
    }
    if (open_r && open_u && dr < ir + PM_WALL_R && du < iu + PM_WALL_R) {
        return corner_px(dr, du, ir + PM_WALL_R, iu + PM_WALL_R);
    }
    if (open_l && open_d && dl < il + PM_WALL_R && dd < id + PM_WALL_R) {
        return corner_px(dl, dd, il + PM_WALL_R, id + PM_WALL_R);
    }
    if (open_r && open_d && dr < ir + PM_WALL_R && dd < id + PM_WALL_R) {
        return corner_px(dr, dd, ir + PM_WALL_R, id + PM_WALL_R);
    }

    if ((open_l && dl < il + PM_WALL_LINE) || (open_r && dr < ir + PM_WALL_LINE) ||
        (open_u && du < iu + PM_WALL_LINE) || (open_d && dd < id + PM_WALL_LINE)) {
        return PM_WPX_LINE;
    }

    int px = PM_WPX_FILL;
    if (!open_l && !open_u && !solid_tile(g, tx - 1, ty - 1)) {
        int d = diagonal_px(dl, du);
        px = d < px ? d : px;
    }
    if (!open_r && !open_u && !solid_tile(g, tx + 1, ty - 1)) {
        int d = diagonal_px(dr, du);
        px = d < px ? d : px;
    }
    if (!open_l && !open_d && !solid_tile(g, tx - 1, ty + 1)) {
        int d = diagonal_px(dl, dd);
        px = d < px ? d : px;
    }
    if (!open_r && !open_d && !solid_tile(g, tx + 1, ty + 1)) {
        int d = diagonal_px(dr, dd);
        px = d < px ? d : px;
    }
    return px;
}

static uint16_t wall_colour(const pm_game *g, bool house, bool line) {
    if (g->flash) {
        return line ? pal.wall_flash : pal.wall_fill;
    }
    if (house) {
        return line ? pal.house_edge : pal.house_fill;
    }
    return line ? pal.wall_edge : pal.wall_fill;
}

static uint16_t bg_pixel(const pm_game *g, int px, int py) {
    int tx = px / PM_TILE;
    int ty = py / PM_TILE;
    uint8_t t = g->tiles[ty][tx];
    int ix = px - tx * PM_TILE;
    int iy = py - ty * PM_TILE;

    switch (t) {
    case PM_T_WALL:
    case PM_T_HWALL: {
        int w = wall_px(g, tx, ty, ix, iy);
        if (w == PM_WPX_OUT) {
            return pal.bg;
        }
        return wall_colour(g, t == PM_T_HWALL, w == PM_WPX_LINE);
    }
    case PM_T_DOOR:
        return (iy >= PM_DOOR_AT && iy < PM_DOOR_AT + PM_WALL_LINE) ? pal.door : pal.bg;
    case PM_T_PELLET:
        return (ix >= PM_PELLET_AT && ix < PM_PELLET_AT + PM_PELLET &&
                iy >= PM_PELLET_AT && iy < PM_PELLET_AT + PM_PELLET) ? pal.pellet : pal.bg;
    case PM_T_POWER: {
        if (!pm_power_visible(g)) {
            return pal.bg;
        }
        int dx = 2 * ix - (PM_TILE - 1);
        int dy = 2 * iy - (PM_TILE - 1);
        return (dx * dx + dy * dy <= PM_POWER_R2) ? pal.pellet : pal.bg;
    }
    default:
        return pal.bg;
    }
}

/* ------------------------------------------------------------------ */
/* sprites, all drawn inside a PM_SPRITE box                           */
/* ------------------------------------------------------------------ */

/* how wide the mouth opens: perp * 5 <= along * num */
static const uint8_t MOUTH_NUM[4] = {0, 2, 5, 2};
static const uint8_t DEATH_NUM[5] = {5, 9, 16, 30, 80};

/*
 * Distances are kept in half-pixels so the centre of an even-sized box lands
 * on a whole number: dx spans -(S-1)..(S-1).  The radius keeps the same
 * fraction of the box the 10px sprite had (84/100).
 */
#define PM_PAC_R2 ((PM_SPRITE * PM_SPRITE * 84) / 100)

static bool pac_pixel(const pm_game *g, int i, int j, uint16_t *col) {
    int dx = 2 * i - (PM_SPRITE - 1);
    int dy = 2 * j - (PM_SPRITE - 1);
    if (dx * dx + dy * dy > PM_PAC_R2) {
        return false;
    }

    int num;
    if (g->phase == PM_DYING) {
        if (g->death >= 5) {
            return false;
        }
        num = DEATH_NUM[g->death];
    } else if (g->phase == PM_READY) {
        num = MOUTH_NUM[2];
    } else {
        num = MOUTH_NUM[g->mouth & 3];
    }

    if (num > 0) {
        int along, perp;
        switch (g->pac.dir) {
        case PM_RIGHT: along = dx; perp = dy; break;
        case PM_LEFT: along = -dx; perp = dy; break;
        case PM_DOWN: along = dy; perp = dx; break;
        default: along = -dy; perp = dx; break;
        }
        if (perp < 0) {
            perp = -perp;
        }
        if (along > 0 && perp * 5 <= along * num) {
            return false;
        }
    }

    *col = pal.pac;
    return true;
}

/*
 * Ghost outline, built from the same circle Pac-Man uses so the two stay in
 * proportion at any sprite size: a dome over straight sides, with the bottom
 * rows cut into feet that swap over every few frames to give the walk its
 * wobble.
 */
static bool ghost_body_px(int i, int j, int phase) {
    int dx = 2 * i - (PM_SPRITE - 1);
    int mid = PM_SPRITE / 2;

    if (j < mid) {
        int dy = 2 * (j - mid);
        if (dx * dx + dy * dy > PM_PAC_R2) {
            return false;
        }
    } else if (dx * dx > PM_PAC_R2) {
        return false;
    }

    int skirt = PM_SPRITE / 6;
    if (skirt > 0 && j >= PM_SPRITE - skirt) {
        int seg = (i * 5) / PM_SPRITE; /* foot, notch, foot, notch, foot */
        if (((seg & 1) != 0) == (phase == 0)) {
            return false;
        }
    }
    return true;
}

/* eye geometry, derived from the sprite box so the shapes scale together */
#define PM_EYE_W  (PM_SPRITE / 4)                   /* 3 */
#define PM_EYE_LX (PM_SPRITE / 5)                   /* 2 */
#define PM_EYE_RX (PM_SPRITE - PM_EYE_LX - PM_EYE_W)/* 9 */
#define PM_EYE_Y  (PM_SPRITE / 3)                   /* 4 */

static bool eye_pixel(int i, int j, pm_dir dir, uint16_t *col) {
    bool left_eye = (i >= PM_EYE_LX && i < PM_EYE_LX + PM_EYE_W);
    bool right_eye = (i >= PM_EYE_RX && i < PM_EYE_RX + PM_EYE_W);
    if (!(left_eye || right_eye) || j < PM_EYE_Y || j >= PM_EYE_Y + PM_EYE_W) {
        return false;
    }

    int base = left_eye ? PM_EYE_LX : PM_EYE_RX;
    int pw = PM_EYE_W / 2 > 0 ? PM_EYE_W / 2 : 1;
    int px = base + (dir == PM_RIGHT   ? PM_EYE_W - pw
                     : dir == PM_LEFT  ? 0
                                       : (PM_EYE_W - pw) / 2);
    int py = PM_EYE_Y + (dir == PM_DOWN ? PM_EYE_W - pw
                         : dir == PM_UP ? 0
                                        : (PM_EYE_W - pw) / 2);
    bool pupil = (i >= px && i < px + pw && j >= py && j < py + pw);
    *col = pupil ? pal.pupil : pal.eye;
    return true;
}

static bool ghost_pixel(const pm_game *g, int idx, int i, int j, uint16_t *col) {
    const pm_ghost *gh = &g->ghosts[idx];
    bool body = ghost_body_px(i, j, (g->frame >> 3) & 1);

    if (gh->state == PM_G_EYES || gh->state == PM_G_ENTER) {
        return eye_pixel(i, j, gh->actor.dir, col);
    }
    if (!body) {
        return false;
    }

    if (g->fright > 0 && gh->state == PM_G_OUT) {
        bool flash = pm_fright_flashing(g);
        uint16_t face = flash ? pal.flash_face : pal.fright_face;
        *col = flash ? pal.flash_body : pal.fright_body;

        bool eye_box = (i >= PM_EYE_LX && i < PM_EYE_LX + PM_EYE_W) ||
                       (i >= PM_EYE_RX && i < PM_EYE_RX + PM_EYE_W);
        int mouth_y = (PM_SPRITE * 2) / 3;
        bool tooth = ((i >> 1) & 1) != 0;

        if (eye_box && j >= PM_EYE_Y && j < PM_EYE_Y + PM_EYE_W) {
            *col = face; /* eyes */
        } else if (i >= 1 && i < PM_SPRITE - 1 &&
                   ((j == mouth_y && !tooth) || (j == mouth_y + 1 && tooth))) {
            *col = face; /* zig-zag mouth */
        }
        return true;
    }

    if (eye_pixel(i, j, gh->actor.dir, col)) {
        return true;
    }
    *col = pal.ghost[idx];
    return true;
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

static bool actor_box(const pm_game *g, int idx, int16_t *ax, int16_t *ay) {
    if (idx == PM_GHOSTS) {
        *ax = g->pac.x;
        *ay = g->pac.y;
        return !(g->phase == PM_DYING && g->death >= 5);
    }
    if (g->hide_ghosts || !pm_ghost_visible(g, idx)) {
        return false;
    }
    *ax = g->ghosts[idx].actor.x;
    *ay = g->ghosts[idx].actor.y;
    return true;
}

static void paint(pm_game *g, int x0, int y0, int w, int h) {
    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (y0 < 0) {
        h += y0;
        y0 = 0;
    }
    if (x0 + w > PM_WIDTH) {
        w = PM_WIDTH - x0;
    }
    if (y0 + h > PM_HEIGHT) {
        h = PM_HEIGHT - y0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    uint8_t *out = scratch;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            uint16_t c = bg_pixel(g, x, y);

            for (int idx = PM_ACTORS - 1; idx >= 0; idx--) {
                int16_t ax, ay;
                if (!actor_box(g, idx, &ax, &ay)) {
                    continue;
                }
                int i = x - (ax + PM_SPRITE_OFF);
                int j = y - (ay + PM_SPRITE_OFF);
                if (i < 0 || i >= PM_SPRITE || j < 0 || j >= PM_SPRITE) {
                    continue;
                }
                uint16_t sc;
                bool hit = (idx == PM_GHOSTS) ? pac_pixel(g, i, j, &sc)
                                              : ghost_pixel(g, idx, i, j, &sc);
                if (hit) {
                    c = sc;
                    break;
                }
            }

            int px = x + PM_MARGIN, py = y + PM_MARGIN;
            bool on_side = (px < PM_BORDER || px >= PM_PANEL - PM_BORDER);
            bool on_cap = (py < PM_BORDER || py >= PM_PANEL - PM_BORDER);
            if (on_side || on_cap) {
                bool tunnel = (g->tunnel_rows >> (y / PM_TILE)) & 1u;
                if (!(on_side && !on_cap && tunnel)) {
                    c = g->flash ? pal.wall_flash : pal.wall_edge;
                }
            }

            *out++ = (uint8_t)(c >> 8);
            *out++ = (uint8_t)(c & 0xFF);
        }
    }
    pm_blit((uint16_t)(x0 + PM_MARGIN), (uint16_t)(y0 + PM_MARGIN), (uint16_t)w,
            (uint16_t)h, scratch);
}

/*
 * The maze has no border tiles: what walls it in is a frame drawn in the
 * margin left over from PM_COLS * PM_TILE.  The frame hugs the playfield and
 * is PM_BORDER_LINE thick (or the whole margin, when that is thinner); any
 * margin outside it is background, which is what keeps the outer wall as light
 * as the walls inside when the tile size leaves a wide margin.
 *
 * The far margin is a pixel wider than the near one when the leftover is odd,
 * so the two are painted from PM_MARGIN and PM_MARGIN_END separately; using
 * PM_MARGIN for both would leave the last row and column of the panel with
 * whatever happened to be on it.
 */
#define PM_FRAME_NEAR (PM_BORDER_LINE < PM_MARGIN ? PM_BORDER_LINE : PM_MARGIN)
#define PM_FRAME_FAR  (PM_BORDER_LINE < PM_MARGIN_END ? PM_BORDER_LINE : PM_MARGIN_END)
#define PM_FRAME_LO   (PM_MARGIN - PM_FRAME_NEAR)
#define PM_FRAME_HI   (PM_MARGIN + PM_WIDTH - 1 + PM_FRAME_FAR)

/* colour of one pixel of the margin, in panel coordinates */
static uint16_t border_pixel(const pm_game *g, int px, int py, uint16_t line) {
    if (px < PM_FRAME_LO || px > PM_FRAME_HI || py < PM_FRAME_LO || py > PM_FRAME_HI) {
        return pal.bg;
    }
    /* the sides open up wherever a row runs off into the tunnel */
    bool side = (px < PM_MARGIN || px >= PM_MARGIN + PM_WIDTH);
    bool cap = (py < PM_MARGIN || py >= PM_MARGIN + PM_HEIGHT);
    if (side && !cap && ((g->tunnel_rows >> ((py - PM_MARGIN) / PM_TILE)) & 1u)) {
        return pal.bg;
    }
    return line;
}

/* fills one band of the margin; the four of them cover it exactly once */
static void paint_band(const pm_game *g, int x0, int y0, int w, int h, uint16_t line) {
    if (w <= 0 || h <= 0) {
        return;
    }
    uint8_t *out = scratch;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            uint16_t c = border_pixel(g, x, y, line);
            *out++ = (uint8_t)(c >> 8);
            *out++ = (uint8_t)(c & 0xFF);
        }
    }
    pm_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, scratch);
}

static void paint_border(const pm_game *g) {
    if (PM_MARGIN == 0 && PM_MARGIN_END == 0) {
        return; /* paint() covers the whole panel and draws the line itself */
    }

    uint16_t line = g->flash ? pal.wall_flash : pal.wall_edge;
    int inner = PM_MARGIN + PM_HEIGHT;

    paint_band(g, 0, 0, PM_PANEL, PM_MARGIN, line);
    paint_band(g, 0, inner, PM_PANEL, PM_MARGIN_END, line);
    paint_band(g, 0, PM_MARGIN, PM_MARGIN, PM_HEIGHT, line);
    paint_band(g, PM_MARGIN + PM_WIDTH, PM_MARGIN, PM_MARGIN_END, PM_HEIGHT, line);
}

static void paint_all(pm_game *g) {
    for (int band = 0; band < PM_ROWS; band++) {
        paint(g, 0, band * PM_TILE, PM_WIDTH, PM_TILE);
    }
    paint_border(g);
}

static void sync_prev(const pm_game *g) {
    for (int i = 0; i < PM_GHOSTS; i++) {
        prev_x[i] = g->ghosts[i].actor.x;
        prev_y[i] = g->ghosts[i].actor.y;
    }
    prev_x[PM_GHOSTS] = g->pac.x;
    prev_y[PM_GHOSTS] = g->pac.y;
    for (int i = 0; i < PM_ACTORS; i++) {
        int16_t ax, ay;
        prev_vis[i] = actor_box(g, i, &ax, &ay);
    }
    prev_valid = true;
}

static void paint_power_pellets(pm_game *g) {
    for (int y = 0; y < PM_ROWS; y++) {
        for (int x = 0; x < PM_COLS; x++) {
            if (g->tiles[y][x] == PM_T_POWER) {
                paint(g, x * PM_TILE, y * PM_TILE, PM_TILE, PM_TILE);
            }
        }
    }
}

void pm_render_frame(pm_game *g) {
    if (!pal_ready) {
        pm_palette def;
        pm_render_default_palette(&def);
        pm_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        paint_all(g);
        sync_prev(g);
        last_blink = pm_power_visible(g);
        return;
    }

    for (int idx = 0; idx < PM_ACTORS; idx++) {
        int16_t ax, ay;
        bool visible = actor_box(g, idx, &ax, &ay);
        int16_t px = prev_x[idx];
        int16_t py = prev_y[idx];

        if (!visible) {
            if (prev_vis[idx]) {
                /* just vanished (eaten, or the death animation finished) */
                paint(g, px + PM_SPRITE_OFF, py + PM_SPRITE_OFF, PM_SPRITE, PM_SPRITE);
                prev_vis[idx] = false;
            }
            continue;
        }
        prev_vis[idx] = true;

        int dx = ax - px;
        int dy = ay - py;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }

        if (dx >= PM_SPRITE || dy >= PM_SPRITE) {
            /* teleported through the tunnel (or respawned): two small boxes */
            paint(g, px + PM_SPRITE_OFF, py + PM_SPRITE_OFF, PM_SPRITE, PM_SPRITE);
            paint(g, ax + PM_SPRITE_OFF, ay + PM_SPRITE_OFF, PM_SPRITE, PM_SPRITE);
        } else {
            int x0 = (ax < px ? ax : px) + PM_SPRITE_OFF;
            int y0 = (ay < py ? ay : py) + PM_SPRITE_OFF;
            paint(g, x0, y0, PM_SPRITE + dx, PM_SPRITE + dy);
        }

        prev_x[idx] = ax;
        prev_y[idx] = ay;
    }

    bool blink = pm_power_visible(g);
    if (blink != last_blink) {
        last_blink = blink;
        paint_power_pellets(g);
    }
}
