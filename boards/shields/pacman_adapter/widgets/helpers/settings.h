/*
 * Pac-Man dongle - what survives a power cycle.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t current_theme;
    bool mute;
} settings_t;

int pacman_settings_save_current_theme(uint8_t current_theme);
uint8_t pacman_settings_get_current_theme(void);

int pacman_settings_toggle_mute(void);
bool pacman_settings_get_mute(void);
