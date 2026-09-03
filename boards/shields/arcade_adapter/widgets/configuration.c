/*
 * Arcade dongle - the build's values into the running one.
 *
 * There used to be a function per setting here, each pulling a Kconfig symbol
 * apart and handing it to a setter.  All of that now lives as one line per
 * setting in helpers/settings_list.h, so this file has two jobs left: fill in
 * whatever flash had nothing to say about, and push the result at everything
 * that draws or sounds it - in that order, before any widget is built.
 *
 * It also tells display.c what to re-apply after a theme change, because a
 * theme derives the whole dashboard from four colours and would otherwise
 * throw away any colour somebody had set by hand.
 *
 * Between the two comes the profiles: the dongle is always on one, and the one
 * it is on the first time it boots is the firmware it was flashed with - which
 * is only known once the defaults are in.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(arcade_configuration, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include "configuration.h"
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"

void configure(void) {
    arcade_settings_load_defaults();
    arcade_profile_init();
    set_color_override_cb(arcade_settings_apply_colors);
    arcade_settings_apply_all();
}
