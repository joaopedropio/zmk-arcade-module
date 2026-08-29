/*
 * Pac-Man dongle widget - ZMK/LVGL glue.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/* called once from the custom status screen */
void zmk_widget_pacman_init(void);

/* the animation starts paused; the status screen kicks it off */
void pacman_start(void);
void pacman_stop(void);
void pacman_toggle_pause(void);
bool pacman_is_paused(void);
