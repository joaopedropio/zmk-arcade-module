/*
 * Pac-Man dongle widget - ZMK/LVGL glue for the games.
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
    PACMAN_GAME_PACMAN = 0,
    PACMAN_GAME_SHOOTER,
    PACMAN_GAME_BOMBER,
    PACMAN_GAME_FIGHTER,
    PACMAN_GAME_COMMANDO,
    PACMAN_GAME_FROGGER,
} PacmanGame;

/* called once from the custom status screen */
void zmk_widget_pacman_init(void);

/* rebuild every game's palette from the stored colours */
void pacman_reload_palette(void);

/* switch which one is playing; the panel is repainted from the top */
void pacman_set_game(uint8_t which);

/* retime the frame timer, in milliseconds */
void pacman_set_frame_interval(uint32_t ms);

/* the animation starts paused; the status screen kicks it off */
void pacman_start(void);
void pacman_stop(void);
void pacman_toggle_pause(void);
bool pacman_is_paused(void);
