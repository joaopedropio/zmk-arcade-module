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

/* which slot the dongle is on, kept apart from the records it points into */
#define CURRENT_KEY "pmprof/current"

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
 * a different slot in between.  A switch needs both at once for the same
 * reason - the profile being left is built in one while the one being gone to
 * waits in the other.  Static rather than automatic: either is most of a
 * kilobyte, and the shell thread's stack is not the place for it.
 */
static profile_record scratch;
static profile_record staged;
static bool staging;

static uint8_t current_slot;
static bool current_ready;

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

static int commit_staged(int slot, const char *name);

static void set_name(profile_record *record, const char *name) {
    snprintf(record->name, sizeof(record->name), "%s", name);
}

/* ------------------------------------------------------------------ */
/* which slot the dongle is on                                         */
/* ------------------------------------------------------------------ */

static int current_read_cb(const char *key, size_t len, settings_read_cb read_fn, void *cb_arg,
                           void *param) {
    ARG_UNUSED(key);
    bool *found = param;
    uint8_t slot;

    if (len != sizeof(slot)) {
        LOG_ERR("The current profile is the wrong size (%zu); ignoring it", len);
        return 0;
    }
    if (read_fn(cb_arg, &slot, sizeof(slot)) < 0) {
        return 0;
    }
    current_slot = slot;
    *found = true;
    return 0;
}

/*
 * Read once and remembered, because everything the shell prints wants to know
 * which slot is current and a flash read per listed slot would be a read per
 * row.  A dongle that has never been told is on the default slot, which is
 * what a freshly flashed one is.
 */
static void current_load(void) {
    bool found = false;

    if (current_ready) {
        return;
    }
    current_ready = true;
    current_slot = PACMAN_PROFILE_DEFAULT_SLOT;
    settings_load_subtree_direct(CURRENT_KEY, current_read_cb, &found);
    if (found && !pacman_profile_slot_valid(current_slot)) {
        LOG_WRN("The stored current profile is slot %u, which this build has not got",
                current_slot);
        current_slot = PACMAN_PROFILE_DEFAULT_SLOT;
    }
}

int pacman_profile_current(void) {
    current_load();
    return current_slot;
}

/*
 * Walked from the current slot rather than from zero so the order is the one
 * somebody pressing the button sees: the slot after this one, then the one
 * after that.  fetch() is a flash read apiece, which is why this is only ever
 * called on a button press and not on anything that draws.
 */
int pacman_profile_next(void) {
    int here = pacman_profile_current();

    for (int step = 1; step < PACMAN_PROFILE_SLOTS; step++) {
        int slot = (here + step) % PACMAN_PROFILE_SLOTS;
        if (fetch(slot) == 0) {
            return slot;
        }
    }
    return here;
}

static int set_current(int slot) {
    uint8_t value = (uint8_t)slot;

    current_load();
    if (current_slot == value) {
        return 0;
    }
    int rc = settings_save_one(CURRENT_KEY, &value, sizeof(value));
    if (rc) {
        LOG_ERR("Failed to write the current profile: %d", rc);
        return rc;
    }
    current_slot = value;
    return 0;
}

/* ------------------------------------------------------------------ */

/* the live settings, written down in the slot under name */
static int snapshot(int slot, const char *name) {
    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    if (!hashes_usable()) {
        return -EEXIST;
    }

    /*
     * Built in staged rather than scratch: this is the same record an import
     * would have filled in, and going through one buffer means one definition
     * of what a profile about to be written looks like.  It also leaves
     * scratch alone, which is what lets a switch hold the profile it is going
     * to while it writes down the one it is leaving.
     */
    pacman_profile_stage_clear();
    for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
        staged.entry[staged.entries].key = hashes[id];
        staged.entry[staged.entries].value = pacman_settings_get(id);
        staged.entries++;
    }
    return commit_staged(slot, name);
}

/*
 * The default slot is written out of whatever the build left in the live
 * settings, so a dongle's first profile is the firmware it was flashed with
 * rather than a copy of it kept somewhere else.  It costs one flash write, on
 * the first boot of a dongle and never again.
 */
void pacman_profile_init(void) {
    current_load();

    if (fetch(PACMAN_PROFILE_DEFAULT_SLOT) == -ENOENT) {
        int rc = snapshot(PACMAN_PROFILE_DEFAULT_SLOT, PACMAN_PROFILE_DEFAULT_NAME);
        if (rc) {
            LOG_ERR("Could not write the default profile: %d", rc);
        }
    }

    /*
     * A slot emptied by a firmware that let it be, or by a storage partition
     * that lost it, would leave the dongle on nothing.  Falling back to the
     * default keeps the one rule everything else here relies on.
     */
    if (current_slot != PACMAN_PROFILE_DEFAULT_SLOT && fetch(current_slot) != 0) {
        LOG_WRN("Profile %u is gone; the dongle is on the default one", current_slot);
        set_current(PACMAN_PROFILE_DEFAULT_SLOT);
    }
}

int pacman_profile_name(int slot, char *buf, size_t len) {
    int rc = fetch(slot);

    if (rc) {
        return rc;
    }
    snprintf(buf, len, "%s", scratch.name);

    /*
     * The current slot's record is only rewritten as the dongle leaves it, so
     * its entries are as old as the last switch.  What it actually holds is
     * every live setting, which is what a listing has to say it holds.
     */
    return slot == pacman_profile_current() ? PACMAN_SETTING_COUNT : (int)scratch.entries;
}

int pacman_profile_read(int slot, pacman_profile_cb fn, void *ctx) {
    /*
     * Reading the current slot answers out of the live settings for the same
     * reason: they are the profile, and the record is only its name until the
     * dongle moves off it.  So an export of the profile you are on is what is
     * on the panel, not what it was when you arrived.
     */
    if (slot == pacman_profile_current()) {
        if (!hashes_usable()) {
            return -EEXIST;
        }
        for (pacman_setting_id id = 0; id < PACMAN_SETTING_COUNT; id++) {
            fn(id, pacman_settings_get(id), ctx);
        }
        return PACMAN_SETTING_COUNT;
    }

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
    int rc = snapshot(slot, name);

    if (rc) {
        return rc;
    }
    /*
     * The slot now holds exactly what the panel does, so the dongle is on it
     * whether it moved or not - and saying so is what makes copying a profile
     * before changing it do what it looks like it does.
     */
    return set_current(slot);
}

/*
 * Every value goes through pacman_settings_set(), so a profile written by an
 * older firmware cannot put a value the build no longer accepts on the panel:
 * the range check is the same one the shell does, and an entry that fails it
 * is left as it was rather than taking the whole load down.
 */
int pacman_profile_load(int slot, bool *reboot, pacman_profile_progress_cb progress) {
    char leaving[PACMAN_PROFILE_NAME_LEN];

    if (reboot) {
        *reboot = false;
    }
    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    if (slot == pacman_profile_current()) {
        return 0;
    }

    /*
     * The name of the profile being left is read before anything is written,
     * because reading it lands in scratch and the slot being gone to has to
     * end up there.  A default slot that has somehow gone missing is written
     * back under its own name rather than left out of the listing.
     */
    int rc = pacman_profile_name(current_slot, leaving, sizeof(leaving));
    if (rc < 0) {
        if (current_slot != PACMAN_PROFILE_DEFAULT_SLOT) {
            return rc;
        }
        snprintf(leaving, sizeof(leaving), "%s", PACMAN_PROFILE_DEFAULT_NAME);
    }

    /* the slot being gone to has to exist before the one being left is written */
    rc = fetch(slot);
    if (rc) {
        return rc;
    }

    /*
     * What the panel has now belongs to the profile being left, so it is
     * written down before the new values overwrite it.  Refusing the switch
     * when that write fails is the honest outcome: going on would lose the
     * profile somebody is standing on.
     */
    /*
     * Counted before the writing starts, so whoever is drawing a bar has a
     * denominator from the first step rather than a bar that grows its own
     * scale.  The snapshot is the first of them and each entry after is one
     * more; it is the slowest single step, which is why it is worth counting
     * rather than leaving the bar at nothing until it is over.
     */
    uint16_t total = scratch.entries + 1;

    rc = snapshot(current_slot, leaving);
    if (rc) {
        LOG_ERR("Could not write profile %u before leaving it: %d", current_slot, rc);
        return rc;
    }
    if (progress) {
        progress(1, total);
    }

    int moved = 0;
    for (uint16_t i = 0; i < scratch.entries; i++) {
        int id = id_for_hash(scratch.entry[i].key);
        if (progress) {
            progress(i + 2, total);
        }
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

    set_current(slot);
    return moved;
}

int pacman_profile_rename(int slot, const char *name) {
    int rc = fetch(slot);

    if (rc) {
        return rc;
    }
    /*
     * The current slot's record is being written anyway, so it is written from
     * the live settings rather than from entries the dongle has moved on from.
     */
    if (slot == pacman_profile_current()) {
        return snapshot(slot, name);
    }
    set_name(&scratch, name);
    return store(slot, &scratch);
}

int pacman_profile_delete(int slot) {
    char key[24];

    if (!pacman_profile_slot_valid(slot)) {
        return -EINVAL;
    }
    /*
     * Two refusals, both of the same rule: the dongle is always on a profile.
     * The default slot is what it falls back to, and the current one is where
     * the settings on the panel live.
     */
    if (slot == PACMAN_PROFILE_DEFAULT_SLOT || slot == pacman_profile_current()) {
        return -EPERM;
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

static int commit_staged(int slot, const char *name) {
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

/*
 * An import into the slot the dongle is on would be written and never read:
 * that slot answers out of the live settings, and the record goes back to what
 * the panel holds the moment the dongle leaves it.  Refusing is the honest
 * answer - the values have somewhere else to go, and loading them afterwards
 * is what puts them on the panel.
 */
int pacman_profile_commit(int slot, const char *name) {
    if (pacman_profile_slot_valid(slot) && slot == pacman_profile_current()) {
        return -EBUSY;
    }
    return commit_staged(slot, name);
}
