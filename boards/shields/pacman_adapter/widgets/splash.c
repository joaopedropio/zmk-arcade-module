/*
 * Pac-Man dongle - splash screen.
 *
 * What the panel shows while ZMK finishes coming up.  splash-style picks
 * between two of them: the drawn one - the wordmark, Pac-Man about to run into
 * a ghost, and who to blame, every colour of it a setting - and the picture in
 * splash_image.h, which is a run-length poster with a palette of its own.
 *
 * The status screen's timer calls print_splash() every tick and it draws once
 * - the panel keeps what it was given, so there is nothing to hold up until
 * the game or the menu takes over and the buffers are handed back.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include <zmk/display.h>

#include "splash.h"
#include "helpers/display.h"
#include "pacman_art.h"
#include "splash_image.h"

/* the wordmark: six 10x13 glyphs, three times up, centred on the panel - six
 * of them at this pitch come to 185 of the 240, which is where the 27 is */
#define WORDMARK_FONT_W 10
#define WORDMARK_FONT_H 13
static const uint16_t wordmark_scale = 3;
static const uint16_t wordmark_x = 27;
static const uint16_t wordmark_y = 44;
static const uint16_t wordmark_pitch = 31; /* a pixel of air between glyphs */

/* the chase below it: Pac-Man, three pellets, and what is waiting for him */
static const uint16_t chase_y = 128;
static const uint16_t pac_x = 36;
static const uint16_t pellet_x = 96;
static const uint16_t pellet_pitch = 20;
static const uint16_t pellet_size = 6;
static const uint16_t ghost_x = 168;

static const uint16_t created_by_x = 64;
static const uint16_t created_by_y = 200;
static const uint16_t created_by_scale = 2;

static uint16_t *buf_glyph;  /* one scaled character, the biggest thing drawn */
static uint16_t *buf_sprite; /* one row of Pac-Man or of the ghost */
static uint8_t *buf_pellet;
static uint16_t *buf_row;    /* one row of the picture, for the image style */

static bool initialized_splash = false;

static void print_wordmark(void) {
    uint16_t colors[4] = {
        get_splash_logo_multicolor_0(),
        get_splash_logo_multicolor_1(),
        get_splash_logo_multicolor_2(),
        get_splash_logo_multicolor_3(),
    };
    Character word[] = {CHAR_P, CHAR_A, CHAR_C, CHAR_M, CHAR_A, CHAR_N};

    for (uint8_t i = 0; i < ARRAY_SIZE(word); i++) {
        print_bitmap_multicolor(buf_glyph, word[i], wordmark_x + (i * wordmark_pitch), wordmark_y,
                                wordmark_scale, colors, FONT_SIZE_10x13);
    }
}

static void print_chase(void) {
    uint16_t ghost_colors[4] = {
        get_splash_bg_color(),            /* around the ghost */
        get_splash_logo_multicolor_3(),   /* its body */
        get_splash_created_by_color(),    /* the whites of its eyes */
        get_splash_bg_color(),            /* and the pupils, looking his way */
    };
    uint16_t ghost_y = chase_y + ((splash_pac_height - splash_ghost_height) / 2);
    uint16_t pellet_y = chase_y + ((splash_pac_height - pellet_size) / 2);

    for (uint16_t i = 0; i < splash_pac_height; i++) {
        render_bitmap(buf_sprite, splash_pac[i], pac_x, chase_y + i, splash_pac_width, 1, 1,
                      get_splash_logo_color(), get_splash_bg_color());
    }
    for (uint8_t i = 0; i < 3; i++) {
        render_filled_rectangle(buf_pellet, (uint8_t)(pellet_x + (i * pellet_pitch)),
                                (uint8_t)pellet_y, (uint8_t)pellet_size, (uint8_t)pellet_size);
    }
    for (uint16_t i = 0; i < splash_ghost_height; i++) {
        render_bitmap_multicolor(buf_sprite, splash_ghost[i], ghost_x, ghost_y + i,
                                 splash_ghost_width, 1, 1, ghost_colors);
    }
}

static void print_created_by(void) {
    Character created_chars[] = {
        CHAR_C, CHAR_R, CHAR_E, CHAR_A, CHAR_T, CHAR_E, CHAR_D,
    };
    Character by_chars[] = {
        CHAR_B, CHAR_Y, CHAR_COLON, CHAR_P, CHAR_I, CHAR_O,
    };
    uint16_t char_gap_pixels = 2;
    uint16_t by_x = created_by_x + 60;

    print_string(buf_glyph, created_chars, created_by_x, created_by_y, created_by_scale,
                 get_splash_created_by_color(), get_splash_bg_color(), FONT_SIZE_3x5,
                 char_gap_pixels, ARRAY_SIZE(created_chars));
    print_string(buf_glyph, by_chars, by_x, created_by_y, created_by_scale,
                 get_splash_created_by_color(), get_splash_bg_color(), FONT_SIZE_3x5,
                 char_gap_pixels, ARRAY_SIZE(by_chars));
}

/*
 * The picture, straight out of splash_image.h.  Its palette is the art's own
 * rather than the splash colour settings: it is a photograph of a poster, and
 * eight colours picked to look like one do not survive being reassigned.
 */
static void print_image(void) {
    uint16_t palette[SPLASH_IMAGE_COLORS];

    for (uint8_t i = 0; i < SPLASH_IMAGE_COLORS; i++) {
        palette[i] = rgb888_to_rgb565(splash_image_palette[i]);
    }
    render_indexed_image(buf_row, splash_image_runs, ARRAY_SIZE(splash_image_runs), palette, 0, 0,
                         SPLASH_IMAGE_WIDTH, SPLASH_IMAGE_HEIGHT);
}

void print_splash(void) {
    if (initialized_splash) {
        return;
    }

    if (get_splash_style() == SPLASH_STYLE_IMAGE) {
        print_image();
        initialized_splash = true;
        return;
    }

    clear_screen(get_splash_bg_color());
    print_wordmark();
    print_chase();
    print_created_by();

    initialized_splash = true;
}

/* the splash is drawn once and then left alone; tools/uisim draws it again to
 * check that every rotation comes out as the same picture turned */
void reset_splash(void) { initialized_splash = false; }

/*
 * configure() has already run, so the style is settled and only the buffers
 * the chosen splash actually draws from are allocated - the picture wants one
 * row, the drawn one wants a glyph, a sprite row and a pellet.
 */
void zmk_widget_splash_init(void) {
    if (get_splash_style() == SPLASH_STYLE_IMAGE) {
        buf_row = k_malloc(SPLASH_IMAGE_WIDTH * sizeof(uint16_t));
        return;
    }

    buf_glyph = k_malloc(SCALED_BITMAP_BYTES(WORDMARK_FONT_W, WORDMARK_FONT_H, wordmark_scale));
    buf_sprite = k_malloc(SCALED_BITMAP_BYTES(splash_pac_width, 1, 1));
    buf_pellet = k_malloc(pellet_size * pellet_size * 2u);
    if (buf_pellet != NULL) {
        fill_buffer_color(buf_pellet, pellet_size * pellet_size * 2u, get_splash_created_by_color());
    }
}

/*
 * The dongle calls this once, when the splash hands the panel over.  The
 * pointers are cleared anyway: k_free() of the same address twice corrupts the
 * heap, and a caller that runs the splash more than once - the configurator
 * page does - should get a null pointer rather than a wrecked allocator.
 */
void clean_up_splash(void) {
    k_free(buf_glyph);
    k_free(buf_sprite);
    k_free(buf_pellet);
    k_free(buf_row);
    buf_glyph = NULL;
    buf_sprite = NULL;
    buf_pellet = NULL;
    buf_row = NULL;
}
