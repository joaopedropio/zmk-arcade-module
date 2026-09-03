/*
 * Arcade dongle widget - ZMK/LVGL glue for the games.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The animations the panel can play.  They share the timer, the palette reload
 * and the start/stop below, because from everything else's point of view there
 * is one game screen and this only says what is on it.
 */
typedef enum {
    ARCADE_GAME_PACMAN = 0,
    ARCADE_GAME_SHOOTER,
    ARCADE_GAME_BOMBER,
    ARCADE_GAME_FIGHTER,
    ARCADE_GAME_COMMANDO,
    ARCADE_GAME_FROGGER,
    ARCADE_GAME_KONG,
    ARCADE_GAME_TEMPEST,
} ArcadeGame;

/* called once from the custom status screen */
void zmk_widget_arcade_init(void);

/* rebuild every game's palette from the stored colours */
void arcade_reload_palette(void);

/* switch which one is playing; the panel is repainted from the top */
void arcade_set_game(uint8_t which);

/* retime the frame timer, in milliseconds */
void arcade_set_frame_interval(uint32_t ms);

/* the animation starts paused; the status screen kicks it off */
void arcade_start(void);
void arcade_stop(void);
void arcade_toggle_pause(void);
bool arcade_is_paused(void);
