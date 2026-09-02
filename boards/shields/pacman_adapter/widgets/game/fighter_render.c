/*
 * Street Fighter dongle - renderer (portable).
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "fighter_render.h"

/* a full repaint is split into rows, so the band only has to hold one */
_Static_assert(PM_PANEL <= PM_BAND_PX, "panel.h's band is narrower than the panel");

static fg_palette pal;
static bool pal_ready;

void fg_render_default_palette(fg_palette *p) {
    p->sky = pm_rgb565(0x2a1a4a);
    p->crowd = pm_rgb565(0x53306b);
    p->floor = pm_rgb565(0x8a5a2b);
    p->body[0] = pm_rgb565(0xf2f2f2);
    p->trim[0] = pm_rgb565(0xd42a2a);
    p->body[1] = pm_rgb565(0xffcf3f);
    p->trim[1] = pm_rgb565(0x1e73ff);
    p->spark = pm_rgb565(0xfff4c2);
    p->fireball = pm_rgb565(0x4fd8ff);
    p->health = pm_rgb565(0xffe000);
    p->health_lost = pm_rgb565(0xd03020);
    p->hud = pm_rgb565(0xffee00);
}

void fg_render_set_palette(const fg_palette *p) {
    pal = *p;
    pal_ready = true;
}

/*
 * The same trick the brick field uses: a lit edge and a shaded one out of the
 * colour that was set rather than out of two more settings.  Boots, a shadowed
 * torso and the seams in the floor are all "that colour, moved", and asking a
 * preset for six more shades of it would be six more things for a preset to
 * get wrong.
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
/* the stage                                                           */
/* ------------------------------------------------------------------ */

/*
 * Backdrop, back wall and floor, and none of the three moves.  The wall gets a
 * row of windows and the floor a few seams, purely so that a fighter walking
 * across has something to be measured against - two figures on flat colour
 * look like they are standing still whatever they do.  All of it is locked to
 * the panel rather than to anything in the game, which is what makes it free:
 * a rectangle of stage is the same rectangle every time it is painted.
 */
static void draw_stage(const fg_game *g, int x0, int w) {
    pm_fill(x0, FG_OY, w, FG_HORIZON - FG_OY, pal.sky);
    pm_fill(x0, FG_HORIZON, w, FG_FLOOR - FG_HORIZON, pal.crowd);

    uint16_t lit = shade(pal.crowd, 5, 4), dark = shade(pal.crowd, 2, 3);
    for (int y = FG_HORIZON + 8; y < FG_FLOOR - 24; y += 26) {
        for (int x = 6; x < PM_PANEL; x += 26) {
            pm_fill(x, y, 12, 16, dark);
            pm_fill(x, y, 12, 2, lit);
        }
    }
    pm_fill(x0, FG_HORIZON - 2, w, 2, lit);

    /* the floor, and the front edge of it, which is what gives the stage a
     * depth for the fighters to be standing on rather than in front of */
    uint16_t face = g->flash ? pal.spark : pal.floor;
    pm_fill(x0, FG_FLOOR, w, PM_PANEL - FG_FLOOR, face);
    pm_fill(x0, FG_FLOOR, w, 3, shade(face, 5, 4));
    for (int x = 10; x < PM_PANEL; x += 34) {
        pm_fill(x, FG_FLOOR + 4, 2, PM_PANEL - FG_FLOOR - 4, shade(face, 3, 4));
    }

    /* and the band the readout is written on */
    pm_fill(x0, 0, w, FG_OY, shade(pal.crowd, 1, 3));
}

/* ------------------------------------------------------------------ */
/* the fighters                                                        */
/* ------------------------------------------------------------------ */

/* flat out on the stage, head trailing behind where it was standing */
static void draw_down(int cx, int fy, int s, uint16_t body, uint16_t trim) {
    int hx = s > 0 ? cx - 22 : cx + 12;

    pm_fill(cx - 16, fy - 8, 32, 8, body);
    pm_fill(cx - 16, fy - 8, 32, 2, shade(body, 5, 4));
    pm_fill(hx, fy - 10, 10, 10, body);
    pm_fill(hx, fy - 7, 10, 3, trim);
}

/*
 * Where the kicking leg is hinged.  The drawing and the dirty rectangle around
 * it both take the top of the thigh from here, because a leg that reaches a
 * pixel higher than the box was measured for is a row of stale pixels that
 * only shows up on a sweep.
 */
static int hip_y(const fg_fighter *f, int fy) {
    return fy - FG_LEG_H(fg_height(f)) + FG_HIP_DROP;
}

/*
 * How much of a swing is drawn: all of a punch, and a leg's worth of a kick.
 * Scaled rather than clipped, so the leg still goes out and comes back over
 * the same frames the reach does - a leg that snaps to its full length and
 * holds there has no swing in it.
 */
static int drawn_reach(int reach, int high) {
    return high ? reach : reach * FG_KICK_DRAW / FG_KICK_REACH;
}

/*
 * The leg that is out, as a column at a time: it leaves the hip, drops to the
 * sweep's height by the knee and runs flat from there to the boot, thinning
 * from a thigh to an ankle on the way.  The taper is the whole of what makes
 * it a leg rather than a stick - at forty pixels long and six thick there is
 * no room for a shape, so the only thing left to say "leg" is that it is wider
 * where it joins the body and narrower where it does not.
 *
 * A column at a time rather than three rectangles because the knee moves out
 * with the reach while the leg is extending, and a stepped thigh that changes
 * where its steps are from frame to frame flickers.
 */
static void draw_kick(int cx, int hip, int mid, int s, int reach, uint16_t body,
                      uint16_t trim) {
    int knee = reach * FG_KNEE_AT / 100;

    if (knee < 1) {
        knee = 1;
    }
    for (int i = 0; i < reach; i++) {
        int y = mid, th = FG_SHIN_H;

        if (i < knee) {
            y = hip + (mid - hip) * i / knee;
            th = FG_THIGH_H - (FG_THIGH_H - FG_SHIN_H) * i / knee;
        }
        pm_fill(cx + (s > 0 ? i : -1 - i), y - th / 2, 1, th, body);
    }

    /* the boot, with a sole under it: a foot at the end of the leg is what
     * says which way round the thing is, and it is what a sweep connects with.
     * It is never wider than the leg is long, because the first and last
     * frames of a swing reach five pixels and a boot drawn to its full width
     * there sticks out of the fighter's back - which is both wrong to look at
     * and outside the rectangle this is repainted in */
    int bw = reach < FG_BOOT_W ? reach : FG_BOOT_W;
    int bx = s > 0 ? cx + reach - bw : cx - reach;
    int by = mid - FG_BOOT_H / 2 + 1;
    pm_fill(bx, by, bw, FG_BOOT_H, trim);
    pm_fill(bx, by + FG_BOOT_H - 3, bw, 3, shade(trim, 1, 2));
}

/*
 * A fighter as a dozen rectangles: legs, belt, torso, head, headband, and
 * whichever arm the state calls for.  It is drawn from the feet up out of the
 * height fg_height() reports rather than from a fixed table, so a crouch is
 * the same sprite compressed and there is only one of these to keep right.
 *
 * The headband streams out behind the head, and it is the one piece of the
 * fighter that is there only to say which way it is facing: a symmetrical
 * sprite at this size gives no answer at all, and which way somebody is facing
 * is what every other thing on this stage depends on.
 */
static void draw_fighter(const fg_game *g, int who) {
    const fg_fighter *f = &g->f[who];
    uint16_t body = pal.body[who], trim = pal.trim[who];
    int cx = FG_PX(f->x), fy = FG_FLOOR - FG_PX(f->h);
    int s = f->face ? 1 : -1;

    if (f->state == FG_S_DOWN) {
        draw_down(cx, fy, s, body, trim);
        return;
    }

    int hgt = fg_height(f);
    int leg = FG_LEG_H(hgt), head = 12;
    int torso = hgt - leg - head;
    int ty = fy - leg - torso;
    int hy = ty - head;

    /* which limb is out is decided before anything is drawn, because a kick
     * changes the legs and the arms as well as adding to them */
    int high, reach = fg_swing(f, &high);
    bool kicking = reach > 0 && !high;

    /* the legs, and a stride that only shows while there is one */
    bool walking = f->state == FG_S_WALK || f->state == FG_S_BACK;
    int step = walking ? ((f->stride >> 2) & 1 ? 3 : -3) : 0;
    if (kicking) {
        /* one leg is out, so only the other one is holding the fighter up.
         * Leaving both planted and adding a limb below the belt is what made
         * the sweep read as an arm: nothing had left the body to throw it */
        int lx = s > 0 ? cx - 8 : cx + 1;
        pm_fill(lx, fy - leg, 7, leg, body);
        pm_fill(lx, fy - 3, 7, 3, shade(body, 1, 2));
    } else {
        pm_fill(cx - 7 + step, fy - leg, 6, leg, body);
        pm_fill(cx + 1 - step, fy - leg, 6, leg, body);
        pm_fill(cx - 7 + step, fy - 3, 6, 3, shade(body, 1, 2));
        pm_fill(cx + 1 - step, fy - 3, 6, 3, shade(body, 1, 2));
    }

    pm_fill(cx - 7, ty, 14, torso, body);
    pm_fill(cx - 7, ty, 14, 2, shade(body, 5, 4));
    pm_fill(cx - 7, fy - leg - 3, 14, 3, trim);

    /* the head, carried forward of the middle, and further back when hurt */
    int lean = f->state == FG_S_HURT ? -3 : 2;
    int hx = cx - 6 + lean * s;
    pm_fill(hx, hy, 12, head, body);
    pm_fill(hx, hy + 3, 12, 3, trim);
    pm_fill(hx + (s > 0 ? 8 : 2), hy + 7, 2, 2, shade(body, 1, 3));
    pm_fill(s > 0 ? hx - 4 : hx + 12, hy + 3, 4, 2, trim);

    switch (f->state) {
    case FG_S_BLOCK:
        /* the forearm up in front, which is the whole of what a guard is */
        pm_fill(s > 0 ? cx + 5 : cx - 10, ty + 1, 5, torso - 2, trim);
        break;
    case FG_S_FIRE:
        /* both hands out, and the ball gathering in them on the way */
        pm_fill(s > 0 ? cx + 2 : cx - 11, fy - FG_HIGH_Y - FG_HIGH_H + 2, 9, 8, trim);
        break;
    case FG_S_WIN:
        pm_fill(cx - 10, hy + 2, 4, 10, body);
        pm_fill(cx + 6, hy + 2, 4, 10, body);
        break;
    default:
        /* the far arm stays tucked whatever is happening; the near one is
         * drawn only when it is not the thing being thrown - a kick keeps
         * both, carried high for balance, and an arm still up by the chest is
         * half of what tells the eye that the thing down at ankle height is a
         * leg */
        pm_fill(s > 0 ? cx - 10 : cx + 7, ty + 3, 3, 8, body);
        if (reach == 0 || kicking) {
            pm_fill(s > 0 ? cx + 7 : cx - 10, kicking ? ty : ty + 3, 3, 8, body);
        }
        break;
    }

    /*
     * The limb that is out.  A punch is a bar out of the shoulder with a fist
     * on it, drawn to exactly the reach the core hit with, so the picture and
     * the hit cannot disagree about where the fist got to - which is how
     * anybody watching tells a whiff from a block.  A kick is draw_kick(),
     * and it is drawn to FG_KICK_DRAW instead: a leg the length of the sweep's
     * reach is not a leg.  Both still read the same fg_swing(), so both still
     * go out and come back on the frames the core says they do.
     */
    if (reach > 0) {
        int band = high ? FG_HIGH_H : FG_LOW_H;
        int mid = fy - (high ? FG_HIGH_Y : FG_LOW_Y) - band / 2;

        if (high) {
            pm_fill(s > 0 ? cx : cx - reach, mid - 2, reach, 4, body);
            pm_fill(s > 0 ? cx + reach - 6 : cx - reach, mid - 4, 6, 8, trim);
        } else {
            draw_kick(cx, hip_y(f, fy), mid, s, drawn_reach(reach, high), body, trim);
        }
    }
}

/* ------------------------------------------------------------------ */
/* what they throw                                                     */
/* ------------------------------------------------------------------ */

static void disc(int cx, int cy, int r, uint16_t colour) {
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            if (i * i + j * j <= r * r) {
                pm_put(cx + i, cy + j, colour);
            }
        }
    }
}

/*
 * A fireball with a wake behind it.  The head alone at six pixels a frame
 * reads as a dot that teleports; the two discs trailing it are the frames it
 * came from, which is what makes both its speed and its direction visible.
 */
#define FG_BALL_TAIL 3

static void draw_ball(const fg_ball *b) {
    int cx = FG_PX(b->x), cy = FG_PX(b->y);
    int back = b->vx > 0 ? -1 : 1;

    for (int k = FG_BALL_TAIL; k >= 1; k--) {
        disc(cx + back * k * 5, cy, FG_BALL_R - k, shade(pal.fireball, 4 - k, 4));
    }
    disc(cx, cy, FG_BALL_R, pal.fireball);
    disc(cx, cy, FG_BALL_R - 3, shade(pal.fireball, 5, 4));
}

/*
 * A hit, as a burst of spikes; a guarded one as a ring.  They have to be told
 * apart at a glance, because whether the last exchange took health or not is
 * the only thing the bars say a full second later.
 */
static void draw_spark(const fg_spark *s) {
    int r = 3 + (int)s->age;

    if (s->guarded) {
        for (int j = -r; j <= r; j++) {
            for (int i = -r; i <= r; i++) {
                int d2 = i * i + j * j;
                if (d2 <= r * r && d2 > (r - 2) * (r - 2)) {
                    pm_put(s->x + i, s->y + j, pal.spark);
                }
            }
        }
        return;
    }
    for (int k = -r; k <= r; k++) {
        int t = k < 0 ? -k : k;
        pm_put(s->x + k, s->y, pal.spark);
        pm_put(s->x, s->y + k, pal.spark);
        if (t <= r - 2) {
            pm_put(s->x + k, s->y + k, pal.spark);
            pm_put(s->x + k, s->y - k, pal.spark);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the readout                                                         */
/* ------------------------------------------------------------------ */

/*
 * Two bars that empty towards the outside of the panel, the clock between
 * them, and the rounds each fighter has taken underneath.  The bar that is
 * left is drawn over a bar that is still catching up, so a big hit is visible
 * as a moving edge for the second after it lands rather than as a bar that was
 * one length and is now another.
 */
static void draw_bar(int who, uint8_t health, uint8_t lag) {
    int x = who == 0 ? FG_BAR_X : PM_PANEL - FG_BAR_X - FG_BAR_W;

    pm_fill(x - 1, FG_BAR_Y - 1, FG_BAR_W + 2, FG_BAR_H + 2, shade(pal.hud, 1, 3));
    pm_fill(x, FG_BAR_Y, FG_BAR_W, FG_BAR_H, shade(pal.health_lost, 1, 3));

    if (lag > 0) {
        int lx = who == 0 ? x : x + FG_BAR_W - lag;
        pm_fill(lx, FG_BAR_Y, lag, FG_BAR_H, pal.health_lost);
    }
    if (health > 0) {
        int hx = who == 0 ? x : x + FG_BAR_W - health;
        pm_fill(hx, FG_BAR_Y, health, FG_BAR_H, pal.health);
        pm_fill(hx, FG_BAR_Y, health, 2, shade(pal.health, 5, 4));
    }
}

static void draw_hud(const fg_game *g) {
    char buf[4];

    for (int i = 0; i < 2; i++) {
        draw_bar(i, g->f[i].health, g->bar[i]);
        for (int k = 0; k < g->wins[i] && k < FG_ROUNDS_WIN; k++) {
            int x = i == 0 ? FG_BAR_X + k * (FG_PIP + 3)
                           : PM_PANEL - FG_BAR_X - FG_PIP - k * (FG_PIP + 3);
            pm_fill(x, FG_BAR_Y + FG_BAR_H + 4, FG_PIP, FG_PIP, pal.health);
        }
    }

    /* the clock in whole seconds, which is what a fighting game counts in */
    pm_digits(g->clock / 15, 2, buf);
    pm_text((PM_PANEL - pm_text_w(buf, 2)) / 2, FG_BAR_Y - 2, 2, pal.hud, buf);
}

static void draw_banner(const fg_game *g) {
    const char *word = fg_banner(g);

    if (word != NULL) {
        pm_text((PM_PANEL - pm_text_w(word, FG_BANNER_SCALE)) / 2, FG_BANNER_Y,
                FG_BANNER_SCALE, pal.hud, word);
    }
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */

/*
 * Stage, then the fireballs, then the fighters over them, then the sparks over
 * everything.  A fireball has to go behind the fighter it is about to hit or
 * the hit reads as the ball stopping in front of somebody, and a spark has to
 * go in front of both or it is a flash somebody is standing on.
 */
static void paint_band(const fg_game *g, int x0, int y0, int w, int h) {
    pm_band_begin(x0, y0, w, h);

    draw_stage(g, x0, w);

    for (int i = 0; i < 2; i++) {
        if (g->ball[i].live) {
            draw_ball(&g->ball[i]);
        }
    }
    for (int i = 0; i < 2; i++) {
        draw_fighter(g, i);
    }
    for (int i = 0; i < FG_SPARKS; i++) {
        if (g->sparks[i].age > 0) {
            draw_spark(&g->sparks[i]);
        }
    }
    if (y0 < FG_OY) {
        draw_hud(g);
    }
    draw_banner(g);

    pm_blit((uint16_t)x0, (uint16_t)y0, (uint16_t)w, (uint16_t)h, pm_band);
}

/* a rectangle wider than the band is split into as many rows as fit */
static void paint(const fg_game *g, int x0, int y0, int w, int h) {
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
} fg_box2;

/*
 * A fighter is two rectangles rather than one, and that is worth a paragraph.
 * A sweep reaches forty pixels; a box around the fighter and its foot is two
 * and a half thousand pixels, and it changes on every frame of the swing as
 * the leg goes out and comes back - which was two thirds of everything this
 * game drew.  The body barely changes during an attack, so it keeps its own
 * small box and its own look, and the limb gets a box ten pixels tall that is
 * only there while there is a limb out.  Same picture, a fifth of the panel.
 */
enum {
    FG_B_BALL = 0,
    FG_B_SPARK = 2,
    FG_B_MAN = FG_B_SPARK + FG_SPARKS,
    FG_B_LIMB = FG_B_MAN + 2,
    FG_BOXES = FG_B_LIMB + 2,
};

/*
 * How far past its middle a fighter reaches without swinging anything: the
 * guard, the tucked arms and - the widest of them, which is why this is
 * thirteen and not eleven - the tail of the headband on a fighter leaning
 * away from a punch.  Get it a pixel short and the tail is the one thing on
 * the panel that goes stale, which is exactly how this number was found.
 */
#define FG_SPILL 13

static fg_box2 prev[FG_BOXES];
static bool prev_valid;
static bool prev_flash;
static char prev_banner[12];

/*
 * A fighter's rectangle is its body plus whatever limb is out, which is why it
 * is recomputed from fg_swing() rather than being a constant: a sweep reaches
 * forty pixels and a stationary fighter reaches none, and a box big enough for
 * the first would repaint a third of the stage on every frame of the second.
 *
 * The look beside it is everything that changes the sprite without moving it -
 * the state, the stride, which way it faces - because a fighter that turns on
 * the spot or puts a guard up does both of those without its box shifting a
 * pixel.
 */
static fg_box2 box_of(const fg_game *g, int idx) {
    fg_box2 b = {0, 0, 0, 0, 0, false};

    if (idx < FG_B_SPARK) {
        const fg_ball *ball = &g->ball[idx];
        if (!ball->live) {
            return b;
        }
        int cx = FG_PX(ball->x), cy = FG_PX(ball->y);
        int tail = FG_BALL_TAIL * 5 + FG_BALL_R;
        b.x = (int16_t)(cx - (ball->vx > 0 ? tail : FG_BALL_R) - 1);
        b.w = (int16_t)(tail + FG_BALL_R + 3);
        b.y = (int16_t)(cy - FG_BALL_R - 1);
        b.h = (int16_t)(2 * FG_BALL_R + 3);
        b.look = (uint16_t)(ball->vx > 0);
    } else if (idx < FG_B_MAN) {
        const fg_spark *s = &g->sparks[idx - FG_B_SPARK];
        if (s->age == 0) {
            return b;
        }
        int r = 3 + (int)s->age;
        b.x = (int16_t)(s->x - r - 1);
        b.y = (int16_t)(s->y - r - 1);
        b.w = b.h = (int16_t)(2 * r + 3);
        b.look = (uint16_t)(s->age | (s->guarded ? 0x100 : 0));
    } else if (idx < FG_B_LIMB) {
        int who = idx - FG_B_MAN;
        const fg_fighter *f = &g->f[who];
        int cx = FG_PX(f->x), fy = FG_FLOOR - FG_PX(f->h);
        int hgt = fg_height(f);
        int x0 = cx - FG_SPILL - 1, x1 = cx + FG_SPILL + 1;
        int y0 = fy - hgt - 2;

        if (f->state == FG_S_DOWN) {
            x0 = cx - 23;
            x1 = cx + 23;
            y0 = fy - 12;
        }
        int high;
        b.x = (int16_t)x0;
        b.y = (int16_t)y0;
        b.w = (int16_t)(x1 - x0);
        b.h = (int16_t)(fy + 1 - y0);
        /* whether there is a limb out is in here because the near arm is
         * tucked away while there is, which the body box has to notice */
        b.look = (uint16_t)(f->state | (((f->stride >> 2) & 1) << 5) | (f->face << 6) |
                            ((fg_swing(f, &high) > 0) << 7));
    } else {
        const fg_fighter *f = &g->f[idx - FG_B_LIMB];
        int high, reach = fg_swing(f, &high);
        if (reach == 0) {
            return b;
        }
        int cx = FG_PX(f->x), fy = FG_FLOOR - FG_PX(f->h);
        int band = high ? FG_HIGH_H : FG_LOW_H;
        int mid = fy - (high ? FG_HIGH_Y : FG_LOW_Y) - band / 2;

        int out = drawn_reach(reach, high);
        b.x = (int16_t)((f->face ? cx : cx - out) - 1);
        b.w = (int16_t)(out + 2);
        if (high) {
            b.y = (int16_t)(mid - 5);
            b.h = 10;
        } else {
            /* a kick is taller than a punch: the box has to start at the top
             * of the thigh, up at the hip, and not at the shin it ends on */
            int top = hip_y(f, fy) - FG_THIGH_H / 2 - 1;
            b.y = (int16_t)top;
            b.h = (int16_t)(mid + FG_BOOT_H / 2 + 3 - top);
        }
        b.look = (uint16_t)reach;
    }
    b.on = true;
    return b;
}

static bool overlap(const fg_box2 *a, const fg_box2 *b) {
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x || a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

static void paint_box(const fg_game *g, const fg_box2 *b) { paint(g, b->x, b->y, b->w, b->h); }

/* the union where the two touch, one call each where they do not */
static void paint_move(const fg_game *g, const fg_box2 *a, const fg_box2 *b) {
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
 * The readout is compared and repainted in five pieces rather than as one
 * band, and the bar that trails a hit is the reason.  It moves a pixel a frame
 * for the best part of a second after every exchange, and repainting the whole
 * band on each of those frames came to more pixels than the two fighters put
 * together - where a bar on its own is an eighth of that and touches nothing
 * else.  Each piece keeps the string it last showed, and its own rectangle.
 */
enum { FG_T_BAR = 0, FG_T_PIP = 2, FG_T_CLOCK = 4, FG_TEXTS };

static char prev_text[FG_TEXTS][8];

static void text_key(const fg_game *g, int which, char *buf) {
    if (which < FG_T_PIP) {
        int i = which - FG_T_BAR;
        pm_digits(g->f[i].health, 2, buf);
        pm_digits(g->bar[i], 2, buf + 2);
        return;
    }
    if (which < FG_T_CLOCK) {
        buf[0] = (char)('0' + g->wins[which - FG_T_PIP]);
        buf[1] = '\0';
        return;
    }
    pm_digits(g->clock / 15, 2, buf);
}

static void text_rect(int which, int *x, int *y, int *w, int *h) {
    if (which < FG_T_CLOCK) {
        int i = which & 1;
        int bx = i == 0 ? FG_BAR_X : PM_PANEL - FG_BAR_X - FG_BAR_W;
        if (which < FG_T_PIP) {
            *x = bx - 2;
            *y = FG_BAR_Y - 2;
            *w = FG_BAR_W + 4;
            *h = FG_BAR_H + 4;
            return;
        }
        /* the pips run inwards from the outer end of the bar above them */
        *w = FG_ROUNDS_WIN * (FG_PIP + 3);
        *x = i == 0 ? FG_BAR_X : PM_PANEL - FG_BAR_X - *w;
        *y = FG_BAR_Y + FG_BAR_H + 3;
        *h = FG_PIP + 2;
        return;
    }
    *w = pm_text_w("00", 2) + 4;
    *x = (PM_PANEL - *w) / 2;
    *y = FG_BAR_Y - 4;
    *h = 2 * PM_GLYPH_H + 4;
}

static void repaint_text(const fg_game *g) {
    char buf[8];

    for (int i = 0; i < FG_TEXTS; i++) {
        text_key(g, i, buf);
        if (strcmp(buf, prev_text[i]) == 0) {
            continue;
        }
        memcpy(prev_text[i], buf, strlen(buf) + 1);

        int x, y, w, h;
        text_rect(i, &x, &y, &w, &h);
        paint(g, x, y, w, h);
    }

    const char *word = fg_banner(g);
    const char *now = word != NULL ? word : "";
    if (strcmp(now, prev_banner) != 0) {
        /* the wider of the two, so whichever is going away is wiped as well */
        int was = pm_text_w(prev_banner, FG_BANNER_SCALE);
        int is = pm_text_w(now, FG_BANNER_SCALE);
        int w = (was > is ? was : is) + 2;
        memcpy(prev_banner, now, strlen(now) + 1);
        paint(g, (PM_PANEL - w) / 2, FG_BANNER_Y - 1, w, PM_GLYPH_H * FG_BANNER_SCALE + 2);
    }
}

static void snapshot(const fg_game *g) {
    const char *word = fg_banner(g);

    for (int i = 0; i < FG_BOXES; i++) {
        prev[i] = box_of(g, i);
    }
    prev_flash = g->flash;
    for (int i = 0; i < FG_TEXTS; i++) {
        text_key(g, i, prev_text[i]);
    }
    memcpy(prev_banner, word != NULL ? word : "", word != NULL ? strlen(word) + 1 : 1);
}

void fg_render_frame(fg_game *g) {
    if (!pal_ready) {
        fg_palette def;
        fg_render_default_palette(&def);
        fg_render_set_palette(&def);
    }

    if (g->redraw || !prev_valid) {
        g->redraw = false;
        prev_valid = true;
        paint(g, 0, 0, PM_PANEL, PM_PANEL);
        snapshot(g);
        return;
    }

    /* the stage lights up for the five frames a round is decided in, and the
     * floor is the only part of it that changes - so that is all that is
     * repainted, rather than the whole panel twice a round */
    if (g->flash != prev_flash) {
        prev_flash = g->flash;
        paint(g, 0, FG_FLOOR, PM_PANEL, PM_PANEL - FG_FLOOR);
    }

    for (int i = 0; i < FG_BOXES; i++) {
        fg_box2 now = box_of(g, i);
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
