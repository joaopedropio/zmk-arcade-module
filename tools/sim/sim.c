/*
 * Host simulator for the Pac-Man dongle widget.
 *
 * Runs the very same game core and renderer that the firmware runs, but
 * blits into a plain PM_PANEL square frame buffer and writes PPM frames, so the
 * animation can be checked (and eyeballed) without flashing anything.
 *
 *   cc -O2 -o /tmp/pmsim tools/sim/sim.c \
 *      boards/shields/pacman_adapter/widgets/game/pacman_core.c \
 *      boards/shields/pacman_adapter/widgets/game/pacman_render.c
 *   /tmp/pmsim <frames> <every-nth-frame> <out-dir> [first-frame] [speed 1-3]
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../boards/shields/pacman_adapter/widgets/game/pacman_render.h"

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

int main(int argc, char **argv) {
    int frames = argc > 1 ? atoi(argv[1]) : 1200;
    int every = argc > 2 ? atoi(argv[2]) : 0;
    const char *dir = argc > 3 ? argv[3] : NULL;
    int from = argc > 4 ? atoi(argv[4]) : 0;
    int speed = argc > 5 ? atoi(argv[5]) : 4;

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
