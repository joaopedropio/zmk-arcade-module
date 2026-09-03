/*
 * Arcade dongle - the voice (portable).
 *
 * A buzzer can only be on or off at one pitch.  What is wired to the dongle
 * now is a DAC and an amplifier, which can put any shape it likes into the
 * speaker, so this is a small polyphonic synth rather than a tone generator:
 * four voices, each an instrument with its own timbre and envelope, playing a
 * score.  Chords, a melody over a held pad, a bell that rings and decays -
 * none of which a buzzer can do at all.
 *
 * Nothing here knows about Zephyr: the same code fills the amplifier's buffers
 * on the dongle and writes the .wav files tools/sfxsim renders, so a sound can
 * be listened to before it is flashed.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The instruments.  Each is a pair of oscillators and an envelope: what
 * separates a bell from a marimba is mostly how fast it dies away and how far
 * out of tune its second partial is.
 */
typedef enum {
    ARC_INST_BELL,   /* sine plus an inharmonic partner, long ringing decay */
    ARC_INST_PLUCK,  /* triangle plus an octave, short decay - a marimba */
    ARC_INST_PAD,    /* two triangles a breath apart, slow in and slow out */
    ARC_INST_NOISE,  /* filtered noise with a fast decay, for a soft tick */
    ARC_INST_CHIME,  /* a plain sine eased in and out, with a quiet octave for body */
} arc_inst;

typedef struct {
    uint16_t at_ms; /* when it starts, from the top of the tune */
    uint16_t hz;    /* what pitch, ignored by the noise instrument */
    uint16_t ms;    /* how long the key is held - the release runs on past it */
    uint8_t inst;
    uint8_t level; /* how hard it is struck, percent */
} arc_note;

typedef struct {
    const arc_note *notes;
    uint8_t count;
    uint16_t ms; /* the whole tune, release included */
    bool loop;
    uint8_t priority; /* a tune only interrupts one no more important */
} arc_tune;

typedef enum {
    ARC_TUNE_CONNECT,
    ARC_TUNE_DISCONNECT,
    ARC_TUNE_COUNT,
    ARC_TUNE_NONE = ARC_TUNE_COUNT,
} arc_tune_id;

/*
 * sample_rate in Hz, volume 0-100, and the lowest pitch the speaker is worth
 * sending.
 *
 * A small speaker radiates almost nothing below its own resonance - a 20 mm
 * driver is 20 dB down at 300 Hz - so the pads underneath these tunes arrive
 * as silence that still costs mixing headroom.  Any note below floor_hz is
 * doubled until it clears it, which keeps the tune's intervals and moves the
 * energy into the band the cone can actually move air in.  0 leaves the
 * scores alone, which is what a speaker with a real box behind it wants.
 */
void arc_sfx_init(uint32_t sample_rate, uint8_t volume, uint16_t floor_hz);

/* both settable while it plays; see the note in arcade_sfx.c */
void arc_sfx_set_volume(uint8_t volume);
void arc_sfx_set_bass_floor(uint16_t floor_hz);

/* starts a tune if the voice is free or busy with something less important */
void arc_sfx_play(arc_tune_id id);
void arc_sfx_stop(void);

/* which tune is sounding, ARC_TUNE_NONE when nothing is */
arc_tune_id arc_sfx_playing(void);

/* true while anything is still sounding, including notes ringing on after
 * the tune itself has finished */
bool arc_sfx_sounding(void);

/* whether a tune runs until something stops it, which the fright pad does */
bool arc_sfx_loops(arc_tune_id id);

/*
 * Fills count mono 16-bit samples and returns how many carry sound: 0 when
 * nothing is playing (the buffer is silence either way, so it can go to the
 * amplifier regardless).
 */
size_t arc_sfx_render(int16_t *out, size_t count);
