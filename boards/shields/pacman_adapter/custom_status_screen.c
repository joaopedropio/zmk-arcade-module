/*
 * Pac-Man dongle - status screen.
 *
 * ZMK expects an LVGL screen here.  We hand it an empty one and paint the
 * game straight to the panel from an LVGL timer, which skips the widget
 * tree (and its memory) entirely.  The first frame is delayed a moment so
 * LVGL's initial flush of that empty screen cannot wipe the maze.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <zmk/display.h>

#include "custom_status_screen.h"
#include "widgets/action_button.h"
#include "widgets/pacman.h"

LOG_MODULE_DECLARE(pacman, LOG_LEVEL_INF);

static void start_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    pacman_start();
}

lv_obj_t *zmk_display_status_screen(void) {
    zmk_widget_pacman_init();
    zmk_widget_action_button_init();

    lv_timer_t *start = lv_timer_create(start_cb, CONFIG_PACMAN_START_DELAY, NULL);
    lv_timer_set_repeat_count(start, 1);

    return lv_obj_create(NULL);
}
