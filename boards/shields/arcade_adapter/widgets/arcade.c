/*
 * Arcade dongle widget - ZMK/LVGL glue for the games.
 *
 * The games themselves live in widgets/game/ and know nothing about Zephyr.
 * This file owns the display device, turns the stored colours into their
 * palettes, ticks whichever one is chosen from an LVGL timer and (optionally)
 * speeds it up while you type.  The colours, the tick and the typing
 * thresholds all come from helpers/settings.h rather than straight from
 * Kconfig, so the shell can change them without a rebuild.
 *
 * All of them are always built and all of them keep their state, so switching
 * between them is a repaint rather than a restart: come back to the maze and
 * it is where you left it.  What that costs is the structs; what it saves is
 * having to decide, at build time, which one somebody will want.
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
#include "arcade.h"
#include "sound.h"
#include "game/bomber_render.h"
#include "game/commando_render.h"
#include "game/fighter_render.h"
#include "game/pacman_render.h"
#include "game/frogger_render.h"
#include "game/kong_render.h"
#include "game/shooter_render.h"
#include "game/tempest_render.h"

LOG_MODULE_REGISTER(arcade, LOG_LEVEL_INF);

static const struct device *display_dev;
static pm_game game;
static ss_game shooter;
static bb_game bomber;
static fg_game fighter;
static cm_game commando;
static fr_game crossing;
static dk_game site;
static tp_game well;
static uint8_t playing = ARCADE_GAME_PACMAN;
static bool running;
static bool paused;
static uint32_t frames;

/* the panel can be mounted any way up; rotating costs a scratch buffer and
 * a pixel copy, so it is compiled in only when it is actually asked for */
#define ARCADE_ROTATION CONFIG_ARCADE_ROTATE_DISPLAY

#if ARCADE_ROTATION == 90 || ARCADE_ROTATION == 180 || ARCADE_ROTATION == 270
#define ARCADE_ROTATED 1
/* whatever the widest blit any renderer stages, which is panel.h's band */
static uint8_t rot_buf[ARC_BAND_PX * 2];
#endif

/* the panel keeps its contents while ZMK blanks it, but repaint everything
 * once in a while so a stray redraw can never leave the screen half drawn */
#define ARCADE_FULL_REDRAW_FRAMES 1800

/*
 * Every game is told to repaint, never only the one that is running: each
 * renderer remembers what it last put on the panel, and the ones that are idle
 * have had the others drawing over them ever since.
 */
static void repaint_all(void) {
    game.redraw = true;
    shooter.redraw = true;
    bomber.redraw = true;
    fighter.redraw = true;
    commando.redraw = true;
    crossing.redraw = true;
    site.redraw = true;
    well.redraw = true;
}

/* ------------------------------------------------------------------ */
/* blitting                                                            */
/* ------------------------------------------------------------------ */

void arc_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t *pixels) {
    if (!display_dev) {
        return;
    }

    struct display_buffer_descriptor desc;
    uint16_t dx = x, dy = y, dw = w, dh = h;

#ifdef ARCADE_ROTATED
#if ARCADE_ROTATION != 180
    dw = h;
    dh = w;
#endif
    for (uint16_t j = 0; j < h; j++) {
        for (uint16_t i = 0; i < w; i++) {
#if ARCADE_ROTATION == 90
            uint16_t ti = (uint16_t)(h - 1 - j), tj = i;
#elif ARCADE_ROTATION == 270
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
#if ARCADE_ROTATION == 90
    dx = (uint16_t)(ARC_PANEL - y - h);
    dy = x;
#elif ARCADE_ROTATION == 270
    dx = y;
    dy = (uint16_t)(ARC_PANEL - x - w);
#else
    dx = (uint16_t)(ARC_PANEL - x - w);
    dy = (uint16_t)(ARC_PANEL - y - h);
#endif
    pixels = rot_buf;
#endif /* ARCADE_ROTATED */

    desc.buf_size = (uint32_t)dw * dh * 2u;
    desc.pitch = dw;
    desc.width = dw;
    desc.height = dh;
    display_write(display_dev, dx, dy, &desc, pixels);
}

/* ------------------------------------------------------------------ */
/* colours                                                             */
/*
 * Every palette is rebuilt rather than patched: each colour is one settings
 * entry, and reading all of them back costs less than tracking which one moved
 * - and less than working out which game it belonged to.  Called again
 * whenever the shell changes any of them.
 */
void arcade_reload_palette(void) {
    pm_palette p;
    pm_render_default_palette(&p);

    p.bg = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BG));
    p.wall_fill = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_WALL_FILL));
    p.wall_edge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_WALL));
    p.wall_flash = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_WALL_FLASH));
    p.house_fill = p.wall_fill;
    p.house_edge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HOUSE));
    p.door = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_DOOR));
    p.pellet = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_PELLET));
    p.pac = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_PAC));
    p.ghost[0] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GHOST_0));
    p.ghost[1] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GHOST_1));
    p.ghost[2] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GHOST_2));
    p.ghost[3] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GHOST_3));
    p.fright_body = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FRIGHT));

    pm_render_set_palette(&p);

    ss_palette s;
    ss_render_default_palette(&s);

    s.space = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SPACE));
    s.star = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_STAR));
    s.ship = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SHIP));
    s.trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SHIP_TRIM));
    s.thruster = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_THRUSTER));
    s.bullet = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BULLET));
    s.rock = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_METEOR));
    s.rock_edge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_METEOR_EDGE));
    s.blast = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BLAST));
    s.power = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_POWERUP));
    s.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    ss_render_set_palette(&s);

    bb_palette b;
    bb_render_default_palette(&b);

    b.floor = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FLOOR));
    b.solid = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SOLID));
    b.brick = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BRICK));
    b.brick_edge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BRICK_EDGE));
    b.bomb = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BOMB));
    b.flame = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FLAME));
    b.flame_hot = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FLAME_HOT));
    b.bomber = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BOMBER));
    b.bomber_trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BOMBER_TRIM));
    b.foe = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FOE));
    b.foe_eye = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FOE_EYE));
    b.pickup = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_PICKUP));
    b.door = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_EXIT));
    /* the readout is the same job on either panel, so it is the same colour */
    b.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    bb_render_set_palette(&b);

    fg_palette f;
    fg_render_default_palette(&f);

    f.sky = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_RING));
    f.crowd = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_RING_CROWD));
    f.floor = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_RING_FLOOR));
    f.body[0] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FIGHTER_0));
    f.trim[0] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FIGHTER_0_TRIM));
    f.body[1] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FIGHTER_1));
    f.trim[1] = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FIGHTER_1_TRIM));
    f.spark = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SPARK));
    f.fireball = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FIREBALL));
    f.health = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HEALTH));
    f.health_lost = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HEALTH_LOST));
    f.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    fg_render_set_palette(&f);

    cm_palette c;
    cm_render_default_palette(&c);

    c.sky = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SKY));
    c.hill = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HILL));
    c.ground = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GROUND));
    c.edge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GROUND_EDGE));
    c.hero = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HERO));
    c.hero_trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HERO_TRIM));
    c.grunt = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GRUNT));
    c.grunt_trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GRUNT_TRIM));
    c.shot = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SHOT));
    c.grenade = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GRENADE));
    c.boom = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BOOM));
    c.crate = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CRATE));
    c.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    cm_render_set_palette(&c);

    fr_palette r;
    fr_render_default_palette(&r);

    r.water = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_WATER));
    r.road = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_ROAD));
    r.bank = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BANK));
    r.hedge = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HEDGE));
    r.frog = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FROG));
    r.frog_eye = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FROG_EYE));
    r.log = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_LOG));
    r.turtle = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_TURTLE));
    r.car = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CAR));
    r.truck = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_TRUCK));
    r.splat = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SPLAT));
    r.fly = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FLY));
    /* the readout is the same job on either panel, so it is the same colour */
    r.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    fr_render_set_palette(&r);

    dk_palette k;
    dk_render_default_palette(&k);

    k.site = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SITE));
    k.girder = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_GIRDER));
    k.ladder = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_LADDER));
    k.climber = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CLIMBER));
    k.climber_trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CLIMBER_TRIM));
    k.barrel = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BARREL));
    k.ape = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_APE));
    k.ape_trim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_APE_TRIM));
    k.lady = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_LADY));
    k.hammer = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HAMMER));
    k.oil = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_DRUM));
    /* the readout is the same job on either panel, so it is the same colour */
    k.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    dk_render_set_palette(&k);

    tp_palette t;
    tp_render_default_palette(&t);

    t.site = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_VOID));
    t.well = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_WELL));
    t.rim = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_RIM));
    t.claw = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CLAW));
    t.shot = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_CLAW_SHOT));
    t.flipper = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_FLIPPER));
    t.tanker = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_TANKER));
    t.spiker = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SPIKER));
    t.pulsar = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_PULSAR));
    t.spike = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_SPIKE));
    t.bolt = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_BOLT));
    /* the readout is the same job on either panel, so it is the same colour */
    t.hud = arc_rgb565(arcade_settings_get(ARCADE_SETTING_GAME_HUD));

    tp_render_set_palette(&t);
    repaint_all();
}

/*
 * Switching games only says which one the timer ticks.  None of them is reset
 * - the maze is still mid-level when it comes back - but all are told to
 * repaint, because whichever takes the panel next inherits another's pixels.
 */
void arcade_set_game(uint8_t which) {
    playing = which <= ARCADE_GAME_TEMPEST ? which : ARCADE_GAME_PACMAN;
    repaint_all();
}

/* ------------------------------------------------------------------ */
/* typing speed                                                        */
/* ------------------------------------------------------------------ */

struct arcade_wpm_state {
    uint8_t wpm;
};

/*
 * Speeds are pixels per frame, so they set how fast the maze crosses the panel
 * rather than how many tiles pass per second.  3 and 4 divide a 24px tile
 * exactly and step evenly; 5 does not, and just spends a short frame arriving
 * at each tile, since advance() never overshoots one.
 */
static void apply_wpm(uint8_t wpm) {
#if IS_ENABLED(CONFIG_ARCADE_WPM_SPEED)
    uint8_t speed = 4;
    if (wpm > arcade_settings_get(ARCADE_SETTING_WPM_FAST)) {
        speed = 5;
    } else if (wpm < arcade_settings_get(ARCADE_SETTING_WPM_SLOW)) {
        speed = 3;
    }
    pm_set_speed(&game, speed, speed);
    ss_set_speed(&shooter, speed);
    bb_set_speed(&bomber, speed);
    fg_set_speed(&fighter, speed);
    cm_set_speed(&commando, speed);
    fr_set_speed(&crossing, speed);
    dk_set_speed(&site, speed);
    tp_set_speed(&well, speed);
#else
    ARG_UNUSED(wpm);
#endif
}

static struct arcade_wpm_state arcade_wpm_get_state(const zmk_event_t *eh) {
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    return (struct arcade_wpm_state){.wpm = (ev != NULL) ? (uint8_t)ev->state : 0};
}

static void arcade_wpm_update_cb(struct arcade_wpm_state state) { apply_wpm(state.wpm); }

ZMK_DISPLAY_WIDGET_LISTENER(arcade_wpm, struct arcade_wpm_state, arcade_wpm_update_cb,
                            arcade_wpm_get_state)
ZMK_SUBSCRIPTION(arcade_wpm, zmk_wpm_state_changed);

/* ------------------------------------------------------------------ */
/* the loop                                                            */
/* ------------------------------------------------------------------ */

static lv_timer_t *frame_timer;

/*
 * Nothing before zmk_widget_arcade_init() has a timer to retime, and
 * configure() runs first - so a period arriving early is stored and picked up
 * when the timer is made.
 */
void arcade_set_frame_interval(uint32_t ms) {
    if (frame_timer == NULL) {
        return;
    }
    lv_timer_set_period(frame_timer, ms);
}

static void arcade_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    if (!running || paused) {
        return;
    }

    if (++frames >= ARCADE_FULL_REDRAW_FRAMES) {
        frames = 0;
        repaint_all();
    }

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
}

void zmk_widget_arcade_init(void) {
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("display device is not ready");
        display_dev = NULL;
        return;
    }

    arcade_sound_init();

    uint32_t seed = (uint32_t)k_uptime_get_32() | 1u;
    pm_init(&game, seed);
    pm_set_speed(&game, 4, 4);
    ss_init(&shooter, seed);
    ss_set_speed(&shooter, 4);
    bb_init(&bomber, seed);
    bb_set_speed(&bomber, 4);
    fg_init(&fighter, seed);
    fg_set_speed(&fighter, 4);
    cm_init(&commando, seed);
    cm_set_speed(&commando, 4);
    fr_init(&crossing, seed);
    fr_set_speed(&crossing, 4);
    dk_init(&site, seed);
    dk_set_speed(&site, 4);
    tp_init(&well, seed);
    tp_set_speed(&well, 4);

    /* after the inits, which would otherwise clear the repaint they ask for */
    arcade_reload_palette();

    arcade_wpm_init();

    frame_timer = lv_timer_create(arcade_timer_cb,
                                  arcade_settings_get(ARCADE_SETTING_FRAME_INTERVAL), NULL);
}

void arcade_start(void) {
    running = true;
    paused = false;
    repaint_all();
}

void arcade_stop(void) {
    running = false;
}

void arcade_toggle_pause(void) {
    paused = !paused;
    if (!paused) {
        repaint_all();
    }
}

bool arcade_is_paused(void) { return paused; }
