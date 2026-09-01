/*
 * Pac-Man dongle - every setting there is, once.
 *
 * settings.h turns this into the id enum and settings.c turns it into the
 * table, so a setting is added by adding one line here and nothing else: the
 * line is its flash key, the word the shell takes, the values it accepts, how
 * it reaches the thing that uses it, and what the firmware was built with.
 *
 * Order matters in one place.  apply_all() walks the list top to bottom, so
 * the four custom-theme colours come before the theme that derives the
 * dashboard from them, and every colour that can be overridden comes after it.
 *
 *   id            name         kind   min  max          labels  apply  live  override  built with
 *
 * kind      ENUM takes one of its labels, NUMBER a decimal in range, COLOR an
 *           rrggbb string.
 * apply     how the value reaches whatever draws or sounds it, or NULL where
 *           only a reboot can.
 * live      whether calling apply after boot actually changes anything.  The
 *           slot layout does not: each widget sized and allocated its scratch
 *           bitmap from the slot it was handed at init.  Nor does the splash,
 *           which is over before a shell can be typed at.
 * override  whether to put it back on top of whatever a theme change just
 *           derived - true for every colour a theme would otherwise overwrite.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define PACMAN_SETTING_LIST(X)                                                                     \
    X(SCREEN,                "screen",                ENUM,   0,     STATUS_SCREEN,           screen_labels,     apply_screen,                false, false, CONFIG_PACMAN_DEFAULT_SCREEN)   \
    X(SLOT_MODE,             "slot-mode",             ENUM,   0,     SLOT_MODE_6,             slot_mode_labels,  apply_slot_mode,             false, false, CONFIG_PACMAN_INFO_SLOT_MODE)   \
    X(SLOT_1,                "slot1",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_1,                false, false, CONFIG_PACMAN_INFO_SLOT_1)   \
    X(SLOT_2,                "slot2",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_2,                false, false, CONFIG_PACMAN_INFO_SLOT_2)   \
    X(SLOT_3,                "slot3",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_3,                false, false, CONFIG_PACMAN_INFO_SLOT_3)   \
    X(SLOT_4,                "slot4",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_4,                false, false, CONFIG_PACMAN_INFO_SLOT_4)   \
    X(SLOT_5,                "slot5",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_5,                false, false, CONFIG_PACMAN_INFO_SLOT_5)   \
    X(SLOT_6,                "slot6",                 ENUM,   0,     SLOT_NAME_NONE,          slot_labels,       apply_slot_6,                false, false, CONFIG_PACMAN_INFO_SLOT_6)   \
    X(BATTERY_SLOTS,         "battery-slots",         NUMBER, 1,     3,                       NULL,              apply_battery_slots,         false, false, STRINGIFY(CONFIG_PACMAN_BATTERY_SLOTS))   \
    X(ROTATE,                "rotate",                ENUM,   0,     DISPLAY_ORIENTATION_270, rotate_labels,     apply_rotate,                false, false, STRINGIFY(CONFIG_PACMAN_ROTATE_DISPLAY))   \
    X(MUTE,                  "mute",                  ENUM,   0,     1,                       mute_labels,       apply_mute,                  true,  false, "off")   \
    X(THEME_PRIMARY,         "theme-primary",         COLOR,  0,     0xffffff,                NULL,              apply_theme_colors,          true,  false, CONFIG_PACMAN_THEME_PRIMARY_COLOR)   \
    X(THEME_SECONDARY,       "theme-secondary",       COLOR,  0,     0xffffff,                NULL,              apply_theme_colors,          true,  false, CONFIG_PACMAN_THEME_SECONDARY_COLOR)   \
    X(THEME_BG,              "theme-bg",              COLOR,  0,     0xffffff,                NULL,              apply_theme_colors,          true,  false, CONFIG_PACMAN_THEME_BG_COLOR)   \
    X(THEME_BG_DARKER,       "theme-bg-darker",       COLOR,  0,     0xffffff,                NULL,              apply_theme_colors,          true,  false, CONFIG_PACMAN_THEME_BG_DARKER_COLOR)   \
    X(THEME,                 "theme",                 NUMBER, 0,     0,                       NULL,              apply_theme,                 true,  false, "0")   \
    X(GAME,                  "game",                  ENUM,   0,     PACMAN_GAME_FROGGER,     game_labels,       apply_game,                  true,  false, CONFIG_PACMAN_DEFAULT_GAME)   \
    X(GAME_BG,               "game-bg",               COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_BG_COLOR)   \
    X(GAME_WALL,             "game-wall",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_WALL_COLOR)   \
    X(GAME_WALL_FILL,        "game-wall-fill",        COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_WALL_FILL_COLOR)   \
    X(GAME_WALL_FLASH,       "game-wall-flash",       COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_WALL_FLASH_COLOR)   \
    X(GAME_HOUSE,            "game-house",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_HOUSE_COLOR)   \
    X(GAME_DOOR,             "game-door",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_DOOR_COLOR)   \
    X(GAME_PELLET,           "game-pellet",           COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_PELLET_COLOR)   \
    X(GAME_PAC,              "game-pac",              COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_PACMAN_COLOR)   \
    X(GAME_GHOST_0,          "game-ghost-0",          COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_GHOST_0_COLOR)   \
    X(GAME_GHOST_1,          "game-ghost-1",          COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_GHOST_1_COLOR)   \
    X(GAME_GHOST_2,          "game-ghost-2",          COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_GHOST_2_COLOR)   \
    X(GAME_GHOST_3,          "game-ghost-3",          COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_GHOST_3_COLOR)   \
    X(GAME_FRIGHT,           "game-fright",           COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_FRIGHT_COLOR)   \
    X(GAME_SPACE,            "game-space",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_SPACE_COLOR)   \
    X(GAME_STAR,             "game-star",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_STAR_COLOR)   \
    X(GAME_SHIP,             "game-ship",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_SHIP_COLOR)   \
    X(GAME_SHIP_TRIM,        "game-ship-trim",        COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_SHIP_TRIM_COLOR)   \
    X(GAME_THRUSTER,         "game-thruster",         COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_THRUSTER_COLOR)   \
    X(GAME_BULLET,           "game-bullet",           COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_BULLET_COLOR)   \
    X(GAME_METEOR,           "game-meteor",           COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_METEOR_COLOR)   \
    X(GAME_METEOR_EDGE,      "game-meteor-edge",      COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_METEOR_EDGE_COLOR)   \
    X(GAME_BLAST,            "game-blast",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_BLAST_COLOR)   \
    X(GAME_POWERUP,          "game-powerup",          COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_POWERUP_COLOR)   \
    X(GAME_HUD,              "game-hud",              COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_HUD_COLOR)   \
    X(GAME_WATER,            "game-water",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_WATER_COLOR)   \
    X(GAME_ROAD,             "game-road",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_ROAD_COLOR)   \
    X(GAME_BANK,             "game-bank",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_BANK_COLOR)   \
    X(GAME_HEDGE,            "game-hedge",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_HEDGE_COLOR)   \
    X(GAME_FROG,             "game-frog",             COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_FROG_COLOR)   \
    X(GAME_FROG_EYE,         "game-frog-eye",         COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_FROG_EYE_COLOR)   \
    X(GAME_LOG,              "game-log",              COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_LOG_COLOR)   \
    X(GAME_TURTLE,           "game-turtle",           COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_TURTLE_COLOR)   \
    X(GAME_CAR,              "game-car",              COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_CAR_COLOR)   \
    X(GAME_TRUCK,            "game-truck",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_TRUCK_COLOR)   \
    X(GAME_SPLAT,            "game-splat",            COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_SPLAT_COLOR)   \
    X(GAME_FLY,              "game-fly",              COLOR,  0,     0xffffff,                NULL,              apply_game_palette,          true,  false,  CONFIG_PACMAN_FLY_COLOR)   \
    X(SPLASH_STYLE,          "splash-style",          ENUM,   0,     SPLASH_STYLE_IMAGE,      splash_style_labels, apply_splash_style,        false, false, CONFIG_PACMAN_SPLASH_STYLE)   \
    X(SPLASH_MULTI_0,        "splash-multi-0",        COLOR,  0,     0xffffff,                NULL,              apply_splash_multi,          false, true,  CONFIG_PACMAN_SPLASH_MULTICOLOR_0)   \
    X(SPLASH_MULTI_1,        "splash-multi-1",        COLOR,  0,     0xffffff,                NULL,              apply_splash_multi,          false, true,  CONFIG_PACMAN_SPLASH_MULTICOLOR_1)   \
    X(SPLASH_MULTI_2,        "splash-multi-2",        COLOR,  0,     0xffffff,                NULL,              apply_splash_multi,          false, true,  CONFIG_PACMAN_SPLASH_MULTICOLOR_2)   \
    X(SPLASH_MULTI_3,        "splash-multi-3",        COLOR,  0,     0xffffff,                NULL,              apply_splash_multi,          false, true,  CONFIG_PACMAN_SPLASH_MULTICOLOR_3)   \
    X(SPLASH_LOGO,           "splash-logo",           COLOR,  0,     0xffffff,                NULL,              set_splash_logo_color,       false, true,  CONFIG_PACMAN_SPLASH_LOGO_COLOR)   \
    X(SPLASH_CREATED_BY,     "splash-created-by",     COLOR,  0,     0xffffff,                NULL,              set_splash_created_by_color, false, true,  CONFIG_PACMAN_SPLASH_CREATED_BY_COLOR)   \
    X(SPLASH_BG,             "splash-bg",             COLOR,  0,     0xffffff,                NULL,              set_splash_bg_color,         false, true,  CONFIG_PACMAN_SPLASH_BG_COLOR)   \
    X(BATTERY_WIDGET_NUM,    "battery-widget-num",    COLOR,  0,     0xffffff,                NULL,              set_battery_widget_num_color, true,  true,  CONFIG_PACMAN_BATTERY_WIDGET_NUM_COLOR)   \
    X(BATTERY_WIDGET_BG,     "battery-widget-bg",     COLOR,  0,     0xffffff,                NULL,              set_battery_widget_bg_color, true,  true,  CONFIG_PACMAN_BATTERY_WIDGET_BG_COLOR)   \
    X(BATTERY_WIDGET_PERCENTAGE, "battery-widget-percentage", COLOR,  0,     0xffffff,                NULL,              set_battery_widget_percentage_color, true,  true,  CONFIG_PACMAN_BATTERY_WIDGET_PERCENTAGE_COLOR)   \
    X(BATTERY_WIDGET_TEXT,   "battery-widget-text",   COLOR,  0,     0xffffff,                NULL,              set_battery_widget_text_color, true,  true,  CONFIG_PACMAN_BATTERY_WIDGET_TEXT_COLOR)   \
    X(BATTERY_NUM,           "battery-num",           COLOR,  0,     0xffffff,                NULL,              set_battery_num_color,       true,  true,  CONFIG_PACMAN_BATTERY_NUM_COLOR)   \
    X(BATTERY_BG,            "battery-bg",            COLOR,  0,     0xffffff,                NULL,              set_battery_bg_color,        true,  true,  CONFIG_PACMAN_BATTERY_BG_COLOR)   \
    X(BATTERY_PERCENTAGE,    "battery-percentage",    COLOR,  0,     0xffffff,                NULL,              set_battery_percentage_color, true,  true,  CONFIG_PACMAN_BATTERY_PERCENTAGE_COLOR)   \
    X(BATTERY_NUM_1,         "battery-num-1",         COLOR,  0,     0xffffff,                NULL,              set_battery_num_color_1,     true,  true,  CONFIG_PACMAN_BATTERY_NUM_COLOR_1)   \
    X(BATTERY_BG_1,          "battery-bg-1",          COLOR,  0,     0xffffff,                NULL,              set_battery_bg_color_1,      true,  true,  CONFIG_PACMAN_BATTERY_BG_COLOR_1)   \
    X(BATTERY_PERCENTAGE_1,  "battery-percentage-1",  COLOR,  0,     0xffffff,                NULL,              set_battery_percentage_color_1, true,  true,  CONFIG_PACMAN_BATTERY_PERCENTAGE_COLOR_1)   \
    X(BATTERY_NUM_2,         "battery-num-2",         COLOR,  0,     0xffffff,                NULL,              set_battery_num_color_2,     true,  true,  CONFIG_PACMAN_BATTERY_NUM_COLOR_2)   \
    X(BATTERY_BG_2,          "battery-bg-2",          COLOR,  0,     0xffffff,                NULL,              set_battery_bg_color_2,      true,  true,  CONFIG_PACMAN_BATTERY_BG_COLOR_2)   \
    X(BATTERY_PERCENTAGE_2,  "battery-percentage-2",  COLOR,  0,     0xffffff,                NULL,              set_battery_percentage_color_2, true,  true,  CONFIG_PACMAN_BATTERY_PERCENTAGE_COLOR_2)   \
    X(FRAME,                 "frame",                 COLOR,  0,     0xffffff,                NULL,              set_frame_color,             true,  true,  CONFIG_PACMAN_FRAME_COLOR)   \
    X(FRAME_1,               "frame-1",               COLOR,  0,     0xffffff,                NULL,              set_frame_color_1,           true,  true,  CONFIG_PACMAN_FRAME_COLOR_1)   \
    X(WPM_FONT,              "wpm-font",              COLOR,  0,     0xffffff,                NULL,              set_wpm_font_color,          true,  true,  CONFIG_PACMAN_WPM_FONT_COLOR)   \
    X(WPM_FONT_1,            "wpm-font-1",            COLOR,  0,     0xffffff,                NULL,              set_wpm_font_1_color,        true,  true,  CONFIG_PACMAN_WPM_FONT_1_COLOR)   \
    X(WPM_FONT_BG,           "wpm-font-bg",           COLOR,  0,     0xffffff,                NULL,              set_wpm_font_bg_color,       true,  true,  CONFIG_PACMAN_WPM_FONT_BG_COLOR)   \
    X(MENU_BG,               "menu-bg",               COLOR,  0,     0xffffff,                NULL,              set_menu_bg_color,           true,  true,  CONFIG_PACMAN_MENU_BG_COLOR)   \
    X(MODIFIER_SELECTED,     "modifier-selected",     COLOR,  0,     0xffffff,                NULL,              set_modifier_selected_color, true,  true,  CONFIG_PACMAN_MODIFIER_SELECTED_COLOR)   \
    X(MODIFIER_UNSELECTED,   "modifier-unselected",   COLOR,  0,     0xffffff,                NULL,              set_modifier_unselected_color, true,  true,  CONFIG_PACMAN_MODIFIER_UNSELECTED_COLOR)   \
    X(MODIFIER_BG,           "modifier-bg",           COLOR,  0,     0xffffff,                NULL,              set_modifier_bg_color,       true,  true,  CONFIG_PACMAN_MODIFIER_BG_COLOR)   \
    X(SYMBOL_SELECTED,       "symbol-selected",       COLOR,  0,     0xffffff,                NULL,              set_symbol_selected_color,   true,  true,  CONFIG_PACMAN_SYMBOL_SELECTED_COLOR)   \
    X(SYMBOL_UNSELECTED,     "symbol-unselected",     COLOR,  0,     0xffffff,                NULL,              set_symbol_unselected_color, true,  true,  CONFIG_PACMAN_SYMBOL_UNSELECTED_COLOR)   \
    X(SYMBOL_BG,             "symbol-bg",             COLOR,  0,     0xffffff,                NULL,              set_symbol_bg_color,         true,  true,  CONFIG_PACMAN_SYMBOL_BG_COLOR)   \
    X(THEME_FONT_BG,         "theme-font-bg",         COLOR,  0,     0xffffff,                NULL,              set_theme_font_bg_color,     true,  true,  CONFIG_PACMAN_THEME_FONT_BG_COLOR)   \
    X(THEME_FONT,            "theme-font",            COLOR,  0,     0xffffff,                NULL,              set_theme_font_color,        true,  true,  CONFIG_PACMAN_THEME_FONT_COLOR)   \
    X(LAYER_FONT_BG,         "layer-font-bg",         COLOR,  0,     0xffffff,                NULL,              set_layer_font_bg_color,     true,  true,  CONFIG_PACMAN_LAYER_FONT_BG_COLOR)   \
    X(LAYER_FONT,            "layer-font",            COLOR,  0,     0xffffff,                NULL,              set_layer_font_color,        true,  true,  CONFIG_PACMAN_LAYER_FONT_COLOR)   \
    X(THEME_FONT_1,          "theme-font-1",          COLOR,  0,     0xffffff,                NULL,              set_theme_font_color_1,      true,  true,  CONFIG_PACMAN_THEME_FONT_COLOR_1)   \
    X(LOGO_BG,               "logo-bg",               COLOR,  0,     0xffffff,                NULL,              set_logo_bg_color,           true,  true,  CONFIG_PACMAN_LOGO_BG_COLOR)   \
    X(LOGO_FONT,             "logo-font",             COLOR,  0,     0xffffff,                NULL,              set_logo_font_color,         true,  true,  CONFIG_PACMAN_LOGO_FONT_COLOR)   \
    X(LOGO_ACCENT,           "logo-accent",           COLOR,  0,     0xffffff,                NULL,              set_logo_accent_color,       true,  true,  CONFIG_PACMAN_LOGO_ACCENT_COLOR)   \
    X(BT_NUM,                "bt-num",                COLOR,  0,     0xffffff,                NULL,              set_bt_num_color,            true,  true,  CONFIG_PACMAN_BT_NUM_COLOR)   \
    X(BT_BG,                 "bt-bg",                 COLOR,  0,     0xffffff,                NULL,              set_bt_bg_color,             true,  true,  CONFIG_PACMAN_BT_BG_COLOR)   \
    X(BT_STATUS_OK,          "bt-status-ok",          COLOR,  0,     0xffffff,                NULL,              set_bt_status_ok_color,      true,  true,  CONFIG_PACMAN_BT_STATUS_OK_COLOR)   \
    X(BT_STATUS_NOT_OK,      "bt-status-not-ok",      COLOR,  0,     0xffffff,                NULL,              set_bt_status_not_ok_color,  true,  true,  CONFIG_PACMAN_BT_STATUS_NOT_OK_COLOR)   \
    X(BT_STATUS_OPEN,        "bt-status-open",        COLOR,  0,     0xffffff,                NULL,              set_bt_status_open_color,    true,  true,  CONFIG_PACMAN_BT_STATUS_OPEN_COLOR)   \
    X(BT_STATUS_BG,          "bt-status-bg",          COLOR,  0,     0xffffff,                NULL,              set_bt_status_bg_color,      true,  true,  CONFIG_PACMAN_BT_STATUS_BG_COLOR)   \
    X(VOLUME,                "volume",                NUMBER, 0,     100,                     NULL,              apply_volume,                true,  false, STRINGIFY(CONFIG_PACMAN_SOUND_VOLUME))   \
    X(BASS_FLOOR,            "bass-floor",            NUMBER, 0,     4000,                    NULL,              apply_bass_floor,            true,  false, STRINGIFY(CONFIG_PACMAN_SOUND_BASS_FLOOR_HZ))   \
    X(SAMPLE_RATE,           "sample-rate",           NUMBER, 8000,  48000,                   NULL,              NULL,                        false, false, STRINGIFY(CONFIG_PACMAN_SOUND_SAMPLE_RATE))   \
    X(FRAME_INTERVAL,        "frame-interval",        NUMBER, 5,     1000,                    NULL,              apply_frame_interval,        true,  false, STRINGIFY(CONFIG_PACMAN_FRAME_INTERVAL))   \
    X(SPLASH_FRAMES,         "splash-frames",         NUMBER, 0,     1000,                    NULL,              NULL,                        false, false, STRINGIFY(CONFIG_PACMAN_SPLASH_FRAMES))   \
    X(SPLASH_INTERVAL,       "splash-interval",       NUMBER, 5,     1000,                    NULL,              NULL,                        false, false, STRINGIFY(CONFIG_PACMAN_SPLASH_INTERVAL))   \
    X(LOGO_INTERVAL,         "logo-interval",         NUMBER, 5,     1000,                    NULL,              NULL,                        false, false, STRINGIFY(CONFIG_PACMAN_LOGO_WALK_INTERVAL))   \
    X(WPM_SLOW,              "wpm-slow",              NUMBER, 0,     255,                     NULL,              NULL,                        true,  false, STRINGIFY(CONFIG_PACMAN_WPM_SLOW))   \
    X(WPM_FAST,              "wpm-fast",              NUMBER, 0,     255,                     NULL,              NULL,                        true,  false, STRINGIFY(CONFIG_PACMAN_WPM_FAST))   \
    X(THEME_THRESHOLD,       "theme-threshold",       NUMBER, 0,     65535,                   NULL,              apply_theme_threshold,       true,  false, STRINGIFY(CONFIG_PACMAN_THEME_THRESHOLD))   \
    X(MUTE_THRESHOLD,        "mute-threshold",        NUMBER, 0,     65535,                   NULL,              apply_mute_threshold,        true,  false, STRINGIFY(CONFIG_PACMAN_MUTE_THRESHOLD))
