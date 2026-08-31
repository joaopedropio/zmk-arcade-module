/*
 * Pac-Man dongle - the settings shell.
 *
 * A `pacman` command on whatever shell backend the build has, so the dongle
 * can be re-themed, recoloured or rearranged over USB instead of by
 * rebuilding and flashing.  Every value it writes goes through
 * helpers/settings.h, which is what makes it survive the reboot.
 *
 * `pacman schema` is the same information with the prose taken out: one
 * tab-separated line per setting and an `end` to say the reply is complete.
 * That is what the configurator page reads, so the page describes itself from
 * whatever firmware it is talking to and cannot drift out of step with it.
 *
 * `pacman profile` keeps named sets of all of it on the dongle, and the dongle
 * is always on one of them - so `set` is not a change to the settings and a
 * separate change to a profile, it is a change to the profile the dongle is
 * on.  Its replies are a wire format too: `list`, `show` and `current` print
 * tab-separated columns closed by a bare `end`, and everything that writes
 * answers with a sentence whose first word is what it did - saved, loaded,
 * renamed, deleted, staged, cleared.  Anything else the page is reading is a
 * failure, which saves both ends from a list of error strings to keep in step.
 *
 * Not everything can be shown before that reboot, and the shell says which
 * rather than pretending: the slot widgets each size and allocate a scratch
 * bitmap from the slot they were handed at init, so moving one means building
 * all of them again with the panel live.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zmk/display.h>

#include "action_button.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"

/*
 * The panel is written a dirty rectangle at a time by the game timer, on the
 * display queue.  The shell runs on its own thread, so anything that draws is
 * handed to that queue instead - two threads pushing the same SPI bus is a
 * corrupt screen, and the sound thread sits above both of them.
 *
 * Re-applying everything rather than just the one that changed keeps the work
 * item from having to carry which: the setters are all idempotent, and eighty
 * of them cost less than the repaint that follows.
 */
static void apply_on_display_queue(struct k_work *work) {
    ARG_UNUSED(work);

    pacman_settings_apply_all();
    refresh_screen();
}

static K_WORK_DEFINE(apply_work, apply_on_display_queue);

static void apply(void) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &apply_work);
    }
}

static const char *kind_name(pacman_value_kind kind) {
    switch (kind) {
    case PACMAN_VALUE_ENUM:
        return "enum";
    case PACMAN_VALUE_COLOR:
        return "color";
    default:
        return "number";
    }
}

static void print_value(const struct shell *sh, pacman_setting_id id) {
    const pacman_setting *desc = pacman_settings_describe(id);
    char value[PACMAN_SETTING_VALUE_LEN];

    shell_print(sh, "%s %-24s %-14s %s", pacman_settings_is_stored(id) ? "*" : " ", desc->name,
                pacman_settings_format(id, pacman_settings_get(id), value, sizeof(value)),
                desc->live ? "live" : "next boot");
}

static void print_accepted(const struct shell *sh, pacman_setting_id id) {
    const pacman_setting *desc = pacman_settings_describe(id);

    if (desc->kind == PACMAN_VALUE_COLOR) {
        shell_print(sh, "%s takes a colour, as rrggbb", desc->name);
        return;
    }
    if (desc->kind == PACMAN_VALUE_NUMBER) {
        shell_print(sh, "%s takes %u to %u", desc->name, desc->min, pacman_settings_max(id));
        return;
    }

    shell_fprintf(sh, SHELL_NORMAL, "%s takes", desc->name);
    for (uint32_t value = desc->min; value <= pacman_settings_max(id); value++) {
        shell_fprintf(sh, SHELL_NORMAL, " %s", desc->labels[value]);
    }
    shell_fprintf(sh, SHELL_NORMAL, "\n");
}

static int cmd_get(const struct shell *sh, size_t argc, char **argv) {
    if (argc == 2) {
        int id = pacman_settings_find(argv[1]);
        if (id < 0) {
            shell_error(sh, "no setting called \"%s\"", argv[1]);
            return -ENOENT;
        }
        print_value(sh, id);
        return 0;
    }

    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        print_value(sh, id);
    }
    shell_print(sh, "");
    shell_print(sh, "* is stored in flash; the rest are what the firmware was built with.");
    return 0;
}

/*
 * name  kind  value  stored|default  live|boot  min  max  labels
 *
 * Tab separated, labels comma separated and "-" where there are none, and a
 * bare "end" once every setting has been printed.  Adding a column would
 * break the page that reads it; adding a setting will not.
 */
static int cmd_schema(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        const pacman_setting *desc = pacman_settings_describe(id);
        char value[PACMAN_SETTING_VALUE_LEN];

        shell_fprintf(sh, SHELL_NORMAL, "%s\t%s\t%s\t%s\t%s\t%u\t%u\t", desc->name,
                      kind_name(desc->kind),
                      pacman_settings_format(id, pacman_settings_get(id), value, sizeof(value)),
                      pacman_settings_is_stored(id) ? "stored" : "default",
                      desc->live ? "live" : "boot", desc->min, pacman_settings_max(id));

        if (desc->kind != PACMAN_VALUE_ENUM) {
            shell_fprintf(sh, SHELL_NORMAL, "-\n");
            continue;
        }
        for (uint32_t v = desc->min; v <= pacman_settings_max(id); v++) {
            shell_fprintf(sh, SHELL_NORMAL, "%s%s", v == desc->min ? "" : ",", desc->labels[v]);
        }
        shell_fprintf(sh, SHELL_NORMAL, "\n");
    }
    shell_print(sh, "end");
    return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int id = pacman_settings_find(argv[1]);
    if (id < 0) {
        shell_error(sh, "no setting called \"%s\"", argv[1]);
        return -ENOENT;
    }

    int64_t value = pacman_settings_parse(id, argv[2]);
    if (value < 0) {
        shell_error(sh, "\"%s\" is not a value for %s", argv[2], argv[1]);
        print_accepted(sh, id);
        return -EINVAL;
    }

    int rc = pacman_settings_set(id, (uint32_t)value);
    if (rc == -EINVAL) {
        shell_error(sh, "%s is out of range", argv[2]);
        print_accepted(sh, id);
        return rc;
    }
    if (rc) {
        shell_error(sh, "could not write %s to flash (%d)", argv[1], rc);
        return rc;
    }

    const pacman_setting *desc = pacman_settings_describe(id);
    if (desc->live) {
        apply();
        shell_print(sh, "%s is now %s", desc->name, argv[2]);
        return 0;
    }

    shell_print(sh, "%s will be %s from the next boot", desc->name, argv[2]);
    return 0;
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    if (strcmp(argv[1], "all") == 0) {
        for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
            if (pacman_settings_is_stored(id)) {
                pacman_settings_forget(id);
            }
        }
        shell_print(sh, "forgotten; the next boot takes what the firmware was built with");
        return 0;
    }

    int id = pacman_settings_find(argv[1]);
    if (id < 0) {
        shell_error(sh, "no setting called \"%s\"", argv[1]);
        return -ENOENT;
    }
    if (!pacman_settings_is_stored(id)) {
        shell_print(sh, "%s was not stored anyway", argv[1]);
        return 0;
    }

    int rc = pacman_settings_forget(id);
    if (rc) {
        shell_error(sh, "could not forget %s (%d)", argv[1], rc);
        return rc;
    }
    shell_print(sh, "%s will be what the firmware was built with from the next boot", argv[1]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* profiles                                                            */
/* ------------------------------------------------------------------ */

static int slot_arg(const struct shell *sh, const char *word) {
    char *end;
    long slot = strtol(word, &end, 10);

    if (*word == '\0' || *end != '\0' || !pacman_profile_slot_valid((int)slot)) {
        shell_error(sh, "there is no profile slot %s; they run 0 to %d", word,
                    PACMAN_PROFILE_SLOTS - 1);
        return -EINVAL;
    }
    return (int)slot;
}

/*
 * Every slot, used or not, because the page offering somewhere to save has to
 * know how many there are and which are free.  A free one prints "-" for its
 * name, the same way the schema spells a column with nothing in it.
 *
 * Which one the dongle is on is `current` rather than a fifth column here,
 * because a column is the half of this format that cannot be added to without
 * breaking the page that reads it.
 */
static int cmd_profile_list(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    for (int slot = 0; slot < PACMAN_PROFILE_SLOTS; slot++) {
        char name[PACMAN_PROFILE_NAME_LEN];
        int size = pacman_profile_name(slot, name, sizeof(name));

        if (size < 0) {
            shell_print(sh, "%d\t-\t0", slot);
            continue;
        }
        shell_print(sh, "%d\t%s\t%d", slot, name, size);
    }
    shell_print(sh, "end");
    return 0;
}

/* the one row `list` cannot carry: which slot everything typed here lands in */
static int cmd_profile_current(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int slot = pacman_profile_current();
    char name[PACMAN_PROFILE_NAME_LEN];

    if (pacman_profile_name(slot, name, sizeof(name)) < 0) {
        snprintf(name, sizeof(name), "%s", PACMAN_PROFILE_DEFAULT_NAME);
    }
    shell_print(sh, "%d\t%s", slot, name);
    shell_print(sh, "end");
    return 0;
}

static void print_entry(pacman_setting_id id, uint32_t value, void *ctx) {
    const struct shell *sh = ctx;
    char text[PACMAN_SETTING_VALUE_LEN];

    shell_print(sh, "%s\t%s", pacman_settings_describe(id)->name,
                pacman_settings_format(id, value, text, sizeof(text)));
}

static int cmd_profile_show(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }

    int rc = pacman_profile_read(slot, print_entry, (void *)sh);
    if (rc < 0) {
        shell_error(sh, "profile %d %s", slot, rc == -ENOENT ? "is empty" : "would not read");
        return rc;
    }
    shell_print(sh, "end");
    return 0;
}

static int cmd_profile_save(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }

    int rc = pacman_profile_save(slot, argv[2]);
    if (rc) {
        shell_error(sh, "profile %d would not be written (%d)", slot, rc);
        return rc;
    }
    shell_print(sh, "saved %d settings to profile %d as \"%s\"; the dongle is on it now",
                PACMAN_SETTING_COUNT, slot, argv[2]);
    return 0;
}

/*
 * Loading is moving: the profile being left is written down as the panel has
 * it before the new values land, so nothing is lost by walking away from a
 * profile somebody has been editing all afternoon.
 */
static int cmd_profile_load(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }
    if (slot == pacman_profile_current()) {
        shell_print(sh, "loaded profile %d; the dongle was already on it", slot);
        return 0;
    }

    bool reboot = false;
    int moved = pacman_profile_load(slot, &reboot, NULL);
    if (moved < 0) {
        shell_error(sh, "profile %d %s", slot, moved == -ENOENT ? "is empty" : "would not read");
        return moved;
    }

    apply();
    shell_print(sh, "loaded profile %d; %d settings moved%s", slot, moved,
                reboot ? ", and the layout needs a restart to show" : "");
    return 0;
}

static int cmd_profile_rename(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }

    int rc = pacman_profile_rename(slot, argv[2]);
    if (rc) {
        shell_error(sh, "profile %d %s", slot, rc == -ENOENT ? "is empty" : "would not be written");
        return rc;
    }
    shell_print(sh, "renamed profile %d to \"%s\"", slot, argv[2]);
    return 0;
}

static int cmd_profile_delete(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }

    int rc = pacman_profile_delete(slot);
    if (rc == -EPERM) {
        shell_error(sh, "profile %d cannot be forgotten; %s", slot,
                    slot == PACMAN_PROFILE_DEFAULT_SLOT
                        ? "it is the one the dongle falls back to"
                        : "it is the one the dongle is on");
        return rc;
    }
    if (rc) {
        shell_error(sh, "profile %d would not be forgotten (%d)", slot, rc);
        return rc;
    }
    shell_print(sh, "deleted profile %d", slot);
    return 0;
}

/*
 * An import arrives a value at a time, so it is held in RAM and written once:
 * `stage` with nothing after it throws away whatever a half-finished one left
 * behind, `stage <setting> <value>` adds to it, and `commit` is the single
 * flash write.  Nothing is on the panel until a `load` afterwards, which is
 * what lets a profile be imported without disturbing the dongle in front of
 * you.
 */
static int cmd_profile_stage(const struct shell *sh, size_t argc, char **argv) {
    if (argc == 1) {
        pacman_profile_stage_clear();
        shell_print(sh, "cleared what was staged");
        return 0;
    }
    if (argc != 3) {
        shell_error(sh, "stage takes a setting and a value, or nothing at all");
        return -EINVAL;
    }

    int id = pacman_settings_find(argv[1]);
    if (id < 0) {
        shell_error(sh, "no setting called \"%s\"", argv[1]);
        return -ENOENT;
    }

    int64_t value = pacman_settings_parse(id, argv[2]);
    if (value < 0) {
        shell_error(sh, "\"%s\" is not a value for %s", argv[2], argv[1]);
        return -EINVAL;
    }

    int rc = pacman_profile_stage(id, (uint32_t)value);
    if (rc) {
        shell_error(sh, "could not stage %s (%d)", argv[1], rc);
        return rc;
    }
    shell_print(sh, "staged %s %s", argv[1], argv[2]);
    return 0;
}

static int cmd_profile_commit(const struct shell *sh, size_t argc, char **argv) {
    ARG_UNUSED(argc);

    int slot = slot_arg(sh, argv[1]);
    if (slot < 0) {
        return slot;
    }

    int rc = pacman_profile_commit(slot, argv[2]);
    if (rc == -ENODATA) {
        shell_error(sh, "nothing has been staged to write");
        return rc;
    }
    if (rc == -EBUSY) {
        shell_error(sh, "profile %d is the one the dongle is on; write to another slot", slot);
        return rc;
    }
    if (rc) {
        shell_error(sh, "profile %d would not be written (%d)", slot, rc);
        return rc;
    }
    shell_print(sh, "saved profile %d as \"%s\"", slot, argv[2]);
    return 0;
}

static void setting_names(size_t idx, struct shell_static_entry *entry) {
    entry->syntax = idx < PACMAN_SETTING_COUNT ? pacman_settings_describe(idx)->name : NULL;
    entry->handler = NULL;
    entry->subcmd = NULL;
    entry->help = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(setting_name_list, setting_names);

SHELL_STATIC_SUBCMD_SET_CREATE(
    profile_subcmds,
    SHELL_CMD_ARG(list, NULL, "Every slot, tab separated, for the configurator page",
                  cmd_profile_list, 1, 0),
    SHELL_CMD_ARG(current, NULL, "Which slot the dongle is on, tab separated",
                  cmd_profile_current, 1, 0),
    SHELL_CMD_ARG(show, NULL, "show <slot> - what one profile holds, tab separated",
                  cmd_profile_show, 2, 0),
    SHELL_CMD_ARG(save, NULL,
                  "save <slot> <name> - the settings as they stand, and go on from there",
                  cmd_profile_save, 3, 0),
    SHELL_CMD_ARG(load, NULL, "load <slot> - move the dongle onto a profile", cmd_profile_load, 2,
                  0),
    SHELL_CMD_ARG(rename, NULL, "rename <slot> <name>", cmd_profile_rename, 3, 0),
    SHELL_CMD_ARG(delete, NULL, "delete <slot> - forget one the dongle is not on",
                  cmd_profile_delete, 2, 0),
    SHELL_CMD_ARG(stage, &setting_name_list,
                  "stage [<setting> <value>] - build a profile to commit, or clear one",
                  cmd_profile_stage, 1, 2),
    SHELL_CMD_ARG(commit, NULL,
                  "commit <slot> <name> - write what was staged, into a slot it is not on",
                  cmd_profile_commit, 3, 0),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    pacman_subcmds,
    SHELL_CMD_ARG(get, &setting_name_list, "Print every setting, or just the one named", cmd_get, 1,
                  1),
    SHELL_CMD_ARG(set, &setting_name_list, "set <setting> <value>", cmd_set, 3, 0),
    SHELL_CMD_ARG(reset, &setting_name_list, "reset <setting>|all - back to the built-in value",
                  cmd_reset, 2, 0),
    SHELL_CMD_ARG(schema, NULL, "Every setting, tab separated, for the configurator page",
                  cmd_schema, 1, 0),
    SHELL_CMD(profile, &profile_subcmds, "The set of settings the dongle is on, and the others",
              NULL),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(pacman, &pacman_subcmds, "What the Pac-Man dongle remembers", NULL);
