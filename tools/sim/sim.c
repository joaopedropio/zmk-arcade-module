/*
 * Host simulator for the dongle's games.
 *
 * Runs the very same game cores and renderers that the firmware runs, but
 * blits into a plain PM_PANEL square frame buffer and writes PPM frames, so the
 * animation can be checked (and eyeballed) without flashing anything.
 *
 *   tools/sim/build.sh /tmp/pacman-sim
 *   /tmp/pacman-sim [pacman|shooter|bomber|fighter|commando] <frames> \
 *                   <every-nth-frame> <out-dir> \
 *                   [first-frame] [speed 1-5]
 *
 * The game name may be left out, and then it is the maze - so the command in
 * CLAUDE.md still means what it did.  Every game is checked the same way:
 * every frame the incremental redraw is compared against a full repaint, and
 * whatever else is true of that game (nobody inside a wall, nobody off the
 * panel) is checked alongside it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../boards/shields/pacman_adapter/widgets/game/pacman_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/shooter_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/bomber_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/fighter_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/commando_render.h"

static uint16_t fb[PM_PANEL][PM_PANEL];
static long blit_calls;
static long blit_pixels;

void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    if (x + w > PM_PANEL || y + h > PM_PANEL) {
        fprintf(stderr, "blit out of bounds: %u,%u %ux%u\n", x, y, w, h);
        exit(1);
    }
    blit_calls++;
    blit_pixels += (long)w * h;
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
            const uint8_t *p = pixels + 2 * (j * w + i);
            fb[y + j][x + i] = (uint16_t)((p[0] << 8) | p[1]);
        }
    }
}

static void write_ppm(const char *dir, int n) {
    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%05d.ppm", dir, n);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", PM_PANEL, PM_PANEL);
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            uint16_t c = fb[y][x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((((c >> 11) & 0x1F) * 255) / 31);
            rgb[1] = (unsigned char)((((c >> 5) & 0x3F) * 255) / 63);
            rgb[2] = (unsigned char)(((c & 0x1F) * 255) / 31);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* compare the incrementally drawn screen against a full repaint: any
 * difference means the dirty-rectangle bookkeeping left something stale */
static uint16_t shadow[PM_PANEL][PM_PANEL];

static int check_incremental(pm_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    pm_render_frame(g);
    blit_calls = calls;
    blit_pixels = px;

    int diff = 0;
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            if (shadow[y][x] != fb[y][x]) {
                if (!diff) {
                    printf("FRAME %d: stale pixel at %d,%d (%04x != %04x)\n", frame, x, y,
                           shadow[y][x], fb[y][x]);
                }
                diff++;
            }
        }
    }
    return diff;
}

/* sanity: an actor must never sit on a wall tile when it is tile aligned */
static void check(const pm_game *g, int frame) {
    struct {
        const char *who;
        int x, y;
        int ghost;
    } list[PM_ACTORS];

    int cx = (g->pac.x + PM_TILE / 2 + PM_WIDTH) % PM_WIDTH;
    list[0].who = "pac";
    list[0].x = cx / PM_TILE;
    list[0].y = (g->pac.y + PM_TILE / 2) / PM_TILE;
    list[0].ghost = 0;
    for (int i = 0; i < PM_GHOSTS; i++) {
        int gx = (g->ghosts[i].actor.x + PM_TILE / 2 + PM_WIDTH) % PM_WIDTH;
        list[i + 1].who = "ghost";
        list[i + 1].x = gx / PM_TILE;
        list[i + 1].y = (g->ghosts[i].actor.y + PM_TILE / 2) / PM_TILE;
        list[i + 1].ghost = 1;
    }

    for (int i = 0; i < PM_ACTORS; i++) {
        if (list[i].y < 0 || list[i].y >= PM_ROWS) {
            printf("FRAME %d: %s left the board at %d,%d\n", frame, list[i].who, list[i].x,
                   list[i].y);
            exit(1);
        }
        uint8_t t = g->tiles[list[i].y][list[i].x];
        bool bad = (t == PM_T_WALL);
        if (!list[i].ghost && (t == PM_T_HWALL || t == PM_T_HOUSE || t == PM_T_DOOR)) {
            bad = true;
        }
        if (bad) {
            printf("FRAME %d: %s %d inside wall tile %d,%d (tile=%d)\n", frame, list[i].who, i,
                   list[i].x, list[i].y, t);
            exit(1);
        }
    }
}

/* the shooter's own version of the two checks above */
static int check_incremental_ss(ss_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    ss_render_frame(g);
    blit_calls = calls;
    blit_pixels = px;

    int diff = 0;
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            if (shadow[y][x] != fb[y][x]) {
                if (!diff) {
                    printf("FRAME %d: stale pixel at %d,%d (%04x != %04x)\n", frame, x, y,
                           shadow[y][x], fb[y][x]);
                }
                diff++;
            }
        }
    }
    return diff;
}

static void check_ss(const ss_game *g, int frame) {
    int sx = SS_PX(g->ship.x), sy = SS_PX(g->ship.y);

    if (sx - SS_HULL_R < 0 || sx + SS_HULL_R >= PM_PANEL || sy - SS_HULL_R < 0 ||
        sy + SS_HULL_R >= PM_PANEL) {
        printf("FRAME %d: the ship left the panel at %d,%d\n", frame, sx, sy);
        exit(1);
    }
    /* a meteor is allowed off the panel, but only far enough to be forgotten */
    for (int i = 0; i < SS_ROCKS; i++) {
        const ss_rock *r = &g->rocks[i];
        if (!r->alive) {
            continue;
        }
        int rad = ss_rock_r[r->size] + 8;
        int x = SS_PX(r->x), y = SS_PX(r->y);
        if (x < -rad || x > PM_PANEL + rad || y < -rad || y > PM_PANEL + rad) {
            printf("FRAME %d: meteor %d is adrift at %d,%d\n", frame, i, x, y);
            exit(1);
        }
    }
}

static int run_shooter(int frames, int every, const char *dir, int from, int speed) {
    ss_game g;
    ss_init(&g, 12345);
    ss_set_speed(&g, (uint8_t)speed);

    int deaths = 0, stale = 0, powers = 0, overs = 0, out = 0, broken = 0;
    ss_phase phase = g.phase;
    ss_power power = g.power;
    uint32_t top = 0, last_score = 0;
    long rock_frames = 0;

    for (int f = 0; f < frames; f++) {
        ss_step(&g);
        ss_render_frame(&g);
        check_ss(&g, f);

        if (g.score > top) {
            top = g.score;
        }
        if (g.score != last_score) {
            broken++;
            last_score = g.score;
        }
        for (int i = 0; i < SS_ROCKS; i++) {
            rock_frames += g.rocks[i].alive ? 1 : 0;
        }
        if (g.phase != phase) {
            if (g.phase == SS_DEAD) {
                deaths++;
            }
            if (g.phase == SS_OVER) {
                deaths++;
                overs++;
                printf("  f%-6d game over with %u\n", f, (unsigned)top);
            }
            phase = g.phase;
        }
        if (g.power != power) {
            if (g.power != SS_P_NONE) {
                powers++;
                printf("  f%-6d picked up %s\n", f, ss_power_name(&g));
            }
            power = g.power;
        }

        if ((f % 37) == 0) {
            stale += check_incremental_ss(&g, f);
        }
        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d score=%u best=%u lives=%u deaths=%d meteors=%d powerups=%d "
           "restarts=%d\n",
           frames, (unsigned)g.score, (unsigned)top, g.lives, deaths, broken, powers, overs);
    printf("longest wait for a hit: %u frames, %.1f meteors on the panel, stale pixels: %d\n",
           g.patient, (double)rock_frames / frames, stale);
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* the bomber                                                          */
/* ------------------------------------------------------------------ */

static int check_incremental_bb(bb_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    bb_render_frame(g);
    blit_calls = calls;
    blit_pixels = px;

    int diff = 0;
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            if (shadow[y][x] != fb[y][x]) {
                if (!diff) {
                    printf("FRAME %d: stale pixel at %d,%d (%04x != %04x)\n", frame, x, y,
                           shadow[y][x], fb[y][x]);
                }
                diff++;
            }
        }
    }
    return diff;
}

/* nobody stands in a wall, and nobody walks off the board */
static void check_bb(const bb_game *g, int frame) {
    struct {
        const char *who;
        const bb_actor *a;
    } list[BB_FOES + 1];
    int n = 0;

    list[n].who = "the bomber";
    list[n++].a = &g->bomber;
    for (int i = 0; i < BB_FOES; i++) {
        if (g->foes[i].alive) {
            list[n].who = "an enemy";
            list[n++].a = &g->foes[i].a;
        }
    }

    for (int i = 0; i < n; i++) {
        const bb_actor *a = list[i].a;
        if (a->x < 0 || a->x > BB_W - BB_CELL || a->y < 0 || a->y > BB_H - BB_CELL) {
            printf("FRAME %d: %s left the board at %d,%d\n", frame, list[i].who, a->x, a->y);
            exit(1);
        }
        int r = (a->y + BB_CELL / 2) / BB_CELL, c = (a->x + BB_CELL / 2) / BB_CELL;
        if (g->cell[r][c] != BB_C_FLOOR) {
            printf("FRAME %d: %s is inside cell %d,%d (kind=%d)\n", frame, list[i].who, c, r,
                   g->cell[r][c]);
            exit(1);
        }
    }
}

/*
 * What killed it, which is the number a soak is run for: a bomber that blows
 * itself up is a pilot bug, one that is caught is a board that was too busy,
 * and one that runs out of clock is a board it could not find its way into.
 */
static const char *const BB_CAUSE[BB_D_CAUSES] = {"-", "blown up", "caught", "no time"};

static int run_bomber(int frames, int every, const char *dir, int from, int speed) {
    bb_game g;
    bb_init(&g, 12345);
    bb_set_speed(&g, (uint8_t)speed);

    int stale = 0, out = 0, bombs = 0, bricks = 0, foes = 0, items = 0;
    int cleared = 0, deaths = 0, overs = 0;
    int by[BB_D_CAUSES] = {0};
    uint32_t top = 0;
    long alive = 0;

    for (int f = 0; f < frames; f++) {
        bb_step(&g);
        bb_render_frame(&g);
        check_bb(&g, f);

        bombs += (g.sfx & BB_SFX_BOMB) ? 1 : 0;
        bricks += (g.sfx & BB_SFX_BRICK) ? 1 : 0;
        foes += (g.sfx & BB_SFX_FOE) ? 1 : 0;
        items += (g.sfx & BB_SFX_ITEM) ? 1 : 0;
        alive += g.foes_left;
        if (g.score > top) {
            top = g.score;
        }
        if (g.sfx & BB_SFX_CLEAR) {
            cleared++;
            printf("  f%-6d board %u cleared\n", f, g.level);
        }
        if (g.sfx & BB_SFX_DEATH) {
            deaths++;
            by[g.cause]++;
#ifdef BB_TRACE
            printf("  f%-6d died: %s at %d,%d on level %u\n", f, BB_CAUSE[g.cause],
                   (g.bomber.x + 8) / BB_CELL, (g.bomber.y + 8) / BB_CELL, g.level);
#endif
            if (g.phase == BB_OVER) {
                overs++;
            }
        }

        if ((f % 37) == 0) {
            stale += check_incremental_bb(&g, f);
        }
        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d score=%u best=%u level=%u lives=%u cleared=%d deaths=%d restarts=%d\n",
           frames, (unsigned)g.score, (unsigned)top, g.level, g.lives, cleared, deaths, overs);
    printf("bombs=%d bricks=%d enemies=%d pickups=%d, %.1f enemies on the board\n", bombs,
           bricks, foes, items, (double)alive / frames);
    printf("deaths:");
    for (int i = 1; i < BB_D_CAUSES; i++) {
        printf(" %s=%d", BB_CAUSE[i], by[i]);
    }
    printf(", stale pixels: %d\n", stale);
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the ring                                                            */
/* ------------------------------------------------------------------ */

static int check_incremental_fg(fg_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    fg_render_frame(g);
    blit_calls = calls;
    blit_pixels = px;

    int diff = 0;
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            if (shadow[y][x] != fb[y][x]) {
                if (!diff) {
                    printf("FRAME %d: stale pixel at %d,%d (%04x != %04x)\n", frame, x, y,
                           shadow[y][x], fb[y][x]);
                }
                diff++;
            }
        }
    }
    return diff;
}

/* neither fighter may leave the stage, sink into it or carry an impossible bar */
static void check_fg(const fg_game *g, int frame) {
    for (int i = 0; i < 2; i++) {
        const fg_fighter *f = &g->f[i];
        int cx = FG_PX(f->x), fy = FG_FLOOR - FG_PX(f->h);

        if (cx - FG_BODY_W / 2 < 0 || cx + FG_BODY_W / 2 >= PM_PANEL) {
            printf("FRAME %d: fighter %d left the stage at %d\n", frame, i, cx);
            exit(1);
        }
        if (fy > FG_FLOOR || fy - FG_BODY_H < 0) {
            printf("FRAME %d: fighter %d is at %d, off the stage\n", frame, i, fy);
            exit(1);
        }
        if (f->health > FG_HEALTH || g->bar[i] > FG_HEALTH) {
            printf("FRAME %d: fighter %d has %u health and a bar of %u\n", frame, i,
                   f->health, g->bar[i]);
            exit(1);
        }
    }
    /* and neither of them may be standing inside the other */
    int gap = FG_PX(g->f[1].x) - FG_PX(g->f[0].x);
    if (gap < 0) {
        gap = -gap;
    }
    if (g->phase == FG_FIGHT && gap < FG_CLINCH - 1) {
        printf("FRAME %d: the two of them are %d apart\n", frame, gap);
        exit(1);
    }
}

/* how the rounds ended, which is what says whether the two of them fight */
static const char *const FG_END[FG_E_ENDS] = {"-", "knockout", "on the clock"};

static int run_fighter(int frames, int every, const char *dir, int from, int speed) {
    fg_game g;
    fg_init(&g, 12345);
    fg_set_speed(&g, (uint8_t)speed);

    int stale = 0, out = 0, hits = 0, blocks = 0, balls = 0, rounds = 0;
    int by[FG_E_ENDS] = {0};
    int wins[3] = {0, 0, 0};
    long health = 0;

    for (int f = 0; f < frames; f++) {
        fg_step(&g);
        fg_render_frame(&g);
        check_fg(&g, f);

        hits += (g.sfx & FG_SFX_HIT) ? 1 : 0;
        blocks += (g.sfx & FG_SFX_BLOCK) ? 1 : 0;
        balls += (g.sfx & FG_SFX_FIRE) ? 1 : 0;
        health += g.f[0].health + g.f[1].health;
        if (g.sfx & FG_SFX_KO) {
            rounds++;
            by[g.ended]++;
        }
        if (g.sfx & FG_SFX_MATCH) {
            wins[g.winner]++;
        }

        if ((f % 37) == 0) {
            stale += check_incremental_fg(&g, f);
        }
        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d matches=%u rounds=%d round=%u wins=%u-%u\n", frames,
           (unsigned)g.bouts, rounds, g.round, g.wins[0], g.wins[1]);
    printf("hits=%d blocked=%d fireballs=%d, %.1f health on the two bars\n", hits, blocks,
           balls, (double)health / frames);
    printf("rounds ended:");
    for (int i = 1; i < FG_E_ENDS; i++) {
        printf(" %s=%d", FG_END[i], by[i]);
    }
    printf(", matches to p1=%d p2=%d drawn=%d, stale pixels: %d\n", wins[0], wins[1], wins[2],
           stale);
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the ridge                                                           */
/* ------------------------------------------------------------------ */

static int check_incremental_cm(cm_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    cm_render_frame(g);
    blit_calls = calls;
    blit_pixels = px;

    int diff = 0;
    for (int y = 0; y < PM_PANEL; y++) {
        for (int x = 0; x < PM_PANEL; x++) {
            if (shadow[y][x] != fb[y][x]) {
                if (!diff) {
                    printf("FRAME %d: stale pixel at %d,%d (%04x != %04x)\n", frame, x, y,
                           shadow[y][x], fb[y][x]);
                }
                diff++;
            }
        }
    }
    return diff;
}

/* the trooper stands on the ground rather than in it, and never below the
 * panel for more than the frame it takes to notice */
static void check_cm(const cm_game *g, int frame) {
    int feet = CM_PX(g->hero_y);
    int under = cm_surface(g, g->scroll + CM_HERO_X);

    if (g->phase != CM_RUNNING) {
        return; /* it is falling out of the world, or waiting to be put back */
    }
    if (feet >= PM_PANEL) {
        printf("FRAME %d: the trooper is at %d, below the panel\n", frame, feet);
        exit(1);
    }
    if (under != CM_PIT && feet > under) {
        printf("FRAME %d: the trooper is %d inside ground at %d\n", frame, feet - under,
               under);
        exit(1);
    }
    for (int i = 0; i < CM_FOES; i++) {
        const cm_foe *f = &g->foes[i];
        if (!f->alive) {
            continue;
        }
        int x = (int)(f->wx - g->scroll);
        if (x < -CM_CHUNK || x > PM_PANEL + CM_SPAN * CM_CHUNK) {
            printf("FRAME %d: enemy %d is adrift at %d\n", frame, i, x);
            exit(1);
        }
    }
}

/* what took the last life, which is the number a soak is run for */
static const char *const CM_CAUSE[CM_D_CAUSES] = {"-", "shot", "walked into", "fell"};

static int run_commando(int frames, int every, const char *dir, int from, int speed) {
    cm_game g;
    cm_init(&g, 12345);
    cm_set_speed(&g, (uint8_t)speed);

    int stale = 0, out = 0, kills = 0, shots = 0, nades = 0, crates = 0, jumps = 0;
    int deaths = 0, overs = 0;
    int by[CM_D_CAUSES] = {0};
    uint32_t top = 0;
    long ground = 0, best = 0, run = 0;
    int32_t was = g.scroll;

    for (int f = 0; f < frames; f++) {
        cm_step(&g);
        cm_render_frame(&g);
        check_cm(&g, f);

        if (g.scroll > was) {
            ground += g.scroll - was;
            run += g.scroll - was;
        } else if (g.scroll < was) {
            run = 0; /* a new run starts the world again */
        }
        was = g.scroll;
        if (run > best) {
            best = run;
        }
        if (g.score > top) {
            top = g.score;
        }

        kills += (g.sfx & CM_SFX_KILL) ? 1 : 0;
        shots += (g.sfx & CM_SFX_SHOT) ? 1 : 0;
        nades += (g.sfx & CM_SFX_NADE) ? 1 : 0;
        crates += (g.sfx & CM_SFX_PICKUP) ? 1 : 0;
        jumps += (g.sfx & CM_SFX_JUMP) ? 1 : 0;
        if (g.sfx & CM_SFX_DEATH) {
            deaths++;
            by[g.cause]++;
            if (g.lives == 0) {
                overs++;
            }
        }

        if ((f % 37) == 0) {
            stale += check_incremental_cm(&g, f);
        }
        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d score=%u best=%u lives=%u deaths=%d restarts=%d\n", frames,
           (unsigned)g.score, (unsigned)top, g.lives, deaths, overs);
    printf("ground covered=%ld px, longest run=%ld px; kills=%d shots=%d grenades=%d "
           "crates=%d jumps=%d\n",
           ground, best, kills, shots, nades, crates, jumps);
    printf("deaths:");
    for (int i = 1; i < CM_D_CAUSES; i++) {
        printf(" %s=%d", CM_CAUSE[i], by[i]);
    }
    printf(", stale pixels: %d\n", stale);
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *game = "pacman";

    /* an optional game name in front, so the old command line still works */
    if (argc > 1 && (strcmp(argv[1], "shooter") == 0 || strcmp(argv[1], "pacman") == 0 ||
                     strcmp(argv[1], "bomber") == 0 || strcmp(argv[1], "fighter") == 0 ||
                     strcmp(argv[1], "commando") == 0)) {
        game = argv[1];
        argv++;
        argc--;
    }

    int frames = argc > 1 ? atoi(argv[1]) : 1200;
    int every = argc > 2 ? atoi(argv[2]) : 0;
    const char *dir = argc > 3 ? argv[3] : NULL;
    int from = argc > 4 ? atoi(argv[4]) : 0;
    int speed = argc > 5 ? atoi(argv[5]) : 4;

    if (strcmp(game, "shooter") == 0) {
        return run_shooter(frames, every, dir, from, speed);
    }
    if (strcmp(game, "bomber") == 0) {
        return run_bomber(frames, every, dir, from, speed);
    }
    if (strcmp(game, "fighter") == 0) {
        return run_fighter(frames, every, dir, from, speed);
    }
    if (strcmp(game, "commando") == 0) {
        return run_commando(frames, every, dir, from, speed);
    }

    pm_game g;
    pm_init(&g, 12345);
    pm_set_speed(&g, (uint8_t)speed, (uint8_t)speed);

    int deaths = 0, levels = 0, stale = 0;
    uint8_t lives = g.lives;
    uint8_t level = g.level;
    pm_phase phase = g.phase;
    int out = 0;
    bool fright = false;
    uint16_t stuck = 0, last_pellets = g.pellets_left;

    for (int f = 0; f < frames; f++) {
        pm_step(&g);
        pm_render_frame(&g);
        check(&g, f);

        if (g.phase != phase) {
            if (g.phase == PM_DYING) {
                deaths++;
                printf("  f%-6d caught by a ghost\n", f);
            }
            if (g.phase == PM_CLEARED) {
                levels++;
                printf("  f%-6d maze cleared\n", f);
            }
            phase = g.phase;
        }
        if (g.fright > 0 && !fright) {
            printf("  f%-6d power pellet\n", f);
        }
        fright = g.fright > 0;
        if (g.level != level) {
            level = g.level;
        }
        if (g.lives != lives) {
            lives = g.lives;
        }
        if (g.pellets_left != last_pellets) {
            last_pellets = g.pellets_left;
            stuck = 0;
        } else if (g.phase == PM_PLAY) {
            stuck++;
        }

        if ((f % 37) == 0) {
            stale += check_incremental(&g, f);
        }

        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d score=%u level=%u lives=%u pellets_left=%u deaths=%d cleared=%d\n", frames,
           (unsigned)g.score, g.level, g.lives, g.pellets_left, deaths, levels);
    printf("longest stretch without eating: %u frames, stale pixels: %d\n", stuck, stale);
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
}
