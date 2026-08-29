/*
 * Dongle action button - pauses and resumes the animation.
 *
 * The button is optional: it only does anything when the keymap binds
 * &dongle_action_behavior (the same behaviour the snake dongle uses).
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk_dongle_events/dongle_action_event.h>

#include "action_button.h"
#include "pacman.h"

LOG_MODULE_DECLARE(pacman, LOG_LEVEL_INF);

static struct zmk_dongle_actioned dongle_action_get_state(const zmk_event_t *eh) {
    const struct zmk_dongle_actioned *ev = as_zmk_dongle_actioned(eh);
    return (struct zmk_dongle_actioned){
        .pressed = (ev != NULL) ? ev->pressed : false,
        .timestamp = (ev != NULL) ? ev->timestamp : 0,
    };
}

static void dongle_action_update_cb(struct zmk_dongle_actioned state) {
    if (state.timestamp == 0 || state.pressed) {
        return;
    }
    pacman_toggle_pause();
    LOG_INF("pac-man %s", pacman_is_paused() ? "paused" : "running");
}

ZMK_DISPLAY_WIDGET_LISTENER(pacman_action, struct zmk_dongle_actioned, dongle_action_update_cb,
                            dongle_action_get_state)
ZMK_SUBSCRIPTION(pacman_action, zmk_dongle_actioned);

void zmk_widget_action_button_init(void) { pacman_action_init(); }
