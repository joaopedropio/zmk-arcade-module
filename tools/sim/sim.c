/*
 * Host simulator for the dongle's three games.
 *
 * Runs the very same game cores and renderers that the firmware runs, but
 * blits into a plain PM_PANEL square frame buffer and writes PPM frames, so the
 * animation can be checked (and eyeballed) without flashing anything.
 *
 *   tools/sim/build.sh /tmp/pacman-sim
 *   /tmp/pacman-sim [pacman|shooter|frogger] <frames> <every-nth-frame> <out-dir> \
 *                   [first-frame] [speed 1-5]
 *
 * Building it with -DFR_TRACE makes the crossing print a line for every death
 * saying what killed the frog and where, which is what any change to its pilot
 * has to be judged on: the counts alone cannot tell a frog that misjudged a
 * lane from one that ran out of clock waiting for a lane it liked.
 *
 * The game name may be left out, and then it is the maze - so the command in
 * CLAUDE.md still means what it did.  All three games are checked the same way:
 * every frame the incremental redraw is compared against a full repaint, and
 * whatever else is true of that game (nobody inside a wall, nobody standing on
 * water, nobody off the panel) is checked alongside it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../boards/shields/pacman_adapter/widgets/game/pacman_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/shooter_render.h"
#include "../../boards/shields/pacman_adapter/widgets/game/frogger_render.h"

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

/* the crossing's version of the same two checks */
static int check_incremental_fr(fr_game *g, int frame) {
    memcpy(shadow, fb, sizeof(fb));
    long calls = blit_calls, px = blit_pixels;
    g->redraw = true;
    fr_render_frame(g);
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

static void check_fr(const fr_game *g, int frame) {
    int fx = FR_PX(g->frog.x), fy = FR_PX(g->frog.y);

    /* where a dead frog is lying is the splat's business, not the board's */
    if (g->phase != FR_PLAY) {
        return;
    }
    if (fx - FR_FROG_W / 2 < 0 || fx + FR_FROG_W / 2 >= PM_PANEL || fy < FR_TOP ||
        fy >= FR_FOOT) {
        printf("FRAME %d: the frog left the board at %d,%d\n", frame, fx, fy);
        exit(1);
    }
    if (g->frog.row > FR_ROW_START) {
        printf("FRAME %d: the frog is on row %u\n", frame, g->frog.row);
        exit(1);
    }
    /* the frog is drawn on the row it says it is on, give or take a hop */
    int want = FR_ROW_Y(g->frog.row) + FR_CELL / 2;
    if (fy < want - FR_CELL || fy > want + FR_CELL) {
        printf("FRAME %d: the frog is at y %d but says row %u\n", frame, fy, g->frog.row);
        exit(1);
    }
    for (int r = 0; r < FR_ROWS; r++) {
        const fr_lane *l = &g->lanes[r];
        for (int i = 0; i < l->count; i++) {
            int x = fr_mover_x(g, l, i, 0);
            if (x < -FR_RUNOFF || x > PM_PANEL) {
                printf("FRAME %d: lane %d mover %d is adrift at %d\n", frame, r, i, x);
                exit(1);
            }
        }
    }

    /* and the crossing's version of "nobody is inside a wall": a frog that has
     * landed in the river is on something, or the core should have drowned it */
    if (g->phase != FR_PLAY || g->frog.hop > 0 || g->frog.row < FR_ROW_RIVER ||
        g->frog.row >= FR_ROW_MEDIAN) {
        return;
    }
    const fr_lane *l = &g->lanes[g->frog.row];
    for (int i = 0; i < l->count; i++) {
        int x = fr_mover_x(g, l, i, 0);
        if (fx >= x && fx < x + l->span[i] && fr_turtle_sunk(g, l, i, 0) < FR_DIVE_WARN) {
            return;
        }
    }
    printf("FRAME %d: the frog is standing on water at %d, row %u\n", frame, fx, g->frog.row);
    exit(1);
}

static int run_frogger(int frames, int every, const char *dir, int from, int speed) {
    fr_game g;
    fr_init(&g, 12345);
    fr_set_speed(&g, (uint8_t)speed);

    int deaths = 0, homes = 0, levels = 0, overs = 0, stale = 0, out = 0;
    int why[8] = {0};
    static const char *const WHY[] = {"-",    "run over", "drowned", "sunk",
                                      "edge", "hedge",    "no time"};
    fr_phase phase = g.phase;
    uint32_t top = 0;
    long airborne = 0;
    long dwell[FR_ROWS] = {0};

    for (int f = 0; f < frames; f++) {
        fr_step(&g);
        fr_render_frame(&g);
        check_fr(&g, f);

        if (g.score > top) {
            top = g.score;
        }
        airborne += g.frog.hop > 0 ? 1 : 0;
        if (g.phase == FR_PLAY) {
            dwell[g.frog.row]++;
        }
        if (g.phase != phase) {
            if (g.phase == FR_DYING) {
                deaths++;
                why[g.why & 7]++;
#ifdef FR_TRACE
                printf("  f%-6d died %s on row %u at x=%d, think=%u, clock=%u\n", f,
                       WHY[g.why & 7], g.frog.row, FR_PX(g.frog.x), g.think, g.clock);
#endif
            }
            if (g.phase == FR_HOMED) {
                homes++;
            }
            if (g.phase == FR_LEVEL) {
                homes++;
                levels++;
                printf("  f%-6d level %u done with %u\n", f, g.level, (unsigned)g.score);
            }
            if (g.phase == FR_OVER) {
                overs++;
                printf("  f%-6d game over with %u\n", f, (unsigned)top);
            }
            phase = g.phase;
        }

        if ((f % 37) == 0) {
            stale += check_incremental_fr(&g, f);
        }
        if (every > 0 && dir && f >= from && (f % every) == 0) {
            write_ppm(dir, out++);
        }
    }

    printf("frames=%d score=%u best=%u level=%u lives=%u homes=%d levels=%d deaths=%d "
           "restarts=%d\n",
           frames, (unsigned)g.score, (unsigned)top, g.level, g.lives, homes, levels, deaths,
           overs);
    printf("deaths:");
    for (int i = 1; i <= 6; i++) {
        printf(" %s=%d", WHY[i], why[i]);
    }
    printf(", %.0f%% of frames mid-hop, stale pixels: %d\n", 100.0 * airborne / frames, stale);
    printf("frames spent on each row, home first:");
    for (int r = 0; r < FR_ROWS; r++) {
        printf(" %ld", dwell[r]);
    }
    printf("\n");
    printf("blits=%ld pixels=%ld (%.1f px/frame, %.1f blits/frame)\n", blit_calls, blit_pixels,
           (double)blit_pixels / frames, (double)blit_calls / frames);
    if (out) {
        printf("wrote %d frames to %s\n", out, dir);
    }
    return 0;
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

int main(int argc, char **argv) {
    const char *game = "pacman";

    /* an optional game name in front, so the old command line still works */
    if (argc > 1 && (strcmp(argv[1], "shooter") == 0 || strcmp(argv[1], "pacman") == 0 ||
                     strcmp(argv[1], "frogger") == 0)) {
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
    if (strcmp(game, "frogger") == 0) {
        return run_frogger(frames, every, dir, from, speed);
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
