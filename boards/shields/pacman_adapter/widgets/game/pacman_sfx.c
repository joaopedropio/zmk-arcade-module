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
    int32_t gain;        /* what the instrument is worth against the others, Q15 */
    bool triangle;       /* triangles for the wooden and breathy ones */
} pm_instrument;

/*
 * The last number is what the instrument is worth against the others.
 *
 * A pad is a bed for something else to sit on, but it holds its level for as
 * long as the key is down while a bell is already dying, and a chord of three
 * puts all three at that level on the same sample - in phase, because they are
 * six cents apart.  At equal weight that alone runs past full scale before a
 * bell has played a note, and the limiter then pulls the bell down with it.
 * Half weight puts the pad back underneath, where it can be heard rather than
 * heard over.
 */
static const pm_instrument INSTRUMENTS[] = {
    /* a struck bell: the 2.76 partial is what stops it sounding like an organ */
    [PM_INST_BELL] = {3, 900, 0, 260, 276, 100, ENV_ONE * 35 / 100, ENV_ONE, false},
    /* a marimba: an octave above, gone almost at once */
    [PM_INST_PLUCK] = {2, 260, 0, 90, 2, 1, ENV_ONE * 25 / 100, ENV_ONE, true},
    /* a pad: six cents apart, so the two drift in and out of phase */
    [PM_INST_PAD] = {140, 260, ENV_ONE * 72 / 100, 420, 1006, 1000, ENV_ONE * 85 / 100,
                     ENV_ONE * 45 / 100, true},
    /* and a soft tick */
    [PM_INST_NOISE] = {1, 130, 0, 50, 1, 1, 0, ENV_ONE, false},
    /*
     * The one the chirps use, and it is defined by what it leaves out.  A
     * triangle carries odd harmonics falling off as 1/n squared, so a marimba
     * note at 1300 Hz puts real energy at 4 and 6.6 kHz - the band the ear is
     * sharpest in and the band a small cone peaks in, which is what made the
     * first chirps sting.  A sine has none of them.  The 2 ms attack was the
     * other half, and the larger half: 32 samples from nothing to full scale
     * is a step, and a step is a click with a note behind it.  This takes
     * 110 ms to reach full level, which is slow enough that the note has no
     * discernible beginning - it is simply there, having arrived while you
     * were not listening for it.  What is left is one quiet octave above the
     * fundamental, a harmonic of it rather than a partial at odds with it,
     * for enough body that it does not sound like a test tone.
     */
    [PM_INST_CHIME] = {110, 950, 0, 460, 2, 1, ENV_ONE * 12 / 100, ENV_ONE, false},
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
static int32_t volume;      /* Q15: what the knob asks for, before the limiter */
static uint32_t floor_hz;   /* the lowest pitch the speaker is worth sending */
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

/*
 * What keeps the mix inside the DAC.
 *
 * Six voices can land on the same sample, so a chord runs well past full
 * scale even though a single tap sits nowhere near it.  Reserving a third of
 * the range for that moment is the cheap answer, and it costs every sound
 * 10 dB to protect the loudest instant of the loudest one - which is most of
 * why a pellet is inaudible on a small speaker.
 *
 * So instead: everything up to the knee passes through untouched, and what is
 * over it is bent into the range that is left.  The curve approaches full
 * scale without reaching it, so a chord leans on it and squashes where it
 * used to clip flat, while a marimba tap never touches it at all.
 */
#define LIMIT_KNEE (INT16_MAX / 2)

static int32_t soft_limit(int32_t x) {
    int32_t room = INT16_MAX - LIMIT_KNEE;
    int32_t sign = (x < 0) ? -1 : 1;
    int32_t mag = x * sign;
    int32_t over;

    if (mag <= LIMIT_KNEE) {
        return x;
    }
    over = mag - LIMIT_KNEE;

    return sign * (LIMIT_KNEE + (int32_t)(((int64_t)over * room) / (over + room)));
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
    v->loud = (((env * v->level) / ENV_ONE) * v->inst->gain) / ENV_ONE;

    a = (((a * env) / ENV_ONE) * v->level) / ENV_ONE;
    return (a * v->inst->gain) / ENV_ONE;
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
    uint32_t hz = n->hz;
    uint32_t step;

    /*
     * Up into the band the speaker can move air in.  Doubling is the one
     * transposition that leaves the tune recognisable - the pitch class does
     * not change, so a chord stays that chord, only voiced higher.  The
     * second bound keeps the doubling from walking a note into the aliasing
     * that lives just under half the sample rate.
     */
    while (hz != 0 && hz < floor_hz && hz * 2 < rate / 3) {
        hz *= 2;
    }

    step = (uint32_t)(((uint64_t)hz * PHASE_ONE) / rate);

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

void pm_sfx_init(uint32_t sample_rate, uint8_t vol, uint16_t low_hz) {
    rate = sample_rate ? sample_rate : 16000;
    pm_sfx_set_volume(vol);
    pm_sfx_set_bass_floor(low_hz);
    pm_sfx_stop();
}

/*
 * Both of these are read by the mixer on the sound thread while whoever is
 * turning the knob writes them from another.  A single aligned word either
 * way is all that changes, so the worst a race costs is one block rendered at
 * the old setting - not worth a lock between a human and a 16 kHz loop.
 */
void pm_sfx_set_volume(uint8_t vol) {
    if (vol > 100) {
        vol = 100;
    }
    /* 100 is unity: one voice at full tilt reaches full scale on its own, and
     * soft_limit() is what holds the chords that stack on top of it */
    volume = ENV_ONE * vol / 100;
}

void pm_sfx_set_bass_floor(uint16_t low_hz) { floor_hz = low_hz; }

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

        mix = (int32_t)(((int64_t)mix * volume) / ENV_ONE);
        out[i] = (int16_t)soft_limit(mix);
    }

    /* the tune can be over while its last notes are still ringing */
    for (int v = 0; v < VOICES; v++) {
        if (voices[v].on) {
            return count;
        }
    }
    return (current == PM_TUNE_NONE) ? 0 : count;
}
