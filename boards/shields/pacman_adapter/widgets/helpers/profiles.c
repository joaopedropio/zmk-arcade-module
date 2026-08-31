#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "profiles.h"
#include "settings.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PROFILE_VERSION 1

/*
 * A setting is written down by the hash of its name rather than by its
 * position in the list, because the list's order is load-bearing elsewhere -
 * the custom-theme colours have to come before the theme that derives from
 * them - so a setting does get inserted into the middle of it, and every
 * profile stored on every dongle would shift under one if it went by index.
 */
typedef struct {
    uint32_t key;
    uint32_t value;
} profile_entry;

typedef struct {
    uint8_t version;
    uint8_t reserved;
    uint16_t entries;
    char name[PACMAN_PROFILE_NAME_LEN];
    profile_entry entry[PACMAN_SETTING_COUNT];
} profile_record;

#define RECORD_HEAD offsetof(profile_record, entry)
#define RECORD_LEN(n) (RECORD_HEAD + (size_t)(n) * sizeof(profile_entry))

/*
 * Two records, because the two things they do overlap in time: an import
 * stages values across a dozen shell commands while a list or a show may read
 * a different slot in between.  Static rather than automatic - either one is
 * most of a kilobyte, and the shell thread's stack is not the place for it.
 */
static profile_record scratch;
static profile_record staged;
static bool staging;

static uint32_t hashes[PACMAN_SETTING_COUNT];
static bool hashes_ready;
static bool hashes_collide;

static uint32_t name_hash(const char *name) {
    uint32_t hash = 2166136261u;

    for (; *name; name++) {
        hash ^= (uint8_t)*name;
        hash *= 16777619u;
    }
    return hash;
}

/*
 * Two settings hashing alike would quietly load one under the other's name,
 * so it is checked once and refused rather than risked.  Thirty-two bits over
 * eighty-odd names makes it about a one-in-a-million build, and the developer
 * who writes that name sees this in the log the first time a profile is
 * touched - which is the moment to rename it.
 */
static bool hashes_usable(void) {
    if (hashes_ready) {
        return !hashes_collide;
    }
    hashes_ready = true;

    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        hashes[id] = name_hash(pacman_settings_describe(id)->name);
    }
    for (pacman_setting_id a = 0; a < PACMAN_SETTING_COUNT; a++) {
        for (pacman_setting_id b = a + 1; b < PACMAN_SETTING_COUNT; b++) {
            if (hashes[a] != hashes[b]) {
                continue;
            }
            LOG_ERR("Profiles are off: \"%s\" and \"%s\" hash alike; rename one",
                    pacman_settings_describe(a)->name, pacman_settings_describe(b)->name);
            hashes_collide = true;
        }
    }
    return !hashes_collide;
}

static int id_for_hash(uint32_t hash) {
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        if (hashes[id] == hash) {
            return id;
        }
    }
    return -1;
}

bool pacman_profile_slot_valid(int slot) { return slot >= 0 && slot < PACMAN_PROFILE_SLOTS; }

static void slot_key(int slot, char *buf, size_t len) {
    snprintf(buf, len, "pmprof/%d", slot);
}

static int read_cb(const char *key, size_t len, settings_read_cb read_fn, void *cb_arg,
                   void *param) {
    ARG_UNUSED(key);
    bool *found = param;

    if (len < RECORD_HEAD || len > sizeof(scratch)) {
        LOG_ERR("A stored profile is the wrong size (%zu); ignoring it", len);
        return 0;
    }
    ssize_t rc = read_fn(cb_arg, &scratch, len);
    if (rc < 0) {
        LOG_ERR("Failed to read a profile: %d", (int)rc);
        return 0;
    }
    if (scratch.version != PROFILE_VERSION || RECORD_LEN(scratch.entries) != len) {
        LOG_ERR("A stored profile is not one this firmware wrote; ignoring it");
        return 0;
    }
    scratch.name[PACMAN_PROFILE_NAME_LEN - 1] = '\0';
    *found = true;
    return 0;
}

/* the slot's record into scratch, or -ENOENT where the slot is empty */
static int fetch(int slot) {
    char key[24];
    bool found = false;

    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    if (!hashes_usable()) {
        return -EEXIST;
    }

    slot_key(slot, key, sizeof(key));
    int rc = settings_load_subtree_direct(key, read_cb, &found);
    if (rc) {
        return rc;
    }
    return found ? 0 : -ENOENT;
}

static int store(int slot, const profile_record *record) {
    char key[24];

    slot_key(slot, key, sizeof(key));
    int rc = settings_save_one(key, record, RECORD_LEN(record->entries));
    if (rc) {
        LOG_ERR("Failed to write profile %d: %d", slot, rc);
    }
    return rc;
}

static void set_name(profile_record *record, const char *name) {
    snprintf(record->name, sizeof(record->name), "%s", name);
}

int pacman_profile_name(int slot, char *buf, size_t len) {
    int rc = fetch(slot);

    if (rc) {
        return rc;
    }
    snprintf(buf, len, "%s", scratch.name);
    return (int)scratch.entries;
}

int pacman_profile_read(int slot, pacman_profile_cb fn, void *ctx) {
    int rc = fetch(slot);

    if (rc) {
        return rc;
    }

    /*
     * Walked in list order rather than record order so that whatever reads it
     * back - the shell, and through it the configurator - gets the settings in
     * the same order `pacman schema` names them.
     */
    int seen = 0;
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        for (uint16_t i = 0; i < scratch.entries; i++) {
            if (scratch.entry[i].key != hashes[id]) {
                continue;
            }
            fn(id, scratch.entry[i].value, ctx);
            seen++;
            break;
        }
    }
    return seen;
}

int pacman_profile_save(int slot, const char *name) {
    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    if (!hashes_usable()) {
        return -EEXIST;
    }

    /*
     * Built in staged rather than scratch: this is the same record an import
     * would have filled in, and going through one buffer means one definition
     * of what a profile about to be written looks like.
     */
    pacman_profile_stage_clear();
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        staged.entry[staged.entries].key = hashes[id];
        staged.entry[staged.entries].value = pacman_settings_get(id);
        staged.entries++;
    }
    return pacman_profile_commit(slot, name);
}

/*
 * Every value goes through pacman_settings_set(), so a profile written by an
 * older firmware cannot put a value the build no longer accepts on the panel:
 * the range check is the same one the shell does, and an entry that fails it
 * is left as it was rather than taking the whole load down.
 */
int pacman_profile_load(int slot, bool *reboot) {
    int rc = fetch(slot);

    if (reboot) {
        *reboot = false;
    }
    if (rc) {
        return rc;
    }

    int moved = 0;
    for (uint16_t i = 0; i < scratch.entries; i++) {
        int id = id_for_hash(scratch.entry[i].key);
        if (id < 0) {
            continue;
        }
        if (pacman_settings_get(id) == scratch.entry[i].value &&
            pacman_settings_is_stored(id)) {
            continue;
        }
        if (pacman_settings_set(id, scratch.entry[i].value) == 0) {
            moved++;
            if (reboot && !pacman_settings_describe(id)->live) {
                *reboot = true;
            }
        } else {
            LOG_WRN("Profile %d has no usable value for %s", slot,
                    pacman_settings_describe(id)->name);
        }
    }
    return moved;
}

int pacman_profile_rename(int slot, const char *name) {
    int rc = fetch(slot);

    if (rc) {
        return rc;
    }
    set_name(&scratch, name);
    return store(slot, &scratch);
}

int pacman_profile_delete(int slot) {
    char key[24];

    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    slot_key(slot, key, sizeof(key));
    return settings_delete(key);
}

void pacman_profile_stage_clear(void) {
    staged.version = PROFILE_VERSION;
    staged.reserved = 0;
    staged.entries = 0;
    staged.name[0] = '\0';
    staging = false;
}

int pacman_profile_stage(pacman_setting_id id, uint32_t value) {
    if (!hashes_usable()) {
        return -EEXIST;
    }
    if (!staging) {
        pacman_profile_stage_clear();
        staging = true;
    }

    for (uint16_t i = 0; i < staged.entries; i++) {
        if (staged.entry[i].key == hashes[id]) {
            staged.entry[i].value = value;
            return 0;
        }
    }
    if (staged.entries >= PACMAN_SETTING_COUNT) {
        return -ENOSPC;
    }
    staged.entry[staged.entries].key = hashes[id];
    staged.entry[staged.entries].value = value;
    staged.entries++;
    return 0;
}

int pacman_profile_commit(int slot, const char *name) {
    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    if (staged.entries == 0) {
        return -ENODATA;
    }

    staged.version = PROFILE_VERSION;
    staged.reserved = 0;
    set_name(&staged, name);

    int rc = store(slot, &staged);
    if (rc == 0) {
        pacman_profile_stage_clear();
    }
    return rc;
}
