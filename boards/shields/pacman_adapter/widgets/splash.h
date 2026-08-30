/*
 * Pac-Man dongle - splash screen.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <lvgl.h>

void print_splash(void);
void zmk_widget_splash_init(void);
void clean_up_splash(void);
void reset_splash(void);
