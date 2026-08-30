/*
 * Pac-Man dongle widget - ZMK/LVGL glue.
 *
 * The game itself lives in widgets/game/ and knows nothing about Zephyr.
 * This file owns the display device, turns the stored colours into a
 * palette, ticks the game from an LVGL timer and (optionally) speeds it
 * up while you type.  The colours, the tick and the typing thresholds all
 * come from helpers/settings.h rather than straight from Kconfig, so the
 * shell can change them without a rebuild.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>

#include "helpers/settings.h"
#include "pacman.h"
#include "sound.h"
#include "game/pacman_render.h"

LOG_MODULE_REGISTER(pacman, LOG_LEVEL_INF);

static const struct device *display_dev;
static pm_game game;
static bool running;
static bool paused;
static uint32_t frames;

/* the panel can be mounted any way up; rotating costs a scratch buffer and
 * a pixel copy, so it is compiled in only when it is actually asked for */
#define PACMAN_ROTATION CONFIG_PACMAN_ROTATE_DISPLAY

#if PACMAN_ROTATION == 90 || PACMAN_ROTATION == 180 || PACMAN_ROTATION == 270
#define PACMAN_ROTATED 1
static uint8_t rot_buf[PM_BLIT_MAX * 2];
#endif

/* the panel keeps its contents while ZMK blanks it, but repaint everything
 * once in a while so a stray redraw can never leave the maze half drawn */
#define PACMAN_FULL_REDRAW_FRAMES 1800

/* ------------------------------------------------------------------ */
/* blitting                                                            */
/* ------------------------------------------------------------------ */

void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    if (!display_dev) {
        return;
    }

    struct display_buffer_descriptor desc;
    uint16_t dx = x, dy = y, dw = w, dh = h;

#ifdef PACMAN_ROTATED
#if PACMAN_ROTATION != 180
    dw = h;
    dh = w;
#endif
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
#if PACMAN_ROTATION == 90
            uint16_t ti = (uint16_t)(h - 1 - j), tj = i;
#elif PACMAN_ROTATION == 270
            uint16_t ti = j, tj = (uint16_t)(w - 1 - i);
#else
            uint16_t ti = (uint16_t)(w - 1 - i), tj = (uint16_t)(h - 1 - j);
#endif
            const uint8_t *src = pixels + 2 * (j * w + i);
            uint8_t *dst = rot_buf + 2 * (tj * dw + ti);
            dst[0] = src[0];
            dst[1] = src[1];
        }
    }
#if PACMAN_ROTATION == 90
    dx = (uint16_t)(PM_PANEL - y - h);
    dy = x;
#elif PACMAN_ROTATION == 270
    dx = y;
    dy = (uint16_t)(PM_PANEL - x - w);
#else
    dx = (uint16_t)(PM_PANEL - x - w);
    dy = (uint16_t)(PM_PANEL - y - h);
#endif
    pixels = rot_buf;
#endif /* PACMAN_ROTATED */

    desc.buf_size = (uint32_t)dw * dh * 2u;
    desc.pitch = dw;
    desc.width = dw;
    desc.height = dh;
    display_write(display_dev, dx, dy, &desc, pixels);
}

/* ------------------------------------------------------------------ */
/* colours                                                             */
/*
 * The palette is rebuilt rather than patched: every colour is one settings
 * entry, and reading all thirteen back costs less than tracking which one
 * moved.  Called again whenever the shell changes any of them.
 */
void pacman_reload_palette(void) {
    pm_palette p;
    pm_render_default_palette(&p);

    p.bg = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_BG));
    p.wall_fill = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_WALL_FILL));
    p.wall_edge = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_WALL));
    p.wall_flash = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_WALL_FLASH));
    p.house_fill = p.wall_fill;
    p.house_edge = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_HOUSE));
    p.door = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_DOOR));
    p.pellet = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_PELLET));
    p.pac = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_PAC));
    p.ghost[0] = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_GHOST_0));
    p.ghost[1] = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_GHOST_1));
    p.ghost[2] = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_GHOST_2));
    p.ghost[3] = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_GHOST_3));
    p.fright_body = pm_rgb565(pacman_settings_get(PACMAN_SETTING_GAME_FRIGHT));

    pm_render_set_palette(&p);
    game.redraw = true;
}

/* ------------------------------------------------------------------ */
/* typing speed                                                        */
/* ------------------------------------------------------------------ */

struct pacman_wpm_state {
    uint8_t wpm;
};

/*
 * Speeds are pixels per frame, so they set how fast the maze crosses the panel
 * rather than how many tiles pass per second.  3 and 4 divide a 24px tile
 * exactly and step evenly; 5 does not, and just spends a short frame arriving
 * at each tile, since advance() never overshoots one.
 */
static void apply_wpm(uint8_t wpm) {
#if IS_ENABLED(CONFIG_PACMAN_WPM_SPEED)
    uint8_t speed = 4;
    if (wpm > pacman_settings_get(PACMAN_SETTING_WPM_FAST)) {
        speed = 5;
    } else if (wpm < pacman_settings_get(PACMAN_SETTING_WPM_SLOW)) {
        speed = 3;
    }
    pm_set_speed(&game, speed, speed);
#else
    ARG_UNUSED(wpm);
#endif
}

static struct pacman_wpm_state pacman_wpm_get_state(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    return (struct pacman_wpm_state){.wpm = (ev != NULL) ? (uint8_t)ev->state : 0};
}

static void pacman_wpm_update_cb(struct pacman_wpm_state state) { apply_wpm(state.wpm); }

ZMK_DISPLAY_WIDGET_LISTENER(pacman_wpm, struct pacman_wpm_state, pacman_wpm_update_cb,
                            pacman_wpm_get_state)
ZMK_SUBSCRIPTION(pacman_wpm, zmk_wpm_state_changed);

/* ------------------------------------------------------------------ */
/* the loop                                                            */
/* ------------------------------------------------------------------ */

static lv_timer_t *frame_timer;

/*
 * Nothing before zmk_widget_pacman_init() has a timer to retime, and
 * configure() runs first - so a period arriving early is stored and picked up
 * when the timer is made.
 */
void pacman_set_frame_interval(uint32_t ms) {
    if (frame_timer == NULL) {
        return;
    }
    lv_timer_set_period(frame_timer, ms);
}

static void pacman_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    if (!running || paused) {
        return;
    }

    if (++frames >= PACMAN_FULL_REDRAW_FRAMES) {
        frames = 0;
        game.redraw = true;
    }

    pm_step(&game);
    pm_render_frame(&game);
}

void zmk_widget_pacman_init(void) {
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("display device is not ready");
        display_dev = NULL;
        return;
    }

    pacman_reload_palette();
    pacman_sound_init();
    pm_init(&game, (uint32_t)k_uptime_get_32() | 1u);
    pm_set_speed(&game, 4, 4);

    pacman_wpm_init();

    frame_timer = lv_timer_create(pacman_timer_cb,
                                  pacman_settings_get(PACMAN_SETTING_FRAME_INTERVAL), NULL);
}

void pacman_start(void) {
    running = true;
    paused = false;
    game.redraw = true;
}

void pacman_stop(void) {
    running = false;
}

void pacman_toggle_pause(void) {
    paused = !paused;
    if (!paused) {
        game.redraw = true;
    }
}

bool pacman_is_paused(void) { return paused; }
