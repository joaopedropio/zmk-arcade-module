/*
 * Pac-Man dongle - the animated header of the dashboard.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

void logo_animation_timer(lv_timer_t *timer);
void logo_animation_init(void);
void stop_animation(void);
void start_animation(void);
