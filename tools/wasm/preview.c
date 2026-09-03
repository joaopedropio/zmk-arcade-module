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
#include "cabinet.h"
#include "battery_status.h"
#include "bomber_core.h"
#include "bomber_render.h"
#include "commando_core.h"
#include "commando_render.h"
#include "fighter_core.h"
#include "fighter_render.h"
#include "frames.h"
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"
#include "layer_status.h"
#include "logo.h"
#include "modifier.h"
#include "output_status.h"
#include "frogger_core.h"
#include "frogger_render.h"
#include "kong_core.h"
#include "kong_render.h"
#include "pacman_core.h"
#include "pacman_render.h"
#include "arcade.h"
#include "shooter_core.h"
#include "shooter_render.h"
#include "sound.h"
#include "splash.h"
#include "tempest_core.h"
#include "tempest_render.h"
#include "theme.h"
#include "wpm.h"

#define PANEL ARC_PANEL

static uint16_t fb[PANEL][PANEL];
static pm_game game;
static ss_game shooter;
static bb_game bomber;
static fg_game fighter;
static cm_game commando;
static fr_game crossing;
static dk_game site;
static tp_game well;
/* which of them the game screen is showing, as the `game` setting says */
static int playing;
static uint32_t values[ARCADE_SETTING_COUNT];
static int screen; /* 0 game, 1 splash, 2 dashboard */
static int built;

const struct device sim_display_dev = {.name = "preview"};

/* what the game renderer calls; the firmware sends this down SPI instead */
void arc_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
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

/* the firmware's arcade.c does exactly this, from the same values */
static void reload_game_palette(void) {
    pm_palette p;
    pm_render_default_palette(&p);

    p.bg = arc_rgb565(values[ARCADE_SETTING_GAME_BG]);
    p.wall_fill = arc_rgb565(values[ARCADE_SETTING_GAME_WALL_FILL]);
    p.wall_edge = arc_rgb565(values[ARCADE_SETTING_GAME_WALL]);
    p.wall_flash = arc_rgb565(values[ARCADE_SETTING_GAME_WALL_FLASH]);
    p.house_fill = p.wall_fill;
    p.house_edge = arc_rgb565(values[ARCADE_SETTING_GAME_HOUSE]);
    p.door = arc_rgb565(values[ARCADE_SETTING_GAME_DOOR]);
    p.pellet = arc_rgb565(values[ARCADE_SETTING_GAME_PELLET]);
    p.pac = arc_rgb565(values[ARCADE_SETTING_GAME_PAC]);
    p.ghost[0] = arc_rgb565(values[ARCADE_SETTING_GAME_GHOST_0]);
    p.ghost[1] = arc_rgb565(values[ARCADE_SETTING_GAME_GHOST_1]);
    p.ghost[2] = arc_rgb565(values[ARCADE_SETTING_GAME_GHOST_2]);
    p.ghost[3] = arc_rgb565(values[ARCADE_SETTING_GAME_GHOST_3]);
    p.fright_body = arc_rgb565(values[ARCADE_SETTING_GAME_FRIGHT]);

    pm_render_set_palette(&p);

    ss_palette s;
    ss_render_default_palette(&s);

    s.space = arc_rgb565(values[ARCADE_SETTING_GAME_SPACE]);
    s.star = arc_rgb565(values[ARCADE_SETTING_GAME_STAR]);
    s.ship = arc_rgb565(values[ARCADE_SETTING_GAME_SHIP]);
    s.trim = arc_rgb565(values[ARCADE_SETTING_GAME_SHIP_TRIM]);
    s.thruster = arc_rgb565(values[ARCADE_SETTING_GAME_THRUSTER]);
    s.bullet = arc_rgb565(values[ARCADE_SETTING_GAME_BULLET]);
    s.rock = arc_rgb565(values[ARCADE_SETTING_GAME_METEOR]);
    s.rock_edge = arc_rgb565(values[ARCADE_SETTING_GAME_METEOR_EDGE]);
    s.blast = arc_rgb565(values[ARCADE_SETTING_GAME_BLAST]);
    s.power = arc_rgb565(values[ARCADE_SETTING_GAME_POWERUP]);
    s.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    ss_render_set_palette(&s);

    bb_palette b;
    bb_render_default_palette(&b);

    b.floor = arc_rgb565(values[ARCADE_SETTING_GAME_FLOOR]);
    b.solid = arc_rgb565(values[ARCADE_SETTING_GAME_SOLID]);
    b.brick = arc_rgb565(values[ARCADE_SETTING_GAME_BRICK]);
    b.brick_edge = arc_rgb565(values[ARCADE_SETTING_GAME_BRICK_EDGE]);
    b.bomb = arc_rgb565(values[ARCADE_SETTING_GAME_BOMB]);
    b.flame = arc_rgb565(values[ARCADE_SETTING_GAME_FLAME]);
    b.flame_hot = arc_rgb565(values[ARCADE_SETTING_GAME_FLAME_HOT]);
    b.bomber = arc_rgb565(values[ARCADE_SETTING_GAME_BOMBER]);
    b.bomber_trim = arc_rgb565(values[ARCADE_SETTING_GAME_BOMBER_TRIM]);
    b.foe = arc_rgb565(values[ARCADE_SETTING_GAME_FOE]);
    b.foe_eye = arc_rgb565(values[ARCADE_SETTING_GAME_FOE_EYE]);
    b.pickup = arc_rgb565(values[ARCADE_SETTING_GAME_PICKUP]);
    b.door = arc_rgb565(values[ARCADE_SETTING_GAME_EXIT]);
    b.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    bb_render_set_palette(&b);

    fg_palette f;
    fg_render_default_palette(&f);

    f.sky = arc_rgb565(values[ARCADE_SETTING_GAME_RING]);
    f.crowd = arc_rgb565(values[ARCADE_SETTING_GAME_RING_CROWD]);
    f.floor = arc_rgb565(values[ARCADE_SETTING_GAME_RING_FLOOR]);
    f.body[0] = arc_rgb565(values[ARCADE_SETTING_GAME_FIGHTER_0]);
    f.trim[0] = arc_rgb565(values[ARCADE_SETTING_GAME_FIGHTER_0_TRIM]);
    f.body[1] = arc_rgb565(values[ARCADE_SETTING_GAME_FIGHTER_1]);
    f.trim[1] = arc_rgb565(values[ARCADE_SETTING_GAME_FIGHTER_1_TRIM]);
    f.spark = arc_rgb565(values[ARCADE_SETTING_GAME_SPARK]);
    f.fireball = arc_rgb565(values[ARCADE_SETTING_GAME_FIREBALL]);
    f.health = arc_rgb565(values[ARCADE_SETTING_GAME_HEALTH]);
    f.health_lost = arc_rgb565(values[ARCADE_SETTING_GAME_HEALTH_LOST]);
    f.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    fg_render_set_palette(&f);

    cm_palette c;
    cm_render_default_palette(&c);

    c.sky = arc_rgb565(values[ARCADE_SETTING_GAME_SKY]);
    c.hill = arc_rgb565(values[ARCADE_SETTING_GAME_HILL]);
    c.ground = arc_rgb565(values[ARCADE_SETTING_GAME_GROUND]);
    c.edge = arc_rgb565(values[ARCADE_SETTING_GAME_GROUND_EDGE]);
    c.hero = arc_rgb565(values[ARCADE_SETTING_GAME_HERO]);
    c.hero_trim = arc_rgb565(values[ARCADE_SETTING_GAME_HERO_TRIM]);
    c.grunt = arc_rgb565(values[ARCADE_SETTING_GAME_GRUNT]);
    c.grunt_trim = arc_rgb565(values[ARCADE_SETTING_GAME_GRUNT_TRIM]);
    c.shot = arc_rgb565(values[ARCADE_SETTING_GAME_SHOT]);
    c.grenade = arc_rgb565(values[ARCADE_SETTING_GAME_GRENADE]);
    c.boom = arc_rgb565(values[ARCADE_SETTING_GAME_BOOM]);
    c.crate = arc_rgb565(values[ARCADE_SETTING_GAME_CRATE]);
    c.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    cm_render_set_palette(&c);

    fr_palette r;
    fr_render_default_palette(&r);

    r.water = arc_rgb565(values[ARCADE_SETTING_GAME_WATER]);
    r.road = arc_rgb565(values[ARCADE_SETTING_GAME_ROAD]);
    r.bank = arc_rgb565(values[ARCADE_SETTING_GAME_BANK]);
    r.hedge = arc_rgb565(values[ARCADE_SETTING_GAME_HEDGE]);
    r.frog = arc_rgb565(values[ARCADE_SETTING_GAME_FROG]);
    r.frog_eye = arc_rgb565(values[ARCADE_SETTING_GAME_FROG_EYE]);
    r.log = arc_rgb565(values[ARCADE_SETTING_GAME_LOG]);
    r.turtle = arc_rgb565(values[ARCADE_SETTING_GAME_TURTLE]);
    r.car = arc_rgb565(values[ARCADE_SETTING_GAME_CAR]);
    r.truck = arc_rgb565(values[ARCADE_SETTING_GAME_TRUCK]);
    r.splat = arc_rgb565(values[ARCADE_SETTING_GAME_SPLAT]);
    r.fly = arc_rgb565(values[ARCADE_SETTING_GAME_FLY]);
    r.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    fr_render_set_palette(&r);

    dk_palette k;
    dk_render_default_palette(&k);

    k.site = arc_rgb565(values[ARCADE_SETTING_GAME_SITE]);
    k.girder = arc_rgb565(values[ARCADE_SETTING_GAME_GIRDER]);
    k.ladder = arc_rgb565(values[ARCADE_SETTING_GAME_LADDER]);
    k.climber = arc_rgb565(values[ARCADE_SETTING_GAME_CLIMBER]);
    k.climber_trim = arc_rgb565(values[ARCADE_SETTING_GAME_CLIMBER_TRIM]);
    k.barrel = arc_rgb565(values[ARCADE_SETTING_GAME_BARREL]);
    k.ape = arc_rgb565(values[ARCADE_SETTING_GAME_APE]);
    k.ape_trim = arc_rgb565(values[ARCADE_SETTING_GAME_APE_TRIM]);
    k.lady = arc_rgb565(values[ARCADE_SETTING_GAME_LADY]);
    k.hammer = arc_rgb565(values[ARCADE_SETTING_GAME_HAMMER]);
    k.oil = arc_rgb565(values[ARCADE_SETTING_GAME_DRUM]);
    k.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    dk_render_set_palette(&k);

    tp_palette t;
    tp_render_default_palette(&t);

    t.site = arc_rgb565(values[ARCADE_SETTING_GAME_VOID]);
    t.well = arc_rgb565(values[ARCADE_SETTING_GAME_WELL]);
    t.rim = arc_rgb565(values[ARCADE_SETTING_GAME_RIM]);
    t.claw = arc_rgb565(values[ARCADE_SETTING_GAME_CLAW]);
    t.shot = arc_rgb565(values[ARCADE_SETTING_GAME_CLAW_SHOT]);
    t.flipper = arc_rgb565(values[ARCADE_SETTING_GAME_FLIPPER]);
    t.tanker = arc_rgb565(values[ARCADE_SETTING_GAME_TANKER]);
    t.spiker = arc_rgb565(values[ARCADE_SETTING_GAME_SPIKER]);
    t.pulsar = arc_rgb565(values[ARCADE_SETTING_GAME_PULSAR]);
    t.spike = arc_rgb565(values[ARCADE_SETTING_GAME_SPIKE]);
    t.bolt = arc_rgb565(values[ARCADE_SETTING_GAME_BOLT]);
    t.hud = arc_rgb565(values[ARCADE_SETTING_GAME_HUD]);

    tp_render_set_palette(&t);
    game.redraw = true;
    shooter.redraw = true;
    bomber.redraw = true;
    fighter.redraw = true;
    commando.redraw = true;
    crossing.redraw = true;
    site.redraw = true;
    well.redraw = true;
}

/*
 * Stand-ins for the apply functions that live in the firmware's settings
 * store.  Anything the panel shows is done for real; anything that only makes
 * a noise or retimes a thread does nothing, because there is nothing here to
 * hear or to time.
 */
static void apply_screen(uint32_t v) { set_default_screen((DefaultScreen)v); }
static void apply_slot_mode(uint32_t v) { set_slot_mode((SlotMode)v); }
static void apply_dashboard_style(uint32_t v) { set_dashboard_style((DashboardStyle)v); }
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
    fighter.redraw = 1;
    commando.redraw = 1;
    crossing.redraw = 1;
    site.redraw = 1;
    well.redraw = 1;
}

static void apply_theme_colors(uint32_t v) {
    (void)v;
    set_custom_theme_colors(values[ARCADE_SETTING_THEME_PRIMARY],
                            values[ARCADE_SETTING_THEME_SECONDARY],
                            values[ARCADE_SETTING_THEME_BG],
                            values[ARCADE_SETTING_THEME_BG_DARKER]);
}

static void apply_splash_multi(uint32_t v) {
    (void)v;
    set_splash_logo_multicolor(
        values[ARCADE_SETTING_SPLASH_MULTI_0], values[ARCADE_SETTING_SPLASH_MULTI_1],
        values[ARCADE_SETTING_SPLASH_MULTI_2], values[ARCADE_SETTING_SPLASH_MULTI_3]);
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
} table[ARCADE_SETTING_COUNT] = {ARCADE_SETTING_LIST(PREVIEW_ROW)};

/* a theme derives the dashboard, so stored colours have to go back on top */
static void reapply_overrides(void) {
    for (int i = 0; i < ARCADE_SETTING_COUNT; i++) {
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
void arcade_sound_init(void) {}
void arcade_sound_quiet(void) {}
void arcade_sound_connected(bool connected) { (void)connected; }
void arcade_sound_set_mute(bool muted) { (void)muted; }
void arcade_sound_set_volume(uint8_t volume) { (void)volume; }
void arcade_sound_set_bass_floor(uint16_t floor_hz) { (void)floor_hz; }

void arcade_start(void) {}
void arcade_stop(void) {}
void arcade_toggle_pause(void) {}
bool arcade_is_paused(void) { return true; }

/*
 * The slot widget draws which profile the dongle is on.  A browser has no
 * flash to read that out of, but the page does know the answer - it asked the
 * dongle - so it is pushed in rather than made up, and the preview shows the
 * number the panel shows.  Stepping between them is the button's job, and
 * there is no button here.
 */
static int profile_slot = 0;

int arcade_profile_current(void) { return profile_slot; }
int arcade_profile_next(void) { return profile_slot; }
int arcade_profile_load(int slot, bool *reboot, arcade_profile_progress_cb progress) {
    (void)slot;
    (void)progress;
    if (reboot) {
        *reboot = false;
    }
    return 0;
}

void arcade_reload_palette(void) { reload_game_palette(); }
void arcade_set_frame_interval(uint32_t ms) { (void)ms; }

/* the widgets size their buffers from the slots, so this happens once */
static void build_once(void) {
    if (built) return;
    built = 1;

    init_display();
    set_color_override_cb(reapply_overrides);

    zmk_widget_splash_init();
    logo_animation_init();
    cabinet_init();
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
EMSCRIPTEN_KEEPALIVE uint16_t preview_rgb565(uint32_t rgb888) { return arc_rgb565(rgb888); }

/* set one setting by the same name the shell takes; 0 if there is no such name */
EMSCRIPTEN_KEEPALIVE int preview_set(const char *name, uint32_t value) {
    for (int i = 0; i < ARCADE_SETTING_COUNT; i++) {
        if (strcmp(name, table[i].name) != 0) continue;
        values[i] = value;
        if (table[i].apply != NULL) table[i].apply(value);
        return 1;
    }
    return 0;
}

/* after a run of preview_set(), so a group is applied once it is all in */
EMSCRIPTEN_KEEPALIVE void preview_apply_all(void) {
    for (int i = 0; i < ARCADE_SETTING_COUNT; i++) {
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
    fg_init(&fighter, seed ? seed : 1u);
    fg_set_speed(&fighter, 4);
    cm_init(&commando, seed ? seed : 1u);
    cm_set_speed(&commando, 4);
    fr_init(&crossing, seed ? seed : 1u);
    fr_set_speed(&crossing, 4);
    dk_init(&site, seed ? seed : 1u);
    dk_set_speed(&site, 4);
    tp_init(&well, seed ? seed : 1u);
    tp_set_speed(&well, 4);
    game.redraw = true;
    shooter.redraw = true;
    bomber.redraw = true;
    fighter.redraw = true;
    commando.redraw = true;
    crossing.redraw = true;
    site.redraw = true;
    well.redraw = true;
}

/* one tick of whatever the current screen does over time */
EMSCRIPTEN_KEEPALIVE void preview_step(void) {
    if (screen == 0) {
        if (playing == ARCADE_GAME_SHOOTER) {
            ss_step(&shooter);
            ss_render_frame(&shooter);
            return;
        }
        if (playing == ARCADE_GAME_BOMBER) {
            bb_step(&bomber);
            bb_render_frame(&bomber);
            return;
        }
        if (playing == ARCADE_GAME_FIGHTER) {
            fg_step(&fighter);
            fg_render_frame(&fighter);
            return;
        }
        if (playing == ARCADE_GAME_COMMANDO) {
            cm_step(&commando);
            cm_render_frame(&commando);
            return;
        }
        if (playing == ARCADE_GAME_FROGGER) {
            fr_step(&crossing);
            fr_render_frame(&crossing);
            return;
        }
        if (playing == ARCADE_GAME_KONG) {
            dk_step(&site);
            dk_render_frame(&site);
            return;
        }
        if (playing == ARCADE_GAME_TEMPEST) {
            tp_step(&well);
            tp_render_frame(&well);
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
        if (playing == ARCADE_GAME_SHOOTER) {
            shooter.redraw = true;
            ss_render_frame(&shooter);
            return;
        }
        if (playing == ARCADE_GAME_BOMBER) {
            bomber.redraw = true;
            bb_render_frame(&bomber);
            return;
        }
        if (playing == ARCADE_GAME_FIGHTER) {
            fighter.redraw = true;
            fg_render_frame(&fighter);
            return;
        }
        if (playing == ARCADE_GAME_COMMANDO) {
            commando.redraw = true;
            cm_render_frame(&commando);
            return;
        }
        if (playing == ARCADE_GAME_FROGGER) {
            crossing.redraw = true;
            fr_render_frame(&crossing);
            return;
        }
        if (playing == ARCADE_GAME_KONG) {
            site.redraw = true;
            dk_render_frame(&site);
            return;
        }
        if (playing == ARCADE_GAME_TEMPEST) {
            well.redraw = true;
            tp_render_frame(&well);
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
