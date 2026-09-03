/*
 * Arcade dongle - what survives a power cycle.
 *
 * Kconfig is the default and flash is the override.  configure() offers every
 * value the build chose to arcade_settings_load_defaults(), which keeps it
 * only where flash had nothing to say, and arcade_settings_apply_all() then
 * pushes whichever won at whatever draws or sounds it.  That is what lets a
 * setting be changed from the shell and still be there after a reboot, rather
 * than the next boot's Kconfig quietly undoing it.
 *
 * One flash key per setting, not one struct.  A struct is a single blob whose
 * length is part of its identity: adding a field to it makes every dongle's
 * saved copy the wrong size, and the whole lot - theme, mute, everything -
 * reverts to the build's defaults without saying so.  Separate keys grow
 * without throwing away what is already stored, and a change rewrites only
 * the setting that changed, which is what the flash would rather we did.
 *
 * The settings themselves are in settings_list.h; nothing here needs editing
 * to add one.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "settings_list.h"

#define ARCADE_SETTING_ID(id, ...) ARCADE_SETTING_##id,

typedef enum {
    ARCADE_SETTING_LIST(ARCADE_SETTING_ID) ARCADE_SETTING_COUNT,
} arcade_setting_id;

typedef enum {
    ARCADE_VALUE_ENUM,   /* one of a fixed set of words */
    ARCADE_VALUE_NUMBER, /* a decimal, min to max */
    ARCADE_VALUE_COLOR,  /* rrggbb, with a leading # or 0x allowed */
} arcade_value_kind;

typedef struct {
    const char *name;
    arcade_value_kind kind;
    uint32_t min;
    /* inclusive, and 0 for the theme, whose count only the build knows */
    uint32_t max;
    /* NULL for anything but an ENUM */
    const char *const *labels;
    void (*apply)(uint32_t value);
    bool live;
    bool override;
    /* what the firmware was built with, in the same words the shell takes */
    const char *build;
} arcade_setting;

const arcade_setting *arcade_settings_describe(arcade_setting_id id);

/* inclusive upper bound, which for the theme is however many the build has */
uint32_t arcade_settings_max(arcade_setting_id id);

/* the id a name stands for, or -1 */
int arcade_settings_find(const char *name);

/*
 * The value a word stands for - a label, a decimal or an rrggbb - or -1.
 * int64_t because a colour will not fit in the int a -1 has to fit in too.
 */
int64_t arcade_settings_parse(arcade_setting_id id, const char *word);

/* what to print for a value: a label, a decimal or an rrggbb, into buf */
const char *arcade_settings_format(arcade_setting_id id, uint32_t value, char *buf, size_t len);

/* the longest arcade_settings_format() output, plus its terminator */
#define ARCADE_SETTING_VALUE_LEN 16

uint32_t arcade_settings_get(arcade_setting_id id);

/* whether the value came out of flash rather than out of the build */
bool arcade_settings_is_stored(arcade_setting_id id);

/* range-checks, stores and writes flash; -EINVAL if the value is out of range */
int arcade_settings_set(arcade_setting_id id, uint32_t value);

/* drops the stored value, so the next boot takes the build's again */
int arcade_settings_forget(arcade_setting_id id);

/* Kconfig into whatever flash left empty; configure() calls this first */
void arcade_settings_load_defaults(void);

/* push one setting at whatever uses it, if it has moved since last time */
void arcade_settings_apply(arcade_setting_id id);

/* the same for every setting, in list order */
void arcade_settings_apply_all(void);

/*
 * Put the stored colours back on top of what a theme change just derived - the
 * stored ones only, since an override colour the build set is the theme's to
 * decide.
 * display.c calls this through a hook it is handed rather than directly,
 * because tools/uisim builds display.c against the Zephyr stubs, where there
 * is no flash to read.
 */
void arcade_settings_apply_colors(void);

/* the two shorthands the UI already had, so its callers do not have to care */
int arcade_settings_save_current_theme(uint8_t current_theme);
uint8_t arcade_settings_get_current_theme(void);
int arcade_settings_toggle_mute(void);
bool arcade_settings_get_mute(void);
