/*
 * Pac-Man dongle - the action button, and the dashboard it opens.
 *
 * One key on the keymap (bound to &dongle_action_behavior) drives the whole
 * UI, and what it does depends on how long it is held: a tap swaps between
 * the game and the dashboard, a hold past PACMAN_THEME_THRESHOLD moves to the
 * next profile, and a longer one mutes.
 *
 * The dashboard is a grid of slots - what goes in each one is Kconfig, see
 * PACMAN_INFO_SLOT_* - drawn straight to the panel by the widgets themselves.
 * They only draw while the menu is up, which is what the start_/stop_ pairs
 * are for: the game owns the whole panel the rest of the time.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/workqueue.h>
#include <zmk_dongle_events/dongle_action_event.h>

#include "action_button.h"
#include "battery_status.h"
#include "frames.h"
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"
#include "layer_status.h"
#include "logo.h"
#include "modifier.h"
#include "output_status.h"
#include "pacman.h"
#include "progress.h"
#include "sound.h"
#include "theme.h"
#include "wpm.h"

static uint8_t *buf_frame;
static uint16_t menu_threshold = 0;
static uint16_t theme_threshold = 300;
static uint16_t mute_threshold = 600;
static int64_t pressed_timestamp = 0;

static bool action_button_initialized = false;
static bool menu_on = false;
static bool dongle_lock = false;

/*
 * Whether a profile switch is in flight.  The button is answered on the
 * display queue and the switch runs on another thread, so a second press
 * arrives while the first is still writing flash - and would start a switch
 * out of a profile that is only half applied.  It is refused rather than
 * queued: the panel is showing a modal, and the honest answer to a press
 * during one is nothing at all.
 */
static atomic_t switching = ATOMIC_INIT(0);

/* what the two ends of the switch say to each other, all of it read by the
 * display queue and written by the one doing the flash */
static atomic_t progress_done = ATOMIC_INIT(0);
static atomic_t progress_total = ATOMIC_INIT(0);
static atomic_t progress_slot = ATOMIC_INIT(0);

/* which action a press turned out to be, once it was let go of */
typedef enum {
    ACTION_MENU,
    ACTION_PROFILE,
    ACTION_MUTE,
} Action;

static Action action;

void set_theme_threshold(uint16_t term_ms) { theme_threshold = term_ms; }

void set_mute_threshold(uint16_t term_ms) { mute_threshold = term_ms; }

/*
 * Repaint whatever is up, for a setting that changed underneath it.  Only ever
 * from the display queue, because it draws - and only once the splash has
 * handed the panel over, which is what start_action_button() marks.
 */
void refresh_screen(void) {
    if (!action_button_initialized) {
        return;
    }
    if (menu_on) {
        print_menu();
        return;
    }
    pacman_start(); /* the maze is repainted from the top on the next frame */
}

void print_menu(void) {
    clear_screen(get_menu_bg_color());
    start_animation();
    print_frames(buf_frame);
    start_battery_status();
    start_output_status();
    start_wpm_status();
    start_modifier_status();
    start_layer_status();
    set_status_symbol();
    set_battery_symbol();
    print_battery_widget();
    print_layer();
    print_themes();
    print_wpm();
    print_modifiers();
}

static void toggle_menu(void) {
    if (menu_on) {
        stop_wpm_status();
        stop_modifier_status();
        stop_output_status();
        stop_battery_status();
        stop_animation();
        stop_layer_status();
        pacman_start();
        menu_on = false;
        return;
    }
    pacman_stop();
    print_menu();
    menu_on = true;
}

/*
 * The three things a profile switch does on the display queue, because they
 * draw: put the modal up, move its bar along, and put the screen back.  The
 * game is stopped for the first of them - its timer runs on this same thread
 * and would paint the maze straight over the box - and started again by
 * refresh_screen() at the end.
 */
static void open_progress(struct k_work *work) {
    ARG_UNUSED(work);

    pacman_stop();
    progress_open((uint8_t)atomic_get(&progress_slot));
}

static K_WORK_DEFINE(open_progress_work, open_progress);

static void draw_progress(struct k_work *work) {
    ARG_UNUSED(work);

    progress_draw((uint16_t)atomic_get(&progress_done), (uint16_t)atomic_get(&progress_total));
}

static K_WORK_DEFINE(draw_progress_work, draw_progress);

/*
 * apply_all() pushes the new values at whatever draws them and the screen goes
 * up again.  Re-applying everything rather than what moved is the same trade
 * the shell makes - the setters are idempotent, and eighty of them cost less
 * than the repaint after.  The flag is cleared here, at the end of the whole
 * thing, so the button stays refused until there is a screen to press against.
 */
static void show_profile(struct k_work *work) {
    ARG_UNUSED(work);

    pacman_settings_apply_all();
    refresh_screen();
    atomic_clear(&switching);
}

static K_WORK_DEFINE(show_profile_work, show_profile);

/*
 * Called between flash writes, on the thread doing them.  It leaves a number
 * behind and asks the display queue to draw it rather than drawing anything
 * itself; an update that arrives while the last one is still queued is simply
 * the number the queued one will read.
 */
static void on_progress(uint16_t done, uint16_t total) {
    atomic_set(&progress_done, done);
    atomic_set(&progress_total, total);
    k_work_submit_to_queue(zmk_display_work_q(), &draw_progress_work);
}

/*
 * The rest of a switch is a flash write for the profile being left and one for
 * every setting that moved, which is far too much to do on the queue that has
 * to repaint next - the button is answered from a display widget listener, so
 * that is the thread this would otherwise run on.
 *
 * It goes to ZMK's low-priority queue rather than the system one, which is
 * cooperative at -1 and so sits above the sound thread: a burst of flash
 * writes there would hold the amplifier off between yields.  At 10 it is below
 * both the sound thread and the display, which is the order this shield needs.
 * The drawing is handed back a piece at a time as it goes.
 */
static void switch_profile(struct k_work *work) {
    ARG_UNUSED(work);

    int slot = pacman_profile_next();
    if (slot == pacman_profile_current()) {
        atomic_clear(&switching); /* the only profile there is; nothing to move to */
        return;
    }

    atomic_set(&progress_slot, slot);
    atomic_set(&progress_done, 0);
    atomic_set(&progress_total, 1);
    k_work_submit_to_queue(zmk_display_work_q(), &open_progress_work);

    /*
     * The screen has to come back whether the load worked or not - the modal
     * is already up by now, and a dongle left staring at a half-full bar is
     * worse than one that simply did not move.
     */
    pacman_profile_load(slot, NULL, on_progress);
    k_work_submit_to_queue(zmk_display_work_q(), &show_profile_work);
}

static K_WORK_DEFINE(switch_profile_work, switch_profile);

static void next_profile(void) {
    if (!atomic_cas(&switching, 0, 1)) {
        return; /* one is already running; the modal is on the panel saying so */
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &switch_profile_work);
}

static void toggle_mute(void) { pacman_settings_toggle_mute(); }

static void run_action(void) {
    if (dongle_lock) {
        return;
    }
    /*
     * A switch owns the panel until its bar is full - swapping screens or
     * muting under the modal would draw over it, and pressing again would
     * start a second switch out of a half-applied profile.
     */
    if (atomic_get(&switching)) {
        return;
    }
    dongle_lock = true;
    if (action == ACTION_MENU) {
        toggle_menu();
    }
    if (action == ACTION_PROFILE) {
        next_profile();
    }
    if (action == ACTION_MUTE) {
        toggle_mute();
    }
    dongle_lock = false;
}

static void dongle_action_update_cb(struct zmk_dongle_actioned state) {
    if (state.timestamp == 0) {
        return;
    }
    if (state.pressed) {
        pressed_timestamp = state.timestamp;
        return;
    }

    int64_t elapsed_time = state.timestamp - pressed_timestamp;
    if (elapsed_time > mute_threshold) {
        action = ACTION_MUTE;
    } else if (elapsed_time > theme_threshold) {
        action = ACTION_PROFILE;
    } else if (elapsed_time > menu_threshold) {
        action = ACTION_MENU;
    }
    if (action_button_initialized) {
        run_action();
    }
    pressed_timestamp = 0;
}

static struct zmk_dongle_actioned dongle_action_get_state(const zmk_event_t *eh) {
    const struct zmk_dongle_actioned *ev = as_zmk_dongle_actioned(eh);

    return (struct zmk_dongle_actioned){
        .pressed = (ev != NULL) ? ev->pressed : false,
        .timestamp = (ev != NULL) ? ev->timestamp : 0,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(dongle_action, struct zmk_dongle_actioned, dongle_action_update_cb,
                            dongle_action_get_state)
ZMK_SUBSCRIPTION(dongle_action, zmk_dongle_actioned);

void zmk_widget_action_button_init(void) {
    dongle_action_init();

    buf_frame = (uint8_t *)k_malloc(320 * 2 * sizeof(uint8_t));
    progress_init();
}

void start_action_button(bool is_menu_on) {
    menu_on = is_menu_on;
    action_button_initialized = true;
}
