/*
 * Arcade dongle - the action button, and the dashboard it opens.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

void zmk_widget_action_button_init(void);
void start_action_button(bool is_menu_on);
void print_menu(void);

/* repaint whichever screen is up, from the display queue */
void refresh_screen(void);
void set_theme_threshold(uint16_t term_ms);

/* held past this, the press mutes or unmutes instead of changing profile */
void set_mute_threshold(uint16_t term_ms);
