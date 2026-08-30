/*
 * Pac-Man dongle widget - ZMK/LVGL glue.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* called once from the custom status screen */
void zmk_widget_pacman_init(void);

/* rebuild the palette from the stored colours */
void pacman_reload_palette(void);

/* retime the frame timer, in milliseconds */
void pacman_set_frame_interval(uint32_t ms);

/* the animation starts paused; the status screen kicks it off */
void pacman_start(void);
void pacman_stop(void);
void pacman_toggle_pause(void);
bool pacman_is_paused(void);
