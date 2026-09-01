/*
 * The dongle's own drawing code, built for a browser.
 *
 * The configurator page needs to show what a colour will actually look like,
 * and the only honest way to do that is to run the code that draws it.  So
 * this is tools/sim and tools/uisim with the file writing taken out and
 * emscripten exports put in: the same game core, renderer, splash and
 * dashboard widgets the firmware runs, given a frame buffer to draw into.
 *
 * The dashboard widgets ask ZMK what layer is on and what the batteries are
 * at, which a browser cannot answer, so they are built against
 * tools/uisim/stub - the same stubs the host preview uses, which make the
 * answers up.  That is what the slot contents are: plausible, not live.
 *
 * Which setting drives what is not repeated here.  helpers/settings_list.h is
 * included and walked, so the page can set anything by the same name the
 * shell takes and the two cannot drift; the apply functions that belong to
 * the firmware's own store are defined below to do the preview's equivalent,
 * or nothing where there is nothing to show.
 *
 * SPDX-License-Identifier: MIT
 */

#include <emscripten/emscripten.h>
#include <string.h>

#include <zephyr/drivers/display.h>

#include "action_button.h"
#include "battery_status.h"
#include "bomber_core.h"
#include "bomber_render.h"
#include "frames.h"
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"
#include "layer_status.h"
#include "logo.h"
#include "modifier.h"
#include "output_status.h"
#include "pacman_core.h"
#include "pacman_render.h"
#include "pacman.h"
#include "shooter_core.h"
#include "shooter_render.h"
#include "sound.h"
#include "splash.h"
#include "theme.h"
#include "wpm.h"

#define PANEL PM_PANEL

static uint16_t fb[PANEL][PANEL];
static pm_game game;
static ss_game shooter;
static bb_game bomber;
/* which of them the game screen is showing, as the `game` setting says */
static int playing;
static uint32_t values[PACMAN_SETTING_COUNT];
static int screen; /* 0 game, 1 splash, 2 dashboard */
static int built;

const struct device sim_display_dev = {.name = "preview"};

/* what the game renderer calls; the firmware sends this down SPI instead */
void pm_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
            uint16_t dx = x + i, dy = y + j;
            if (dx >= PANEL || dy >= PANEL) continue;
            const uint8_t *p = pixels + 2 * (j * w + i);
            fb[dy][dx] = (uint16_t)((p[0] << 8) | p[1]);
        }
    }
}

/* and what the UI helpers call, which carries a pitch the game's blit does not */
void display_write(const struct device *dev, uint16_t x, uint16_t y,
                   const struct display_buffer_descriptor *desc, const void *buf) {
    (void)dev;
    const uint8_t *px = buf;
    for (uint16_t j = 0; j < desc->height; j++) {
        for (uint16_t i = 0; i < desc->width; i++) {
            uint16_t dx = x + i, dy = y + j;
            if (dx >= PANEL || dy >= PANEL) continue;
            const uint8_t *p = px + 2 * (j * desc->pitch + i);
            fb[dy][dx] = (uint16_t)((p[0] << 8) | p[1]);
        }
    }
}

/* the firmware's pacman.c does exactly this, from the same values */
static void reload_game_palette(void) {
    pm_palette p;
    pm_render_default_palette(&p);

    p.bg = pm_rgb565(values[PACMAN_SETTING_GAME_BG]);
    p.wall_fill = pm_rgb565(values[PACMAN_SETTING_GAME_WALL_FILL]);
    p.wall_edge = pm_rgb565(values[PACMAN_SETTING_GAME_WALL]);
    p.wall_flash = pm_rgb565(values[PACMAN_SETTING_GAME_WALL_FLASH]);
    p.house_fill = p.wall_fill;
    p.house_edge = pm_rgb565(values[PACMAN_SETTING_GAME_HOUSE]);
    p.door = pm_rgb565(values[PACMAN_SETTING_GAME_DOOR]);
    p.pellet = pm_rgb565(values[PACMAN_SETTING_GAME_PELLET]);
    p.pac = pm_rgb565(values[PACMAN_SETTING_GAME_PAC]);
    p.ghost[0] = pm_rgb565(values[PACMAN_SETTING_GAME_GHOST_0]);
    p.ghost[1] = pm_rgb565(values[PACMAN_SETTING_GAME_GHOST_1]);
    p.ghost[2] = pm_rgb565(values[PACMAN_SETTING_GAME_GHOST_2]);
    p.ghost[3] = pm_rgb565(values[PACMAN_SETTING_GAME_GHOST_3]);
    p.fright_body = pm_rgb565(values[PACMAN_SETTING_GAME_FRIGHT]);

    pm_render_set_palette(&p);

    ss_palette s;
    ss_render_default_palette(&s);

    s.space = pm_rgb565(values[PACMAN_SETTING_GAME_SPACE]);
    s.star = pm_rgb565(values[PACMAN_SETTING_GAME_STAR]);
    s.ship = pm_rgb565(values[PACMAN_SETTING_GAME_SHIP]);
    s.trim = pm_rgb565(values[PACMAN_SETTING_GAME_SHIP_TRIM]);
    s.thruster = pm_rgb565(values[PACMAN_SETTING_GAME_THRUSTER]);
    s.bullet = pm_rgb565(values[PACMAN_SETTING_GAME_BULLET]);
    s.rock = pm_rgb565(values[PACMAN_SETTING_GAME_METEOR]);
    s.rock_edge = pm_rgb565(values[PACMAN_SETTING_GAME_METEOR_EDGE]);
    s.blast = pm_rgb565(values[PACMAN_SETTING_GAME_BLAST]);
    s.power = pm_rgb565(values[PACMAN_SETTING_GAME_POWERUP]);
    s.hud = pm_rgb565(values[PACMAN_SETTING_GAME_HUD]);

    ss_render_set_palette(&s);

    bb_palette b;
    bb_render_default_palette(&b);

    b.floor = pm_rgb565(values[PACMAN_SETTING_GAME_FLOOR]);
    b.solid = pm_rgb565(values[PACMAN_SETTING_GAME_SOLID]);
    b.brick = pm_rgb565(values[PACMAN_SETTING_GAME_BRICK]);
    b.brick_edge = pm_rgb565(values[PACMAN_SETTING_GAME_BRICK_EDGE]);
    b.bomb = pm_rgb565(values[PACMAN_SETTING_GAME_BOMB]);
    b.flame = pm_rgb565(values[PACMAN_SETTING_GAME_FLAME]);
    b.flame_hot = pm_rgb565(values[PACMAN_SETTING_GAME_FLAME_HOT]);
    b.bomber = pm_rgb565(values[PACMAN_SETTING_GAME_BOMBER]);
    b.bomber_trim = pm_rgb565(values[PACMAN_SETTING_GAME_BOMBER_TRIM]);
    b.foe = pm_rgb565(values[PACMAN_SETTING_GAME_FOE]);
    b.foe_eye = pm_rgb565(values[PACMAN_SETTING_GAME_FOE_EYE]);
    b.pickup = pm_rgb565(values[PACMAN_SETTING_GAME_PICKUP]);
    b.door = pm_rgb565(values[PACMAN_SETTING_GAME_EXIT]);
    b.hud = pm_rgb565(values[PACMAN_SETTING_GAME_HUD]);

    bb_render_set_palette(&b);
    game.redraw = true;
    shooter.redraw = true;
    bomber.redraw = true;
}

/*
 * Stand-ins for the apply functions that live in the firmware's settings
 * store.  Anything the panel shows is done for real; anything that only makes
 * a noise or retimes a thread does nothing, because there is nothing here to
 * hear or to time.
 */
static void apply_screen(uint32_t v) { set_default_screen((DefaultScreen)v); }
static void apply_slot_mode(uint32_t v) { set_slot_mode((SlotMode)v); }
static void apply_slot_1(uint32_t v) { set_slot_1((SlotName)v); }
static void apply_slot_2(uint32_t v) { set_slot_2((SlotName)v); }
static void apply_slot_3(uint32_t v) { set_slot_3((SlotName)v); }
static void apply_slot_4(uint32_t v) { set_slot_4((SlotName)v); }
static void apply_slot_5(uint32_t v) { set_slot_5((SlotName)v); }
static void apply_slot_6(uint32_t v) { set_slot_6((SlotName)v); }
static void apply_battery_slots(uint32_t v) { set_battery_slots((uint8_t)v); }
static void apply_rotate(uint32_t v) { set_display_orientation((DisplayOrientation)v); }
static void apply_splash_style(uint32_t v) { set_splash_style((SplashStyle)v); }
static void apply_theme(uint32_t v) { set_theme_number((uint8_t)v); }
static void apply_game_palette(uint32_t v) { (void)v; reload_game_palette(); }

static void apply_game(uint32_t v) {
    playing = (int)v;
    game.redraw = 1;
    shooter.redraw = 1;
    bomber.redraw = 1;
}

static void apply_theme_colors(uint32_t v) {
    (void)v;
    set_custom_theme_colors(values[PACMAN_SETTING_THEME_PRIMARY],
                            values[PACMAN_SETTING_THEME_SECONDARY],
                            values[PACMAN_SETTING_THEME_BG],
                            values[PACMAN_SETTING_THEME_BG_DARKER]);
}

static void apply_splash_multi(uint32_t v) {
    (void)v;
    set_splash_logo_multicolor(
        values[PACMAN_SETTING_SPLASH_MULTI_0], values[PACMAN_SETTING_SPLASH_MULTI_1],
        values[PACMAN_SETTING_SPLASH_MULTI_2], values[PACMAN_SETTING_SPLASH_MULTI_3]);
}

static void apply_mute(uint32_t v) { (void)v; }
static void apply_volume(uint32_t v) { (void)v; }
static void apply_bass_floor(uint32_t v) { (void)v; }
static void apply_frame_interval(uint32_t v) { (void)v; }
static void apply_theme_threshold(uint32_t v) { (void)v; }
static void apply_mute_threshold(uint32_t v) { (void)v; }

#define PREVIEW_ROW(id, nm, knd, lo, hi, lbls, ap, lv, ov, dflt) {nm, ap, ov},

static const struct {
    const char *name;
    void (*apply)(uint32_t);
    int override;
} table[PACMAN_SETTING_COUNT] = {PACMAN_SETTING_LIST(PREVIEW_ROW)};

/* a theme derives the dashboard, so stored colours have to go back on top */
static void reapply_overrides(void) {
    for (int i = 0; i < PACMAN_SETTING_COUNT; i++) {
        if (table[i].override && table[i].apply != NULL) {
            table[i].apply(values[i]);
        }
    }
}

/*
 * The dashboard reaches for the speaker when a half connects, and for the game
 * when the action button swaps screens.  Neither exists here, so they are
 * answered rather than compiled out - keeping the widgets exactly as the
 * firmware builds them is the whole point.
 */
void pacman_sound_init(void) {}
void pacman_sound_quiet(void) {}
void pacman_sound_connected(bool connected) { (void)connected; }
void pacman_sound_set_mute(bool muted) { (void)muted; }
void pacman_sound_set_volume(uint8_t volume) { (void)volume; }
void pacman_sound_set_bass_floor(uint16_t floor_hz) { (void)floor_hz; }

void pacman_start(void) {}
void pacman_stop(void) {}
void pacman_toggle_pause(void) {}
bool pacman_is_paused(void) { return true; }

/*
 * The slot widget draws which profile the dongle is on.  A browser has no
 * flash to read that out of, but the page does know the answer - it asked the
 * dongle - so it is pushed in rather than made up, and the preview shows the
 * number the panel shows.  Stepping between them is the button's job, and
 * there is no button here.
 */
static int profile_slot = 0;

int pacman_profile_current(void) { return profile_slot; }
int pacman_profile_next(void) { return profile_slot; }
int pacman_profile_load(int slot, bool *reboot, pacman_profile_progress_cb progress) {
    (void)slot;
    (void)progress;
    if (reboot) {
        *reboot = false;
    }
    return 0;
}

void pacman_reload_palette(void) { reload_game_palette(); }
void pacman_set_frame_interval(uint32_t ms) { (void)ms; }

/* the widgets size their buffers from the slots, so this happens once */
static void build_once(void) {
    if (built) return;
    built = 1;

    init_display();
    set_color_override_cb(reapply_overrides);

    zmk_widget_splash_init();
    logo_animation_init();
    zmk_widget_output_status_init();
    zmk_widget_peripheral_battery_status_init();
    zmk_widget_layer_init();
    zmk_widget_wpm_init();
    zmk_widget_modifier_init();
    zmk_widget_action_button_init();
    initialize_battery_status();
}

EMSCRIPTEN_KEEPALIVE int preview_panel(void) { return PANEL; }
EMSCRIPTEN_KEEPALIVE uint16_t *preview_framebuffer(void) { return &fb[0][0]; }
EMSCRIPTEN_KEEPALIVE uint16_t preview_rgb565(uint32_t rgb888) { return pm_rgb565(rgb888); }

/* set one setting by the same name the shell takes; 0 if there is no such name */
EMSCRIPTEN_KEEPALIVE int preview_set(const char *name, uint32_t value) {
    for (int i = 0; i < PACMAN_SETTING_COUNT; i++) {
        if (strcmp(name, table[i].name) != 0) continue;
        values[i] = value;
        if (table[i].apply != NULL) table[i].apply(value);
        return 1;
    }
    return 0;
}

/* after a run of preview_set(), so a group is applied once it is all in */
EMSCRIPTEN_KEEPALIVE void preview_apply_all(void) {
    for (int i = 0; i < PACMAN_SETTING_COUNT; i++) {
        if (table[i].apply != NULL) table[i].apply(values[i]);
    }
}

EMSCRIPTEN_KEEPALIVE void preview_set_screen(int which) { screen = which; }

EMSCRIPTEN_KEEPALIVE void preview_set_profile(int slot) { profile_slot = slot; }

/*
 * A fixed seed on purpose: the preview should look the same every time the
 * page is opened, so a colour can be compared against the last one rather
 * than against a different arrangement of ghosts.
 */
EMSCRIPTEN_KEEPALIVE void preview_reset(uint32_t seed) {
    memset(fb, 0, sizeof(fb));
    pm_init(&game, seed ? seed : 1u);
    pm_set_speed(&game, 4, 4);
    ss_init(&shooter, seed ? seed : 1u);
    ss_set_speed(&shooter, 4);
    bb_init(&bomber, seed ? seed : 1u);
    bb_set_speed(&bomber, 4);
    game.redraw = true;
    shooter.redraw = true;
    bomber.redraw = true;
}

/* one tick of whatever the current screen does over time */
EMSCRIPTEN_KEEPALIVE void preview_step(void) {
    if (screen == 0) {
        if (playing == PACMAN_GAME_SHOOTER) {
            ss_step(&shooter);
            ss_render_frame(&shooter);
            return;
        }
        if (playing == PACMAN_GAME_BOMBER) {
            bb_step(&bomber);
            bb_render_frame(&bomber);
            return;
        }
        pm_step(&game);
        pm_render_frame(&game);
        return;
    }
    if (screen == 1) {
        print_splash();
        return;
    }
    /* the dashboard only moves in its header */
    logo_animation_timer(NULL);
}

EMSCRIPTEN_KEEPALIVE void preview_render(void) {
    build_once();

    if (screen == 0) {
        if (playing == PACMAN_GAME_SHOOTER) {
            shooter.redraw = true;
            ss_render_frame(&shooter);
            return;
        }
        if (playing == PACMAN_GAME_BOMBER) {
            bomber.redraw = true;
            bb_render_frame(&bomber);
            return;
        }
        game.redraw = true;
        pm_render_frame(&game);
        return;
    }
    if (screen == 1) {
        reset_splash();
        print_splash();
        return;
    }
    /*
     * The splash keeps its buffers here.  On the dongle they are freed the
     * moment it hands over, because the heap is tight and it never comes
     * back; on a page it comes back every time the screen is switched.
     */
    print_menu();

    /*
     * print_menu() leaves the header to its own LVGL timer, which there is
     * nobody here to run - so wind it on far enough that the lap or the
     * wordmark is drawn rather than showing an empty band.
     */
    for (int i = 0; i < 24; i++) {
        logo_animation_timer(NULL);
    }
}
