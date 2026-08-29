/*
 * Pac-Man dongle - status screen.
 *
 * ZMK expects an LVGL screen here.  We hand it an empty one and paint
 * everything straight to the panel from LVGL timers, which skips the widget
 * tree (and its memory) entirely.
 *
 * Three things share the panel, one at a time.  The splash goes up first and
 * stays for PACMAN_SPLASH_FRAMES ticks - long enough that LVGL's initial
 * flush of the empty screen cannot land on top of the game.  Then either the
 * game or the dashboard takes over, depending on PACMAN_DEFAULT_SCREEN, and
 * from there the action button swaps between them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <zmk/display.h>

#include "custom_status_screen.h"
#include "widgets/action_button.h"
#include "widgets/battery_status.h"
#include "widgets/configuration.h"
#include "widgets/helpers/display.h"
#include "widgets/layer_status.h"
#include "widgets/logo.h"
#include "widgets/modifier.h"
#include "widgets/output_status.h"
#include "widgets/pacman.h"
#include "widgets/splash.h"
#include "widgets/theme.h"
#include "widgets/wpm.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint16_t splash_count = 0;
static bool splash_finished = false;

static void timer_splash(lv_timer_t *timer) {
    if (splash_finished) {
        return;
    }
    if (splash_count >= CONFIG_PACMAN_SPLASH_FRAMES) {
        clean_up_splash();
        initialize_battery_status();

        bool menu_on = get_default_screen() == STATUS_SCREEN;
        if (menu_on) {
            print_menu();
        } else {
            pacman_start();
        }
        start_action_button(menu_on);

        lv_timer_pause(timer);
        splash_finished = true;
        return;
    }
    print_splash();
    splash_count++;
}

lv_obj_t *zmk_display_status_screen(void) {
    configure();
    init_display();
    theme_init();
    logo_animation_init();

    zmk_widget_splash_init();
    zmk_widget_pacman_init();
    zmk_widget_output_status_init();
    zmk_widget_peripheral_battery_status_init();
    zmk_widget_layer_init();
    zmk_widget_action_button_init();
    zmk_widget_wpm_init();
    zmk_widget_modifier_init();

    lv_timer_create(timer_splash, CONFIG_PACMAN_SPLASH_INTERVAL, NULL);
    if (get_slot_mode() == SLOT_MODE_2) {
        lv_timer_create(logo_animation_timer, CONFIG_PACMAN_LOGO_WALK_INTERVAL, NULL);
    }

    return lv_obj_create(NULL);
}
