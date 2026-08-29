/*
 * Pac-Man dongle - Kconfig into runtime settings.
 *
 * Everything the UI reads from Kconfig is turned into a runtime value here,
 * once, before any widget is built: which slot holds what, how the panel is
 * mounted, the custom theme and how long a hold has to be to count.  Doing it
 * in one place is what lets the widgets themselves be plain drawing code.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pacman_configuration, LOG_LEVEL_INF);

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "action_button.h"
#include "configuration.h"
#include "helpers/display.h"

static SlotName get_slot_name_from_var(const char *slot_name) {
    if (strcmp(slot_name, "battery") == 0) {
        return SLOT_NAME_BATTERY;
    }
    if (strcmp(slot_name, "modifiers") == 0) {
        return SLOT_NAME_MODIFIERS;
    }
    if (strcmp(slot_name, "connectivity") == 0) {
        return SLOT_NAME_CONNECTIVITY;
    }
    if (strcmp(slot_name, "layer") == 0) {
        return SLOT_NAME_LAYER;
    }
    if (strcmp(slot_name, "theme") == 0) {
        return SLOT_NAME_THEME;
    }
    if (strcmp(slot_name, "wpm") == 0) {
        return SLOT_NAME_WPM;
    }
    return SLOT_NAME_NONE;
}

static SlotMode get_slot_mode_from_var(const char *slot_mode) {
    if (strcmp(slot_mode, "2-slot") == 0) {
        return SLOT_MODE_2;
    }
    if (strcmp(slot_mode, "4-slot") == 0) {
        return SLOT_MODE_4;
    }
    if (strcmp(slot_mode, "5-slot") == 0) {
        return SLOT_MODE_5;
    }
    if (strcmp(slot_mode, "6-slot") == 0) {
        return SLOT_MODE_6;
    }
    return SLOT_MODE_2;
}

static void reset_slots(void) {
    set_slot_1(SLOT_NAME_NONE);
    set_slot_2(SLOT_NAME_NONE);
    set_slot_3(SLOT_NAME_NONE);
    set_slot_4(SLOT_NAME_NONE);
    set_slot_5(SLOT_NAME_NONE);
    set_slot_6(SLOT_NAME_NONE);
}

static void info_slots(void) {
    reset_slots();
    set_slot_mode(get_slot_mode_from_var(CONFIG_PACMAN_INFO_SLOT_MODE));
    set_slot_1(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_1));
    set_slot_2(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_2));
    set_slot_3(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_3));
    set_slot_4(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_4));
    set_slot_5(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_5));
    set_slot_6(get_slot_name_from_var(CONFIG_PACMAN_INFO_SLOT_6));
}

static void default_screen(void) {
    if (strcmp(CONFIG_PACMAN_DEFAULT_SCREEN, "status") == 0) {
        set_default_screen(STATUS_SCREEN);
        return;
    }
    set_default_screen(GAME_SCREEN);
}

static void custom_theme(void) {
    uint32_t color1 = hex_string_to_uint(CONFIG_PACMAN_THEME_PRIMARY_COLOR);
    uint32_t color2 = hex_string_to_uint(CONFIG_PACMAN_THEME_SECONDARY_COLOR);
    uint32_t color3 = hex_string_to_uint(CONFIG_PACMAN_THEME_BG_COLOR);
    uint32_t color4 = hex_string_to_uint(CONFIG_PACMAN_THEME_BG_DARKER_COLOR);

    if (color1 == HEX_PARSE_ERROR || color2 == HEX_PARSE_ERROR || color3 == HEX_PARSE_ERROR ||
        color4 == HEX_PARSE_ERROR) {
        /* the arcade's own: pellet peach, maze blue, black on black */
        set_custom_theme_colors(0xffb897u, 0x2121deu, 0x000000u, 0x000000u);
        return;
    }
    set_custom_theme_colors(color1, color2, color3, color4);
}

static void action_button(void) {
    set_theme_threshold(300);
    if (CONFIG_PACMAN_THEME_THRESHOLD <= 0) {
        return;
    }
    set_theme_threshold(CONFIG_PACMAN_THEME_THRESHOLD);
}

static void rotate_display(void) {
    if (CONFIG_PACMAN_ROTATE_DISPLAY == 270) {
        set_display_orientation(DISPLAY_ORIENTATION_270);
        return;
    }
    if (CONFIG_PACMAN_ROTATE_DISPLAY == 180) {
        set_display_orientation(DISPLAY_ORIENTATION_180);
        return;
    }
    if (CONFIG_PACMAN_ROTATE_DISPLAY == 90) {
        set_display_orientation(DISPLAY_ORIENTATION_90);
        return;
    }
    set_display_orientation(DISPLAY_ORIENTATION_0);
}

static void battery_slots(void) {
    if (CONFIG_PACMAN_BATTERY_SLOTS == 1) {
        set_battery_slots(1);
        return;
    }
    if (CONFIG_PACMAN_BATTERY_SLOTS == 3) {
        set_battery_slots(3);
        return;
    }
    set_battery_slots(2);
}

void configure(void) {
    battery_slots();
    rotate_display();
    info_slots();
    custom_theme();
    default_screen();
    action_button();
}
