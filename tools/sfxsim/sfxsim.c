/*
 * Renders each of the dongle's sounds to a .wav, using the same synth the
 * firmware runs, so a tune can be listened to before it is flashed.
 *
 *   cc -I boards/shields/arcade_adapter/widgets/game \
 *      -o /tmp/sfxsim tools/sfxsim/sfxsim.c \
 *      boards/shields/arcade_adapter/widgets/game/arcade_sfx.c
 *   /tmp/sfxsim <out-dir> [sample-rate] [bass-floor-hz]
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arcade_sfx.h"

#define MAX_SECONDS 6

static const char *const NAMES[ARC_TUNE_COUNT] = {
    "connect", "disconnect",
};

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav(const char *path, const int16_t *samples, size_t count, uint32_t rate) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    uint32_t data_bytes = (uint32_t)count * 2u;

    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);        /* fmt chunk size */
    put16(f, 1);         /* PCM */
    put16(f, 1);         /* mono */
    put32(f, rate);
    put32(f, rate * 2);  /* byte rate */
    put16(f, 2);         /* block align */
    put16(f, 16);        /* bits */
    fwrite("data", 1, 4, f);
    put32(f, data_bytes);
    fwrite(samples, 2, count, f);
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    uint32_t rate = argc > 2 ? (uint32_t)atoi(argv[2]) : 16000;
    uint16_t floor_hz = argc > 3 ? (uint16_t)atoi(argv[3]) : 0;
    size_t cap = rate * MAX_SECONDS;
    int16_t *buf = malloc(cap * sizeof(int16_t));

    if (buf == NULL) {
        return 1;
    }

    for (int id = 0; id < ARC_TUNE_COUNT; id++) {
        arc_sfx_init(rate, 100, floor_hz);
        arc_sfx_play((arc_tune_id)id);

        size_t total = 0;
        while (total < cap) {
            size_t block = 256;
            if (total + block > cap) {
                block = cap - total;
            }
            arc_sfx_render(buf + total, block);
            total += block;
            /* a looping tune never ends: give it a couple of times round */
            bool looping = arc_sfx_loops((arc_tune_id)id);
            if ((!looping && !arc_sfx_sounding()) || (looping && total > rate * 4)) {
                break;
            }
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s.wav", dir, NAMES[id]);
        write_wav(path, buf, total, rate);
        printf("%-8s %5.2f s  %s\n", NAMES[id], (double)total / rate, path);
    }

    free(buf);
    return 0;
}
