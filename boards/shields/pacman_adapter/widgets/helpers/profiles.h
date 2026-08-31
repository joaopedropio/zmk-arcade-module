/*
 * Pac-Man dongle - named sets of settings, kept on the dongle.
 *
 * A profile is every setting at once under a name, so a dongle can be moved
 * between two looks without the eighty-odd writes it took to build either.
 * They live in flash beside the settings and are read only when asked for,
 * which is why nothing here costs anything at boot.
 *
 * One flash key per profile, not one per setting in one - the opposite of what
 * settings.h does, and for the opposite reason.  A setting is written on its
 * own whenever somebody moves a slider; a profile is only ever written whole,
 * and eighty keys apiece would spend the storage partition on entries nothing
 * reads separately.  What settings.h warns about - a blob whose length is its
 * identity, so growing it throws away everything stored - is answered instead
 * by writing the hash of each setting's name next to its value: a profile from
 * an older firmware still loads, minus whatever has since been renamed, and a
 * setting added in the middle of the list moves nothing.
 *
 * An import arrives one value at a time over a shell, so the values are staged
 * in RAM and written once, rather than rewriting the whole profile eighty
 * times on the way in.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"

/* how many a name may hold, terminator included */
#define PACMAN_PROFILE_NAME_LEN 24

/* how many profiles the dongle has room for */
#define PACMAN_PROFILE_SLOTS CONFIG_PACMAN_PROFILE_SLOTS

/* whether the slot number is one of ours */
bool pacman_profile_slot_valid(int slot);

/*
 * The name in a slot, into buf, and how many settings it carries as the return
 * value.  -ENOENT where the slot is empty, which is what tells a list which
 * slots are free.  Both at once because either one costs a read of the whole
 * record, and a listing wants both of every slot.
 */
int pacman_profile_name(int slot, char *buf, size_t len);

/*
 * Every setting in the slot, one call each, in the order the list has them.
 * Reading through a callback keeps the record - the better part of a kilobyte
 * - off the caller's stack, which on a shell thread is worth having.
 */
typedef void (*pacman_profile_cb)(pacman_setting_id id, uint32_t value, void *ctx);
int pacman_profile_read(int slot, pacman_profile_cb fn, void *ctx);

/* snapshot every setting as it stands into the slot, under name */
int pacman_profile_save(int slot, const char *name);

/*
 * Write the slot's settings into the live ones, flash and all.  Returns how
 * many actually moved; *reboot, which may be NULL, says whether one of them
 * was a setting the widgets only read as they size themselves, and so whether
 * the dongle owes a restart before the screen matches the profile.
 */
int pacman_profile_load(int slot, bool *reboot);

int pacman_profile_rename(int slot, const char *name);
int pacman_profile_delete(int slot);

/* throw away whatever an interrupted import left staged */
void pacman_profile_stage_clear(void);

/* hold one value for the profile being imported */
int pacman_profile_stage(pacman_setting_id id, uint32_t value);

/* write everything staged into the slot under name, and clear the staging */
int pacman_profile_commit(int slot, const char *name);
