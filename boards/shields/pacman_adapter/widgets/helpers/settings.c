#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "../action_button.h"
#include "../pacman.h"
#include "../sound.h"
#include "../theme.h"
#include "display.h"
#include "settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * The words each ENUM setting takes, in the order of the enum they stand for,
 * so a label's index is the value itself and neither direction needs a lookup
 * table of its own.  The trailing NULL is what ends the search.
 */
static const char *const mute_labels[] = {"off", "on", NULL};
static const char *const screen_labels[] = {"game", "status", NULL};
static const char *const slot_mode_labels[] = {"2-slot", "4-slot", "5-slot", "6-slot", NULL};
static const char *const slot_labels[] = {"connectivity", "layer",   "theme", "wpm",
                                          "modifiers",    "battery", "empty", NULL};
static const char *const rotate_labels[] = {"0", "90", "180", "270", NULL};

/* the labels are indexes into display.h's enums, so they have to end together */
_Static_assert(ARRAY_SIZE(screen_labels) == STATUS_SCREEN + 2, "screen labels out of step");
_Static_assert(ARRAY_SIZE(slot_mode_labels) == SLOT_MODE_6 + 2, "slot mode labels out of step");
_Static_assert(ARRAY_SIZE(slot_labels) == SLOT_NAME_NONE + 2, "slot labels out of step");
_Static_assert(ARRAY_SIZE(rotate_labels) == DISPLAY_ORIENTATION_270 + 2,
               "rotation labels out of step");

static uint32_t values[PACMAN_SETTING_COUNT];

/* which of them flash had something to say about, and so the build must not touch */
static bool stored[PACMAN_SETTING_COUNT];

uint32_t pacman_settings_get(pacman_setting_id id) { return values[id]; }

/*
 * Everything below turns one stored value into a call on whoever draws or
 * sounds it.  They all take a uint32_t whether they need it or not, so the
 * table can hold them in one column; the ones that stand for a group of
 * settings ignore it and read the whole group back instead.
 */
static void apply_screen(uint32_t v) { set_default_screen((DefaultScreen)v); }
static void apply_slot_mode(uint32_t v) { set_slot_mode((SlotMode)v); }
static void apply_slot_1(uint32_t v) { set_slot_1((SlotName)v); }
static void apply_slot_2(uint32_t v) { set_slot_2((SlotName)v); }
static void apply_slot_3(uint32_t v) { set_slot_3((SlotName)v); }
static void apply_slot_4(uint32_t v) { set_slot_4((SlotName)v); }
static void apply_slot_5(uint32_t v) { set_slot_5((SlotName)v); }
static void apply_slot_6(uint32_t v) { set_slot_6((SlotName)v); }
static void apply_battery_slots(uint32_t v) { set_battery_slots((uint8_t)v); }
static void apply_rotate(uint32_t v) { set_display_orientation((DisplayOrientation)v); }
static void apply_mute(uint32_t v) { pacman_sound_set_mute(v != 0); }
static void apply_theme(uint32_t v) { set_theme_number((uint8_t)v); }
static void apply_volume(uint32_t v) { pacman_sound_set_volume((uint8_t)v); }
static void apply_bass_floor(uint32_t v) { pacman_sound_set_bass_floor((uint16_t)v); }
static void apply_frame_interval(uint32_t v) { pacman_set_frame_interval(v); }
static void apply_theme_threshold(uint32_t v) { set_theme_threshold((uint16_t)v); }
static void apply_mute_threshold(uint32_t v) { set_mute_threshold((uint16_t)v); }

static void apply_theme_colors(uint32_t v) {
    ARG_UNUSED(v);
    set_custom_theme_colors(values[PACMAN_SETTING_THEME_PRIMARY],
                            values[PACMAN_SETTING_THEME_SECONDARY],
                            values[PACMAN_SETTING_THEME_BG],
                            values[PACMAN_SETTING_THEME_BG_DARKER]);
}

static void apply_game_palette(uint32_t v) {
    ARG_UNUSED(v);
    pacman_reload_palette();
}

static void apply_splash_multi(uint32_t v) {
    ARG_UNUSED(v);
    set_splash_logo_multicolor(
        values[PACMAN_SETTING_SPLASH_MULTI_0], values[PACMAN_SETTING_SPLASH_MULTI_1],
        values[PACMAN_SETTING_SPLASH_MULTI_2], values[PACMAN_SETTING_SPLASH_MULTI_3]);
}

#define PACMAN_SETTING_ROW(id, nm, knd, lo, hi, lbls, ap, lv, ov, dflt)                            \
    [PACMAN_SETTING_##id] = {.name = nm,                                                           \
                             .kind = PACMAN_VALUE_##knd,                                           \
                             .min = (lo),                                                          \
                             .max = (hi),                                                          \
                             .labels = lbls,                                                       \
                             .apply = ap,                                                          \
                             .live = lv,                                                           \
                             .override = ov,                                                       \
                             .build = dflt},

static const pacman_setting settings_table[PACMAN_SETTING_COUNT] = {
    PACMAN_SETTING_LIST(PACMAN_SETTING_ROW)};

/*
 * The first version of this file wrote all of it as one blob under
 * "pacman/settings".  It is read here and applied from the commit callback
 * rather than on the spot, because the per-key entries may well load after it
 * and whichever order they arrive in, the newer key has to win.
 */
typedef struct {
    uint8_t current_theme;
    bool mute;
} legacy_settings_t;

static legacy_settings_t legacy;
static bool have_legacy;

static bool valid(pacman_setting_id id, uint32_t value) {
    return value >= settings_table[id].min && value <= pacman_settings_max(id);
}

static int save(pacman_setting_id id) {
    char key[40];

    snprintf(key, sizeof(key), "pacman/%s", settings_table[id].name);
    int rc = settings_save_one(key, &values[id], sizeof(values[id]));
    if (rc) {
        LOG_ERR("Failed to save %s: %d", settings_table[id].name, rc);
    }
    return rc;
}

static int settings_set_cb(const char *key, size_t len_rd, settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(key, "settings") == 0) {
        if (len_rd != sizeof(legacy)) {
            LOG_ERR("Invalid size for the legacy settings blob: %zu", len_rd);
            return -EINVAL;
        }
        ssize_t rc = read_cb(cb_arg, &legacy, sizeof(legacy));
        if (rc < 0) {
            LOG_ERR("Failed to read the legacy settings blob: %d", (int)rc);
            return rc;
        }
        have_legacy = true;
        return 0;
    }

    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (strcmp(key, settings_table[id].name) != 0) {
            continue;
        }
        uint32_t value;
        if (len_rd != sizeof(value)) {
            LOG_ERR("Invalid size for %s: %zu", key, len_rd);
            return -EINVAL;
        }
        ssize_t rc = read_cb(cb_arg, &value, sizeof(value));
        if (rc < 0) {
            LOG_ERR("Failed to read %s: %d", key, (int)rc);
            return rc;
        }
        values[id] = value;
        stored[id] = true;
        return 0;
    }

    return -ENOENT;
}

/*
 * Range-checking waits until here.  The theme's upper bound is however many
 * themes the build has, which a later build can lower - so a stored value can
 * be in range when it is written and out of range when it is read back, and
 * dropping it is what stops the dashboard indexing past the end of the table.
 */
static int settings_commit_cb(void) {
    if (have_legacy) {
        if (!stored[PACMAN_SETTING_THEME]) {
            values[PACMAN_SETTING_THEME] = legacy.current_theme;
            stored[PACMAN_SETTING_THEME] = true;
        }
        if (!stored[PACMAN_SETTING_MUTE]) {
            values[PACMAN_SETTING_MUTE] = legacy.mute ? 1 : 0;
            stored[PACMAN_SETTING_MUTE] = true;
        }
    }

    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (stored[id] && !valid(id, values[id])) {
            LOG_WRN("Stored %s is out of range (%u); taking the build's instead",
                    settings_table[id].name, values[id]);
            values[id] = settings_table[id].min;
            stored[id] = false;
        }
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pacman_settings_handler, "pacman", NULL, settings_set_cb,
                               settings_commit_cb, NULL);

const pacman_setting *pacman_settings_describe(pacman_setting_id id) { return &settings_table[id]; }

uint32_t pacman_settings_max(pacman_setting_id id) {
    if (id == PACMAN_SETTING_THEME) {
        return get_themes_colors_len() - 1;
    }
    return settings_table[id].max;
}

int pacman_settings_find(const char *name) {
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (strcmp(name, settings_table[id].name) == 0) {
            return id;
        }
    }
    return -1;
}

int64_t pacman_settings_parse(pacman_setting_id id, const char *word) {
    const pacman_setting *desc = &settings_table[id];

    if (desc->kind == PACMAN_VALUE_ENUM) {
        for (uint32_t value = 0; desc->labels[value] != NULL; value++) {
            if (strcmp(word, desc->labels[value]) == 0) {
                return value;
            }
        }
        return -1;
    }

    if (desc->kind == PACMAN_VALUE_COLOR) {
        uint32_t color = hex_string_to_uint(word);
        return color == HEX_PARSE_ERROR ? -1 : (int64_t)color;
    }

    char *end;
    long value = strtol(word, &end, 10);
    if (*word == '\0' || *end != '\0' || value < 0) {
        return -1;
    }
    return (int64_t)value;
}

const char *pacman_settings_format(pacman_setting_id id, uint32_t value, char *buf, size_t len) {
    const pacman_setting *desc = &settings_table[id];

    if (desc->kind == PACMAN_VALUE_ENUM && value <= pacman_settings_max(id)) {
        snprintf(buf, len, "%s", desc->labels[value]);
    } else if (desc->kind == PACMAN_VALUE_COLOR) {
        snprintf(buf, len, "%06x", value & 0xffffffu);
    } else {
        snprintf(buf, len, "%u", value);
    }
    return buf;
}

bool pacman_settings_is_stored(pacman_setting_id id) { return stored[id]; }

int pacman_settings_set(pacman_setting_id id, uint32_t value) {
    if (!valid(id, value)) {
        return -EINVAL;
    }

    uint32_t previous = values[id];
    bool was_stored = stored[id];

    values[id] = value;
    stored[id] = true;

    int rc = save(id);
    if (rc) {
        values[id] = previous;
        stored[id] = was_stored;
    }
    return rc;
}

int pacman_settings_forget(pacman_setting_id id) {
    char key[40];

    snprintf(key, sizeof(key), "pacman/%s", settings_table[id].name);
    int rc = settings_delete(key);
    if (rc) {
        LOG_ERR("Failed to forget %s: %d", settings_table[id].name, rc);
        return rc;
    }
    stored[id] = false;
    return 0;
}

/*
 * A build's own value going unread is a typo in somebody's .conf, so it is
 * worth a line in the log - but not worth failing the boot over.  Falling back
 * to the bottom of the range costs a slot or a colour, not a dongle.
 */
void pacman_settings_load_defaults(void) {
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (stored[id]) {
            continue;
        }
        int64_t value = pacman_settings_parse(id, settings_table[id].build);
        if (value < 0 || !valid(id, (uint32_t)value)) {
            LOG_WRN("%s does not take \"%s\"", settings_table[id].name, settings_table[id].build);
            value = settings_table[id].min;
        }
        values[id] = (uint32_t)value;
    }
}

/*
 * What each setting was last pushed at whatever uses it.  Skipping the ones
 * that have not moved is not an optimisation: apply_all() runs over every
 * setting each time the shell writes one, and the mute's apply makes a sound
 * to prove it worked - so without this, changing a colour would chirp the
 * speaker and repaint the maze.  A theme change is the exception and forces
 * the colours through, because there the value has not moved but what is on
 * the panel has.
 */
static uint32_t applied[PACMAN_SETTING_COUNT];
static bool has_applied[PACMAN_SETTING_COUNT];

static void apply_one(pacman_setting_id id, bool force) {
    if (settings_table[id].apply == NULL) {
        return;
    }
    /*
     * A theme derives every colour marked override from its own four, so the
     * build's value for one of them must never be applied on top - that would
     * paint theme 5's dashboard in theme 0's colours and make the whole set
     * look identical.  Only a colour somebody stored deliberately outranks the
     * theme, and that one goes back on after every theme change.
     */
    if (settings_table[id].override && !stored[id]) {
        return;
    }
    if (!force && has_applied[id] && applied[id] == values[id]) {
        return;
    }
    applied[id] = values[id];
    has_applied[id] = true;
    settings_table[id].apply(values[id]);
}

void pacman_settings_apply(pacman_setting_id id) { apply_one(id, false); }

void pacman_settings_apply_all(void) {
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        apply_one(id, false);
    }
}

void pacman_settings_apply_colors(void) {
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (settings_table[id].override) {
            apply_one(id, true);
        }
    }
}

int pacman_settings_save_current_theme(uint8_t current_theme) {
    int rc = pacman_settings_set(PACMAN_SETTING_THEME, current_theme);
    if (rc == 0) {
        apply_one(PACMAN_SETTING_THEME, false);
    }
    return rc;
}

uint8_t pacman_settings_get_current_theme(void) {
    return (uint8_t)pacman_settings_get(PACMAN_SETTING_THEME);
}

int pacman_settings_toggle_mute(void) {
    int rc = pacman_settings_set(PACMAN_SETTING_MUTE, pacman_settings_get_mute() ? 0 : 1);
    if (rc == 0) {
        apply_one(PACMAN_SETTING_MUTE, false);
    }
    return rc;
}

bool pacman_settings_get_mute(void) { return pacman_settings_get(PACMAN_SETTING_MUTE) != 0; }
