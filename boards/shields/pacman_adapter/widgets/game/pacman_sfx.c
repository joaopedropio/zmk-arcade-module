/*
 * Pac-Man dongle - the voice (portable).
 *
 * A phase accumulator walks the square wave: the top bits of a 16.16 counter
 * are the position in the period, and the tone's duty cycle says how much of
 * it is high.  Frequency is worked out per sample, so a glide costs the same
 * as a flat note and the swoops come out of the same code as the melodies.
 *
 * Each tone is faded in and out over a millisecond.  Without it a jump from
 * one frequency to the next lands mid-wave and the amplifier clicks.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pacman_sfx.h"
#include "pacman_tunes.h"

#define PHASE_ONE (1u << 16) /* one period of the wave, in phase units */
#define FADE_MS 1

static uint32_t rate = 16000;
static int16_t amplitude;

static pm_tune_id current = PM_TUNE_NONE;
static uint8_t tone_index;
static uint32_t tone_samples;   /* how long the current tone lasts */
static uint32_t tone_elapsed;
static uint32_t phase;

static const pm_tune *tune_of(pm_tune_id id) {
    return (id < PM_TUNE_COUNT) ? &PM_TUNES[id] : NULL;
}

static void start_tone(void) {
    const pm_tune *tune = tune_of(current);

    if (tune == NULL || tone_index >= tune->count) {
        return;
    }
    tone_samples = (uint32_t)tune->tones[tone_index].ms * rate / 1000u;
    tone_elapsed = 0;
    phase = 0;
}

void pm_sfx_init(uint32_t sample_rate, uint8_t volume) {
    rate = sample_rate ? sample_rate : 16000;
    if (volume > 100) {
        volume = 100;
    }
    /* room at the top: a square wave at full scale is louder than it looks */
    amplitude = (int16_t)((INT16_MAX / 3) * volume / 100);
    current = PM_TUNE_NONE;
}

void pm_sfx_play(pm_tune_id id) {
    const pm_tune *tune = tune_of(id);
    const pm_tune *playing = tune_of(current);

    if (tune == NULL) {
        return;
    }
    if (playing != NULL && tune->priority < playing->priority) {
        return; /* something more important has the voice */
    }

    current = id;
    tone_index = 0;
    start_tone();
}

void pm_sfx_stop(void) { current = PM_TUNE_NONE; }

pm_tune_id pm_sfx_playing(void) { return current; }

bool pm_sfx_loops(pm_tune_id id) {
    const pm_tune *tune = tune_of(id);

    return tune != NULL && tune->loop;
}

size_t pm_sfx_render(int16_t *out, size_t count) {
    const pm_tune *tune = tune_of(current);
    size_t written = 0;

    for (size_t i = 0; i < count; i++) {
        out[i] = 0;
    }
    if (tune == NULL) {
        return 0;
    }

    while (written < count) {
        if (tone_index >= tune->count) {
            if (!tune->loop) {
                current = PM_TUNE_NONE;
                break;
            }
            tone_index = 0;
            start_tone();
        }

        const pm_tone *t = &tune->tones[tone_index];
        uint32_t left = tone_samples - tone_elapsed;
        uint32_t take = count - written;

        if (take > left) {
            take = left;
        }
        if (t->duty > 0 && t->from_hz > 0) {
            uint32_t fade = FADE_MS * rate / 1000u;
            uint32_t high = PHASE_ONE * t->duty / 100u;
            int32_t peak = (int32_t)amplitude * tune->gain / 100;

            for (uint32_t n = 0; n < take; n++) {
                uint32_t at = tone_elapsed + n;
                /* the glide, worked out where we are along the tone */
                uint32_t hz = t->from_hz +
                              (((int32_t)t->to_hz - (int32_t)t->from_hz) * (int32_t)at) /
                                  (int32_t)(tone_samples ? tone_samples : 1);
                uint32_t at_phase = phase % PHASE_ONE;
                int32_t sample;

                if (tune->wave == PM_WAVE_TRIANGLE) {
                    /* up for the first half of the period and back down the
                     * second, which is the same walk with a fold in it */
                    int32_t up = (int32_t)(at_phase * 2u);
                    if (at_phase >= PHASE_ONE / 2u) {
                        up = (int32_t)(2 * PHASE_ONE) - up;
                    }
                    sample = ((up - (int32_t)PHASE_ONE) * peak) / (int32_t)PHASE_ONE;
                } else {
                    sample = (at_phase < high) ? peak : -peak;
                }

                /* in over the first millisecond, out over the last */
                if (at < fade) {
                    sample = sample * (int32_t)at / (int32_t)fade;
                } else if (tone_samples - at < fade) {
                    sample = sample * (int32_t)(tone_samples - at) / (int32_t)fade;
                }
                out[written + n] = (int16_t)sample;
                phase += hz * PHASE_ONE / rate;
            }
        }

        written += take;
        tone_elapsed += take;
        if (tone_elapsed >= tone_samples) {
            tone_index++;
            start_tone();
        }
    }
    return written;
}
