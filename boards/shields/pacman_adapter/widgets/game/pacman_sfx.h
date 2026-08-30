/*
 * Pac-Man dongle - the voice (portable).
 *
 * One square wave, a list of tones to play through it, and nothing that knows
 * about Zephyr: the same code renders samples for the dongle's amplifier and
 * for the .wav files tools/sfxsim writes, so a tune can be listened to before
 * it is flashed.
 *
 * The caller pulls samples rather than the synth pushing them - pm_sfx_render()
 * fills whatever buffer the I2S driver wants next - and a tune plays until it
 * runs out, unless something with a higher priority takes the voice off it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t from_hz; /* the frequency it starts at */
    uint16_t to_hz;   /* and glides to across the tone; the same for a plain note */
    uint16_t ms;
    uint8_t duty; /* percent of the period the wave is high; 0 is a rest */
} pm_tone;

typedef struct {
    const pm_tone *tones;
    uint8_t count;
    bool loop;
    uint8_t priority; /* a tune only interrupts one no louder than itself */
} pm_tune;

typedef enum {
    PM_TUNE_INTRO,
    PM_TUNE_MUNCH_A,
    PM_TUNE_MUNCH_B,
    PM_TUNE_POWER,
    PM_TUNE_SIREN,
    PM_TUNE_GHOST,
    PM_TUNE_DEATH,
    PM_TUNE_CLEAR,
    PM_TUNE_COUNT,
    PM_TUNE_NONE = PM_TUNE_COUNT,
} pm_tune_id;

/* sample_rate in Hz, volume 0-100 */
void pm_sfx_init(uint32_t sample_rate, uint8_t volume);

/* starts a tune if the voice is free or busy with something quieter */
void pm_sfx_play(pm_tune_id id);
void pm_sfx_stop(void);

/* which tune has the voice, PM_TUNE_NONE when it is silent */
pm_tune_id pm_sfx_playing(void);

/* whether a tune runs until something stops it, which the siren does */
bool pm_sfx_loops(pm_tune_id id);

/*
 * Fills count mono 16-bit samples and returns how many of them are sound: 0
 * when nothing is playing (the buffer is silence either way, so it can be
 * handed to the amplifier regardless).
 */
size_t pm_sfx_render(int16_t *out, size_t count);
