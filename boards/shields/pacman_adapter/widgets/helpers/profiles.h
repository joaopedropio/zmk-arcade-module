/*
 * Pac-Man dongle - named sets of settings, kept on the dongle.
 *
 * A profile is every setting at once under a name, so a dongle can be moved
 * between two looks without the eighty-odd writes it took to build either.
 *
 * The dongle is always on one.  Slot 0 is written the first time a dongle
 * boots, out of whatever the firmware was built with, and neither it nor
 * whichever slot is current can be deleted - so there is always somewhere to
 * be.  It is a profile in every other respect, renaming included.
 *
 * Being on a profile means the live settings are that profile.  `pacman set`
 * reaches flash the moment it is typed, and what it writes is the profile the
 * dongle is on: there is no unsaved half of a profile to lose and nothing to
 * remember to save.  Somebody who wants to keep a look before changing it
 * copies it into another slot and carries on from the copy.
 *
 * That is also why the current slot's record is only rewritten as the dongle
 * leaves it.  Holding it in step would mean seven hundred bytes of flash for
 * every colour somebody drags a slider over - forty-six of them for one
 * preset - to write down values that are already in flash a key at a time.  So
 * the record keeps the name, name() and read() answer for the current slot out
 * of the live settings instead, and a switch snapshots the profile being left
 * before it loads the one being gone to.
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
 * times on the way in.  It is the one thing here that leaves the panel alone:
 * a profile brought in from a file is somewhere to go, not somewhere the
 * dongle has gone.
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

/* the one a dongle is flashed with, and the one it falls back to */
#define PACMAN_PROFILE_DEFAULT_SLOT 0
#define PACMAN_PROFILE_DEFAULT_NAME "Default"

/*
 * Write the default slot if flash has never held one and settle which slot the
 * dongle is on.  configure() calls it once, after the build's values have
 * filled in whatever flash had nothing to say about - so a dongle's first
 * profile is exactly what it was flashed with.
 */
void pacman_profile_init(void);

/* whether the slot number is one of ours */
bool pacman_profile_slot_valid(int slot);

/* the slot the dongle is on; there is always one */
int pacman_profile_current(void);

/*
 * The next slot along that has a profile in it, wrapping, or the current one
 * where it is the only one there is.  This is what the action button steps
 * through, so an empty slot is skipped rather than stopping the cycle dead.
 */
int pacman_profile_next(void);

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

/*
 * Snapshot every setting as it stands into the slot, under name, and go on
 * from there: the slot now holds exactly what the panel does, so it is the
 * one the dongle is on.  That is how a look is copied before it is changed.
 */
int pacman_profile_save(int slot, const char *name);

/*
 * Move the dongle onto the slot: the profile being left is written down as the
 * panel has it, then the slot's settings become the live ones, flash and all.
 * Returns how many actually moved, or 0 for the slot the dongle is already on;
 * *reboot, which may be NULL, says whether one of them was a setting the
 * widgets only read as they size themselves, and so whether the dongle owes a
 * restart before the screen matches the profile.
 */
int pacman_profile_load(int slot, bool *reboot);

int pacman_profile_rename(int slot, const char *name);

/* -EPERM for the default slot and for the one the dongle is on */
int pacman_profile_delete(int slot);

/* throw away whatever an interrupted import left staged */
void pacman_profile_stage_clear(void);

/* hold one value for the profile being imported */
int pacman_profile_stage(pacman_setting_id id, uint32_t value);

/*
 * Write everything staged into the slot under name, and clear the staging.
 * -EBUSY for the slot the dongle is on: that one answers out of the live
 * settings, so a record written there would be thrown away unread.
 */
int pacman_profile_commit(int slot, const char *name);
