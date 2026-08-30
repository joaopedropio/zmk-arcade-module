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
#include "helpers/settings.h"
#include "sound.h"

LOG_MODULE_DECLARE(pacman, LOG_LEVEL_INF);

#define AMP_NODE DT_NODELABEL(pacman_amp)

#if IS_ENABLED(CONFIG_PACMAN_SOUND) && DT_NODE_HAS_STATUS(AMP_NODE, okay)

#define FRAMES_PER_BLOCK 256
#define CHANNELS 2 /* the amplifier sums the two, so both carry the same */
#define BLOCK_BYTES (FRAMES_PER_BLOCK * CHANNELS * sizeof(int16_t))
/*
 * Eight blocks of 16 ms each.  The amplifier needs one every 16 ms and misses
 * nothing as long as the thread gets the CPU inside that; the depth is what
 * covers the times it does not, and the display thread repainting the whole
 * maze is 240x240 pixels down a 20 MHz SPI - tens of milliseconds during which
 * nothing else runs.  Four of them are handed over before the clock starts, so
 * a tune opens with 64 ms in hand.
 */
#define BLOCK_COUNT 8
#define QUEUED_BEFORE_START 4

K_MEM_SLAB_DEFINE_STATIC(sound_slab, BLOCK_BYTES, BLOCK_COUNT, 4);

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_PHANDLE(AMP_NODE, i2s));
static const struct gpio_dt_spec amp_enable = GPIO_DT_SPEC_GET_OR(AMP_NODE, sd_gpios, {0});

static K_SEM_DEFINE(wake, 0, 1);
static bool streaming;
static bool ready;

/*
 * Only the sound thread touches the synth.  Everything else - the battery
 * widget noticing a half come or go, the button muting - leaves the tune it
 * wants in an atomic and wakes the thread.  PM_TUNE_QUIET is "stop".
 */
#define PM_TUNE_QUIET (PM_TUNE_COUNT + 1)

static atomic_t pending = ATOMIC_INIT(PM_TUNE_NONE);

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

static bool start_stream(void) {
    amp_power(true);
    for (int i = 0; i < QUEUED_BEFORE_START; i++) {
        if (write_block() < 0) {
            break;
        }
    }
    if (i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START) < 0) {
        LOG_ERR("i2s start failed");
        amp_power(false);
        return false;
    }
    streaming = true;
    return true;
}

/*
 * An underrun is not just a gap: the driver gives up on the transfer and will
 * not take another block until it has been dropped back to ready.  Rather than
 * losing the rest of the tune, pick the stream up again from wherever the
 * synth has got to.
 */
static bool restart_stream(void) {
    (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
    streaming = false;
    return start_stream();
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
}

static void sound_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (true) {
        k_sem_take(&wake, K_FOREVER);
        take_request();

        while (pm_sfx_sounding()) {
            if (!streaming && !start_stream()) {
                pm_sfx_stop();
                break;
            }

            if (write_block() < 0 && !restart_stream()) {
                pm_sfx_stop();
                break;
            }

            take_request(); /* and anything that arrived while it played */
        }

        stop_stream();
    }
}

/*
 * Above the display thread, which ZMK runs at 5.  Filling a block is well
 * under a millisecond of the sixteen it buys, so the maze loses nothing it can
 * notice, while underneath the display it lost whole blocks every time the
 * screen was busy.
 */
K_THREAD_DEFINE(pacman_sound, 2048, sound_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(3), 0, 0);

/*
 * Everything that wants to make a noise comes through here, so the mute is a
 * single check rather than one at every caller.  Stopping is not a request and
 * does not pass this way, which is what lets the mute itself be heard - or
 * rather, not heard.
 */
static void request(pm_tune_id id) {
    if (!ready || pacman_settings_get_mute()) {
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
        .frame_clk_freq = pacman_settings_get(PACMAN_SETTING_SAMPLE_RATE),
        .mem_slab = &sound_slab,
        .block_size = BLOCK_BYTES,
        .timeout = 200,
    };

    int err = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
    if (err < 0) {
        LOG_ERR("i2s refused the configuration (%d); the dongle plays silently", err);
        return;
    }

    pm_sfx_init(pacman_settings_get(PACMAN_SETTING_SAMPLE_RATE),
                (uint8_t)pacman_settings_get(PACMAN_SETTING_VOLUME),
                (uint16_t)pacman_settings_get(PACMAN_SETTING_BASS_FLOOR));
    ready = true;

    /*
     * Nothing plays here.  Starting a game makes no sound of its own either,
     * so the first thing the speaker says is a half reporting in.
     */
}

void pacman_sound_quiet(void) {
    if (!ready) {
        return;
    }
    atomic_set(&pending, PM_TUNE_QUIET);
    k_sem_give(&wake);
}

void pacman_sound_connected(bool connected) {
    if (!IS_ENABLED(CONFIG_PACMAN_SOUND_CONNECT)) {
        return;
    }
    request(connected ? PM_TUNE_CONNECT : PM_TUNE_DISCONNECT);
}

void pacman_sound_set_volume(uint8_t volume) { pm_sfx_set_volume(volume); }

void pacman_sound_set_bass_floor(uint16_t floor_hz) { pm_sfx_set_bass_floor(floor_hz); }

void pacman_sound_set_mute(bool muted) {
    if (muted) {
        pacman_sound_quiet(); /* mid-tune */
        return;
    }
    /*
     * Unmuting has to make a sound or there is no way to tell it worked, and
     * the connect chirp is the one that already means "this thing is on".
     */
    request(PM_TUNE_CONNECT);
}

#else /* no amplifier in the devicetree, or the sound is switched off */

void pacman_sound_init(void) {}
void pacman_sound_quiet(void) {}
void pacman_sound_connected(bool connected) { ARG_UNUSED(connected); }
void pacman_sound_set_mute(bool muted) { ARG_UNUSED(muted); }
void pacman_sound_set_volume(uint8_t volume) { ARG_UNUSED(volume); }
void pacman_sound_set_bass_floor(uint16_t floor_hz) { ARG_UNUSED(floor_hz); }

#endif
