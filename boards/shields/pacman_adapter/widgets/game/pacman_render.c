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

/* dx, dy are the distances in from the two open sides, so the centre of the
 * arc sits at (PM_WALL_R, PM_WALL_R) and the tile corner is at (0, 0) */
static int corner_px(int dx, int dy) {
    int a = PM_WALL_R - dx;
    int b = PM_WALL_R - dy;
    int d2 = a * a + b * b;
    int inner = PM_WALL_R - PM_WALL_LINE;

    if (d2 > PM_WALL_R * PM_WALL_R) {
        return PM_WPX_OUT;
    }
    return d2 > inner * inner ? PM_WPX_LINE : PM_WPX_FILL;
}

static int wall_px(const pm_game *g, int tx, int ty, int ix, int iy) {
    bool open_l = !solid_tile(g, tx - 1, ty);
    bool open_r = !solid_tile(g, tx + 1, ty);
    bool open_u = !solid_tile(g, tx, ty - 1);
    bool open_d = !solid_tile(g, tx, ty + 1);

    /* distances in from each of the four sides */
    int dl = ix, dr = PM_TILE - 1 - ix;
    int du = iy, dd = PM_TILE - 1 - iy;

    /* 2 * PM_WALL_R <= PM_TILE, so at most one corner box holds the pixel */
    if (dl < PM_WALL_R && du < PM_WALL_R && open_l && open_u) {
        return corner_px(dl, du);
    }
    if (dr < PM_WALL_R && du < PM_WALL_R && open_r && open_u) {
        return corner_px(dr, du);
    }
    if (dl < PM_WALL_R && dd < PM_WALL_R && open_l && open_d) {
        return corner_px(dl, dd);
    }
    if (dr < PM_WALL_R && dd < PM_WALL_R && open_r && open_d) {
        return corner_px(dr, dd);
    }

    bool near_l = dl < PM_WALL_LINE;
    bool near_r = dr < PM_WALL_LINE;
    bool near_u = du < PM_WALL_LINE;
    bool near_d = dd < PM_WALL_LINE;

    if ((open_l && near_l) || (open_r && near_r) ||
        (open_u && near_u) || (open_d && near_d)) {
        return PM_WPX_LINE;
    }

    /* corner nub: both neighbours solid, but the diagonal between them is not */
    if (near_l && near_u && !open_l && !open_u && !solid_tile(g, tx - 1, ty - 1)) {
        return PM_WPX_LINE;
    }
    if (near_r && near_u && !open_r && !open_u && !solid_tile(g, tx + 1, ty - 1)) {
        return PM_WPX_LINE;
    }
    if (near_l && near_d && !open_l && !open_d && !solid_tile(g, tx - 1, ty + 1)) {
        return PM_WPX_LINE;
    }
    if (near_r && near_d && !open_r && !open_d && !solid_tile(g, tx + 1, ty + 1)) {
        return PM_WPX_LINE;
    }
    return PM_WPX_FILL;
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

/*
 * The inside of an elbow: a corridor tile with wall along two perpendicular
 * sides.  The wall wraps that corner, and rounding it means the fillet bulges
 * into the corridor - so unlike the convex corners it has to be drawn from the
 * open tile, not the wall.  The arc is tangent to both wall faces, which puts
 * its centre PM_WALL_R - PM_WALL_LINE in from each of them, and everything
 * beyond it belongs to the wall.
 *
 * Corridors are a whole tile wide against sprites that are nearly as wide, so
 * this only ever bites into a corner a sprite's circle does not reach.
 */
static bool elbow_px(const pm_game *g, int tx, int ty, int ix, int iy, uint16_t *col) {
    int dl = ix, dr = PM_TILE - 1 - ix;
    int du = iy, dd = PM_TILE - 1 - iy;

    /* nothing to do down the middle of the tile, which is most of it */
    if ((dl >= PM_WALL_R && dr >= PM_WALL_R) || (du >= PM_WALL_R && dd >= PM_WALL_R)) {
        return false;
    }

    bool west = dl < PM_WALL_R;
    bool north = du < PM_WALL_R;
    int wx = west ? tx - 1 : tx + 1;
    int wy = north ? ty - 1 : ty + 1;
    /*
     * Real wall tiles only.  solid_tile() calls everything off the grid solid
     * so the outer wall gets no outline, but that wall is the border line
     * painted round the panel - it is PM_MARGIN thick, not a tile, so an arc
     * tangent to a tile face would not meet it.  The maze corners stay square,
     * which is what the square border wants anyway.
     */
    if (wx < 0 || wx >= PM_COLS || wy < 0 || wy >= PM_ROWS) {
        return false;
    }
    if (!solid_tile(g, wx, ty) || !solid_tile(g, tx, wy)) {
        return false;
    }

    int c = PM_WALL_R - PM_WALL_LINE;
    int a = c - (west ? dl : dr);
    int b = c - (north ? du : dd);
    int d2 = a * a + b * b;
    if (d2 <= PM_WALL_R * PM_WALL_R) {
        return false; /* still corridor */
    }

    int outer = PM_WALL_R + PM_WALL_LINE;
    bool house = (g->tiles[ty][wx] == PM_T_HWALL);
    *col = wall_colour(g, house, d2 <= outer * outer);
    return true;
}

static uint16_t bg_pixel(const pm_game *g, int px, int py) {
    int tx = px / PM_TILE;
    int ty = py / PM_TILE;
    uint8_t t = g->tiles[ty][tx];
    int ix = px - tx * PM_TILE;
    int iy = py - ty * PM_TILE;

    if (t != PM_T_WALL && t != PM_T_HWALL) {
        uint16_t c;
        if (elbow_px(g, tx, ty, ix, iy, &c)) {
            return c;
        }
    }

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
 * The maze has no border tiles: what walls it in is the margin left over from
 * PM_COLS * PM_TILE, painted in the wall colour at the very edge of the panel.
 * The sides have to open up wherever a row runs off into the tunnel.
 */
static void paint_border(const pm_game *g) {
    if (PM_MARGIN == 0) {
        return; /* paint() covers the whole panel and draws the line itself */
    }

    uint16_t line = g->flash ? pal.wall_flash : pal.wall_edge;
    uint8_t hi = (uint8_t)(line >> 8), lo = (uint8_t)(line & 0xFF);
    uint8_t *out = scratch;

    for (int i = 0; i < PM_PANEL * PM_MARGIN; i++) {
        *out++ = hi;
        *out++ = lo;
    }
    pm_blit(0, 0, PM_PANEL, PM_MARGIN, scratch);
    pm_blit(0, PM_PANEL - PM_MARGIN, PM_PANEL, PM_MARGIN, scratch);

    for (int side = 0; side < 2; side++) {
        out = scratch;
        for (int y = 0; y < PM_HEIGHT; y++) {
            bool tunnel = (g->tunnel_rows >> (y / PM_TILE)) & 1u;
            uint16_t c = tunnel ? pal.bg : line;
            for (int i = 0; i < PM_MARGIN; i++) {
                *out++ = (uint8_t)(c >> 8);
                *out++ = (uint8_t)(c & 0xFF);
            }
        }
        pm_blit((uint16_t)(side ? PM_PANEL - PM_MARGIN : 0), PM_MARGIN, PM_MARGIN, PM_HEIGHT,
                scratch);
    }
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
