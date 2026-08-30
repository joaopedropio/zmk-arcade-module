/*
 * The dongle's own renderer, built for a browser.
 *
 * The configurator page needs to show what a colour will actually look like,
 * and the only honest way to do that is to run the code that draws it.  So
 * this is tools/sim with the file writing taken out and emscripten exports
 * put in: the same pacman_core.c and pacman_render.c the firmware runs, given
 * a frame buffer to blit into and a palette to be told about.
 *
 * That is what widgets/game/ being strictly portable C buys - the browser is
 * just one more host.  A preview drawn any other way would be a drawing of
 * the maze rather than the maze, and would drift the first time PM_TILE or a
 * sprite changed.
 *
 * SPDX-License-Identifier: MIT
 */

#include <emscripten/emscripten.h>
#include <string.h>

#include "pacman_core.h"
#include "pacman_render.h"

static uint16_t fb[PM_PANEL][PM_PANEL];
static pm_game game;

/*
 * The thirteen game colours in the order settings_list.h lists them, so the
 * page can write its own table straight in without a mapping of its own.
 */
enum {
    PREVIEW_BG,
    PREVIEW_WALL,
    PREVIEW_WALL_FILL,
    PREVIEW_WALL_FLASH,
    PREVIEW_HOUSE,
    PREVIEW_DOOR,
    PREVIEW_PELLET,
    PREVIEW_PAC,
    PREVIEW_GHOST_0,
    PREVIEW_GHOST_1,
    PREVIEW_GHOST_2,
    PREVIEW_GHOST_3,
    PREVIEW_FRIGHT,
    PREVIEW_COLOR_COUNT,
};

static uint32_t colors[PREVIEW_COLOR_COUNT];

/* what the renderer calls; the firmware sends this down SPI instead */
void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
            uint16_t dx = x + i, dy = y + j;
            if (dx >= PM_PANEL || dy >= PM_PANEL) {
                continue;
            }
            const uint8_t *p = pixels + 2 * (j * w + i);
            fb[dy][dx] = (uint16_t)((p[0] << 8) | p[1]);
        }
    }
}

EMSCRIPTEN_KEEPALIVE int preview_panel(void) { return PM_PANEL; }

EMSCRIPTEN_KEEPALIVE uint16_t *preview_framebuffer(void) { return &fb[0][0]; }

EMSCRIPTEN_KEEPALIVE uint32_t *preview_colors(void) { return colors; }

EMSCRIPTEN_KEEPALIVE int preview_color_count(void) { return PREVIEW_COLOR_COUNT; }

/* the same quantisation the panel does, so the page can match a pixel back */
EMSCRIPTEN_KEEPALIVE uint16_t preview_rgb565(uint32_t rgb888) { return pm_rgb565(rgb888); }

/*
 * Mirrors pacman_reload_palette() in widgets/pacman.c, including the house
 * being filled with the wall's colour rather than one of its own.  If that
 * one changes, this has to change with it.
 */
EMSCRIPTEN_KEEPALIVE void preview_apply_colors(void) {
    pm_palette p;
    pm_render_default_palette(&p);

    p.bg = pm_rgb565(colors[PREVIEW_BG]);
    p.wall_fill = pm_rgb565(colors[PREVIEW_WALL_FILL]);
    p.wall_edge = pm_rgb565(colors[PREVIEW_WALL]);
    p.wall_flash = pm_rgb565(colors[PREVIEW_WALL_FLASH]);
    p.house_fill = p.wall_fill;
    p.house_edge = pm_rgb565(colors[PREVIEW_HOUSE]);
    p.door = pm_rgb565(colors[PREVIEW_DOOR]);
    p.pellet = pm_rgb565(colors[PREVIEW_PELLET]);
    p.pac = pm_rgb565(colors[PREVIEW_PAC]);
    p.ghost[0] = pm_rgb565(colors[PREVIEW_GHOST_0]);
    p.ghost[1] = pm_rgb565(colors[PREVIEW_GHOST_1]);
    p.ghost[2] = pm_rgb565(colors[PREVIEW_GHOST_2]);
    p.ghost[3] = pm_rgb565(colors[PREVIEW_GHOST_3]);
    p.fright_body = pm_rgb565(colors[PREVIEW_FRIGHT]);

    pm_render_set_palette(&p);
    game.redraw = true;
}

/*
 * A fixed seed on purpose: the preview should look the same every time the
 * page is opened, so a colour can be compared against the last one rather
 * than against a different arrangement of ghosts.
 */
EMSCRIPTEN_KEEPALIVE void preview_reset(uint32_t seed) {
    memset(fb, 0, sizeof(fb));
    pm_init(&game, seed ? seed : 1u);
    pm_set_speed(&game, 4, 4);
    game.redraw = true;
    pm_render_frame(&game);
}

EMSCRIPTEN_KEEPALIVE void preview_step(void) {
    pm_step(&game);
    pm_render_frame(&game);
}

/* redraw everything without advancing, for when only a colour moved */
EMSCRIPTEN_KEEPALIVE void preview_repaint(void) {
    game.redraw = true;
    pm_render_frame(&game);
}
