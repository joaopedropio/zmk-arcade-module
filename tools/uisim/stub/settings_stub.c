/*
 * Host stand-in for the settings store.
 *
 * On the dongle these read and write flash; here the preview is told its
 * colours and its theme directly, so all that is needed is somewhere for the
 * theme to live between being set and being drawn.  Only the three the UI
 * files actually call are here - the rest of helpers/settings.h needs Zephyr
 * and is not linked in.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>

static uint8_t current_theme;
static bool muted;

uint8_t arcade_settings_get_current_theme(void) { return current_theme; }

int arcade_settings_save_current_theme(uint8_t theme) {
    current_theme = theme;
    return 0;
}

int arcade_settings_toggle_mute(void) {
    muted = !muted;
    return 0;
}

bool arcade_settings_get_mute(void) { return muted; }
