/*
 * Pac-Man dongle - the speaker.
 *
 * A MAX98357A on the I2S port: the amplifier has no registers, it just plays
 * what arrives, so all this does is keep blocks of samples coming while
 * something is sounding and stop the clock when nothing is.
 *
 * The synth in game/pacman_sfx.c does the actual sound and knows nothing about
 * Zephyr; this file is the part that has to.  A thread renders one block ahead
 * of the I2S driver and blocks in i2s_write() when the driver is full, which
 * is what paces it - the game's own timer never waits on audio.
 *
 * The stream is stopped between sounds rather than left running on silence:
 * it drops the bit clock, which stops the amplifier hissing, and SD_MODE goes
 * with it where the board wires it up.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

#include "game/pacman_sfx.h"
#include "sound.h"

LOG_MODULE_DECLARE(pacman, LOG_LEVEL_INF);

#define AMP_NODE DT_NODELABEL(pacman_amp)

#if IS_ENABLED(CONFIG_PACMAN_SOUND) && DT_NODE_HAS_STATUS(AMP_NODE, okay)

#define SAMPLE_RATE CONFIG_PACMAN_SOUND_SAMPLE_RATE
#define FRAMES_PER_BLOCK 256
#define CHANNELS 2 /* the amplifier sums the two, so both carry the same */
#define BLOCK_BYTES (FRAMES_PER_BLOCK * CHANNELS * sizeof(int16_t))
#define BLOCK_COUNT 4
#define QUEUED_BEFORE_START 2 /* the driver wants one to play and one to follow */

K_MEM_SLAB_DEFINE_STATIC(sound_slab, BLOCK_BYTES, BLOCK_COUNT, 4);

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_PHANDLE(AMP_NODE, i2s));
static const struct gpio_dt_spec amp_enable = GPIO_DT_SPEC_GET_OR(AMP_NODE, sd_gpios, {0});

static K_SEM_DEFINE(wake, 0, 1);
static bool streaming;
static bool ready;

/*
 * Only the sound thread touches the synth.  The game's timer runs on the
 * display thread and says what it wants through these two: the tune it would
 * like next, and what the voice is actually doing, which is how the siren
 * knows it has been interrupted.  PM_TUNE_QUIET is "stop".
 */
#define PM_TUNE_QUIET (PM_TUNE_COUNT + 1)

static atomic_t pending = ATOMIC_INIT(PM_TUNE_NONE);
static atomic_t voice = ATOMIC_INIT(PM_TUNE_NONE);

static void amp_power(bool on) {
    if (amp_enable.port != NULL) {
        gpio_pin_set_dt(&amp_enable, on ? 1 : 0);
    }
}

/*
 * One block of the tune, in the frame layout the amplifier expects.  The
 * mono buffer is static rather than on the stack: it is half a kilobyte, only
 * this thread ever touches it, and a thread stack is not the place for it.
 */
static int16_t mono[FRAMES_PER_BLOCK];

static int write_block(void) {
    void *block;
    int err = k_mem_slab_alloc(&sound_slab, &block, K_MSEC(200));

    if (err < 0) {
        return err;
    }

    pm_sfx_render(mono, FRAMES_PER_BLOCK);

    int16_t *out = block;
    for (size_t i = 0; i < FRAMES_PER_BLOCK; i++) {
        out[2 * i] = mono[i];
        out[(2 * i) + 1] = mono[i];
    }

    err = i2s_write(i2s_dev, block, BLOCK_BYTES);
    if (err < 0) {
        k_mem_slab_free(&sound_slab, block);
    }
    return err;
}

static void stop_stream(void) {
    if (!streaming) {
        return;
    }
    /* drain rather than drop: the last block is the end of the tune */
    if (i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN) < 0) {
        (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
    }
    amp_power(false);
    streaming = false;
}

/* the pending request, applied to the synth; PM_TUNE_QUIET stops it */
static void take_request(void) {
    atomic_val_t asked = atomic_set(&pending, PM_TUNE_NONE);

    if (asked == PM_TUNE_QUIET) {
        pm_sfx_stop();
    } else if (asked != PM_TUNE_NONE) {
        pm_sfx_play((pm_tune_id)asked);
    }
    atomic_set(&voice, pm_sfx_playing());
}

static void sound_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (true) {
        k_sem_take(&wake, K_FOREVER);
        take_request();

        while (pm_sfx_playing() != PM_TUNE_NONE) {
            if (!streaming) {
                amp_power(true);
                for (int i = 0; i < QUEUED_BEFORE_START; i++) {
                    if (write_block() < 0) {
                        break;
                    }
                }
                if (i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START) < 0) {
                    LOG_ERR("i2s start failed");
                    amp_power(false);
                    pm_sfx_stop();
                    break;
                }
                streaming = true;
            }

            if (write_block() < 0) {
                break;
            }

            take_request(); /* and anything that arrived while it played */
            atomic_set(&voice, pm_sfx_playing());
        }

        atomic_set(&voice, PM_TUNE_NONE);
        stop_stream();
    }
}

K_THREAD_DEFINE(pacman_sound, 2048, sound_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);

static void request(pm_tune_id id) {
    if (!ready) {
        return;
    }
    atomic_set(&pending, id);
    k_sem_give(&wake);
}

void pacman_sound_init(void) {
    if (!device_is_ready(i2s_dev)) {
        LOG_ERR("i2s device is not ready; the dongle plays silently");
        return;
    }
    if (amp_enable.port != NULL) {
        if (gpio_pin_configure_dt(&amp_enable, GPIO_OUTPUT_INACTIVE) < 0) {
            LOG_ERR("amplifier enable pin is not configurable");
        }
    }

    struct i2s_config cfg = {
        .word_size = 16,
        .channels = CHANNELS,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER,
        .frame_clk_freq = SAMPLE_RATE,
        .mem_slab = &sound_slab,
        .block_size = BLOCK_BYTES,
        .timeout = 200,
    };

    int err = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
    if (err < 0) {
        LOG_ERR("i2s refused the configuration (%d); the dongle plays silently", err);
        return;
    }

    pm_sfx_init(SAMPLE_RATE, CONFIG_PACMAN_SOUND_VOLUME);
    ready = true;

    /*
     * The arcade sings when it is switched on, and so does this - which also
     * means the speaker says whether it is wired up before the game has even
     * started, without anyone having to lose a life to find out.
     */
    request(PM_TUNE_INTRO);
}

void pacman_sound_quiet(void) {
    if (!ready) {
        return;
    }
    atomic_set(&pending, PM_TUNE_QUIET);
    k_sem_give(&wake);
}

void pacman_sound_step(const pm_game *game) {
    static bool munch_b;
    static bool was_frightened;

    if (!ready) {
        return;
    }

    /* loudest first: only one voice, and the synth keeps the better tune */
    if (game->sfx & PM_SFX_DEATH) {
        request(PM_TUNE_DEATH);
    } else if (game->sfx & PM_SFX_CLEAR) {
        request(PM_TUNE_CLEAR);
    } else if (game->sfx & PM_SFX_GHOST) {
        request(PM_TUNE_GHOST);
    } else if (game->sfx & PM_SFX_START) {
        request(PM_TUNE_INTRO);
    } else if (game->sfx & PM_SFX_POWER) {
        request(PM_TUNE_POWER);
    } else if (game->sfx & PM_SFX_PELLET) {
        request(munch_b ? PM_TUNE_MUNCH_B : PM_TUNE_MUNCH_A);
        munch_b = !munch_b;
    }

    /*
     * The siren is a state rather than a moment: it starts when the ghosts
     * turn blue and has to be asked for again whenever a munch or a caught
     * ghost has taken the voice off it.
     */
    bool frightened = game->fright > 0;
    if (frightened && atomic_get(&voice) == PM_TUNE_NONE) {
        request(PM_TUNE_SIREN);
    }
    if (was_frightened && !frightened && atomic_get(&voice) == PM_TUNE_SIREN) {
        pacman_sound_quiet();
    }
    was_frightened = frightened;
}

#else /* no amplifier in the devicetree, or the sound is switched off */

void pacman_sound_init(void) {}
void pacman_sound_step(const pm_game *game) { ARG_UNUSED(game); }
void pacman_sound_quiet(void) {}

#endif
