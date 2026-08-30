/*
 * Pac-Man dongle - the voice (portable).
 *
 * Four voices, mixed.  A note takes the quietest free voice, sets two
 * oscillators running from one sine table and walks an envelope: in, down to
 * whatever it holds at, then out when the key lifts.  Every instrument here is
 * those same parts with different numbers - a bell is a fast attack and a long
 * decay with its second partial deliberately out of tune, a pad is a slow
 * attack and two oscillators a breath apart, and the beating between them is
 * what makes it sound warm rather than electronic.
 *
 * All integer: phases are 16.16 fixed point, envelopes are Q15 multipliers.
 * At 16 kHz and four voices this is a few thousand multiplies a second.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pacman_sfx.h"
#include "pacman_tunes.h"

#define VOICES 6
#define PHASE_BITS 16
#define PHASE_ONE (1u << PHASE_BITS)
#define ENV_ONE 32768 /* Q15 */

/* attack, decay and release in milliseconds; sustain in Q15 */
typedef struct {
    uint16_t attack_ms;
    uint16_t decay_ms;
    int32_t sustain;
    uint16_t release_ms;
    uint16_t partial_num; /* the second oscillator, as a ratio of the first */
    uint16_t partial_den;
    int32_t partial_mix; /* how much of it, Q15 */
    bool triangle;       /* triangles for the wooden and breathy ones */
} pm_instrument;

static const pm_instrument INSTRUMENTS[] = {
    /* a struck bell: the 2.76 partial is what stops it sounding like an organ */
    [PM_INST_BELL] = {3, 900, 0, 260, 276, 100, ENV_ONE * 35 / 100, false},
    /* a marimba: an octave above, gone almost at once */
    [PM_INST_PLUCK] = {2, 260, 0, 90, 2, 1, ENV_ONE * 25 / 100, true},
    /* a pad: six cents apart, so the two drift in and out of phase */
    [PM_INST_PAD] = {140, 260, ENV_ONE * 72 / 100, 420, 1006, 1000, ENV_ONE * 85 / 100, true},
    /* and a soft tick */
    [PM_INST_NOISE] = {1, 130, 0, 50, 1, 1, 0, false},
};

static const int16_t SINE[256] = PM_SINE_TABLE;

typedef struct {
    bool on;
    const pm_instrument *inst;
    uint32_t phase, step;
    uint32_t phase2, step2;
    uint32_t age;    /* samples since the note started */
    uint32_t held;   /* samples the key stays down */
    uint32_t attack, decay, release; /* in samples, worked out once */
    int32_t level;   /* how hard it was struck, Q15 */
    int32_t last;    /* the envelope when the key lifted */
    int32_t loud;    /* what it is contributing right now, for voice stealing */
    uint32_t noise;  /* LFSR state */
} pm_voice;

static uint32_t rate = 16000;
static int32_t volume;
static pm_voice voices[VOICES];

static pm_tune_id current = PM_TUNE_NONE;
static uint32_t elapsed;  /* samples since the tune started */
static uint8_t next_note; /* the first note not yet started */

static const pm_tune *tune_of(pm_tune_id id) {
    return (id < PM_TUNE_COUNT) ? &PM_TUNES[id] : NULL;
}

static uint32_t ms_to_samples(uint32_t ms) { return ms * rate / 1000u; }

static int32_t osc(uint32_t phase, bool triangle) {
    if (!triangle) {
        return SINE[(phase >> 8) & 0xFF];
    }
    /* up for half the period and back down the other half */
    int32_t up = (int32_t)((phase & (PHASE_ONE - 1)) * 2u);
    if (up >= (int32_t)PHASE_ONE) {
        up = (int32_t)(2 * PHASE_ONE) - up;
    }
    return ((up * 2) - (int32_t)PHASE_ONE) / 2;
}

/* where the voice is in its envelope, Q15, and false once it is finished */
static bool envelope(pm_voice *v, int32_t *out) {
    const pm_instrument *in = v->inst;
    uint32_t attack = v->attack;
    uint32_t decay = v->decay;
    uint32_t release = v->release;
    int32_t env;

    if (v->age < v->held) {
        if (v->age < attack) {
            /* on the way in.  This is zero at the first sample, which is the
             * start of the note and not the end of it - only a note that has
             * decayed away is finished */
            env = attack ? (int32_t)((ENV_ONE * (int32_t)v->age) / (int32_t)attack) : ENV_ONE;
        } else if (v->age < attack + decay) {
            uint32_t into = v->age - attack;
            env = ENV_ONE - (((ENV_ONE - in->sustain) * (int32_t)into) / (int32_t)decay);
            if (env <= 0) {
                return false; /* struck, and rung out, with the key still down */
            }
        } else {
            env = in->sustain;
            if (env <= 0) {
                return false;
            }
        }
        v->last = env;
    } else {
        uint32_t since = v->age - v->held;
        if (since >= release || v->last <= 0) {
            return false;
        }
        env = v->last - ((v->last * (int32_t)since) / (int32_t)release);
    }

    *out = env;
    return true;
}

static int32_t voice_sample(pm_voice *v) {
    int32_t env;

    if (!envelope(v, &env)) {
        v->on = false;
        return 0;
    }

    int32_t a;
    if (v->inst == &INSTRUMENTS[PM_INST_NOISE]) {
        /* a 31-bit maximal LFSR, low bits only, which sounds softer */
        v->noise = (v->noise >> 1) ^ (uint32_t)(-(int32_t)(v->noise & 1u) & 0xA3000000u);
        a = (int32_t)(v->noise & 0x7FFF) - 0x4000;
    } else {
        a = osc(v->phase, v->inst->triangle);
        a += (osc(v->phase2, v->inst->triangle) * v->inst->partial_mix) / ENV_ONE;
    }

    v->phase += v->step;
    v->phase2 += v->step2;
    v->age++;
    v->loud = (env * v->level) / ENV_ONE;

    return (((a * env) / ENV_ONE) * v->level) / ENV_ONE;
}

static void start_note(const pm_note *n) {
    pm_voice *pick = &voices[0];

    /*
     * A free voice, or else the quietest one.  Stealing the oldest would take
     * the chord a pad is holding underneath everything and leave the bell that
     * has almost rung out, which is backwards: what is least missed is
     * whatever is contributing least right now.
     */
    for (int i = 0; i < VOICES; i++) {
        if (!voices[i].on) {
            pick = &voices[i];
            break;
        }
        if (pick->on && voices[i].loud < pick->loud) {
            pick = &voices[i];
        }
    }

    const pm_instrument *in = &INSTRUMENTS[n->inst];
    uint32_t step = (uint32_t)(((uint64_t)n->hz * PHASE_ONE) / rate);

    pick->on = true;
    pick->inst = in;
    pick->phase = 0;
    pick->step = step;
    pick->phase2 = 0;
    pick->step2 = (uint32_t)(((uint64_t)step * in->partial_num) / in->partial_den);
    pick->age = 0;
    pick->held = ms_to_samples(n->ms);
    pick->attack = ms_to_samples(in->attack_ms);
    pick->decay = ms_to_samples(in->decay_ms);
    pick->release = ms_to_samples(in->release_ms);
    pick->level = ENV_ONE * n->level / 100;
    pick->last = ENV_ONE;
    pick->loud = pick->level;
    pick->noise = 0x13579BDFu;
}

void pm_sfx_init(uint32_t sample_rate, uint8_t vol) {
    rate = sample_rate ? sample_rate : 16000;
    if (vol > 100) {
        vol = 100;
    }
    /* six voices can land on the same sample, so leave them room */
    volume = (INT16_MAX / 3) * vol / 100;
    pm_sfx_stop();
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
    elapsed = 0;
    next_note = 0;
}

void pm_sfx_stop(void) {
    current = PM_TUNE_NONE;
    for (int i = 0; i < VOICES; i++) {
        voices[i].on = false;
    }
}

pm_tune_id pm_sfx_playing(void) { return current; }

bool pm_sfx_sounding(void) {
    if (current != PM_TUNE_NONE) {
        return true;
    }
    for (int i = 0; i < VOICES; i++) {
        if (voices[i].on) {
            return true;
        }
    }
    return false;
}

bool pm_sfx_loops(pm_tune_id id) {
    const pm_tune *tune = tune_of(id);

    return tune != NULL && tune->loop;
}

size_t pm_sfx_render(int16_t *out, size_t count) {
    const pm_tune *tune = tune_of(current);
    uint32_t length = tune ? ms_to_samples(tune->ms) : 0;

    for (size_t i = 0; i < count; i++) {
        if (tune != NULL) {
            /* everything that has come due since the last sample */
            while (next_note < tune->count &&
                   ms_to_samples(tune->notes[next_note].at_ms) <= elapsed) {
                start_note(&tune->notes[next_note]);
                next_note++;
            }

            if (++elapsed >= length) {
                if (tune->loop) {
                    elapsed = 0;
                    next_note = 0;
                } else {
                    current = PM_TUNE_NONE;
                    tune = NULL;
                }
            }
        }

        int32_t mix = 0;
        for (int v = 0; v < VOICES; v++) {
            if (voices[v].on) {
                mix += voice_sample(&voices[v]);
            }
        }

        mix = (mix * volume) / INT16_MAX;
        if (mix > INT16_MAX) {
            mix = INT16_MAX;
        } else if (mix < -INT16_MAX) {
            mix = -INT16_MAX;
        }
        out[i] = (int16_t)mix;
    }

    /* the tune can be over while its last notes are still ringing */
    for (int v = 0; v < VOICES; v++) {
        if (voices[v].on) {
            return count;
        }
    }
    return (current == PM_TUNE_NONE) ? 0 : count;
}
