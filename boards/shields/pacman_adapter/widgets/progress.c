/*
 * Pac-Man dongle - the modal shown while a profile is being applied.
 *
 * Moving between profiles is a flash write for the profile being left and one
 * for every setting that moved.  That is long enough to look like a button
 * that did not take, and somebody who thinks it did not take presses again -
 * landing a second switch on top of a profile that is half applied.  So the
 * panel says what it is doing and how far along it is, and the button is
 * refused until the bar is full.
 *
 * Everything here draws, so everything here runs on the display queue.  The
 * flash writes are on another thread and reach this through an atomic and a
 * work item in action_button.c; nothing in this file knows that.
 *
 * There is no frame buffer on this shield, so the box is painted in strips
 * through one small buffer rather than composed and pushed - the same trade
 * every other thing that draws here makes.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include "helpers/display.h"
#include "progress.h"

/*
 * The panel is 240 square and the modal is centred in it.  The bar sits in the
 * lower half of the box with the word above it, which leaves the border clear
 * of both at any theme's frame width.
 */
#define PANEL 240
#define BOX_W 192
#define BOX_H 72
#define BOX_X ((PANEL - BOX_W) / 2)
#define BOX_Y ((PANEL - BOX_H) / 2)
#define BORDER 2

#define BAR_MARGIN 16
#define BAR_X (BOX_X + BAR_MARGIN)
#define BAR_W (BOX_W - (BAR_MARGIN * 2))
#define BAR_H 14
#define BAR_Y (BOX_Y + BOX_H - BAR_MARGIN - BAR_H)

/* the trough inside the bar's own border, which is what actually fills */
#define TROUGH_X (BAR_X + BORDER)
#define TROUGH_Y (BAR_Y + BORDER)
#define TROUGH_W (BAR_W - (BORDER * 2))
#define TROUGH_H (BAR_H - (BORDER * 2))

#define FONT_W 3
#define FONT_H 6
#define FONT_SCALE 3
#define CHAR_GAP 2
/* one glyph and the gap after it, which is how the row is measured and placed */
#define CHAR_STEP ((FONT_W * FONT_SCALE) + CHAR_GAP)

/* APPLYING, then the slot the dongle is moving to */
static const Character applying[] = {CHAR_A, CHAR_P, CHAR_P, CHAR_L,
                                     CHAR_Y, CHAR_I, CHAR_N, CHAR_G};

#define WORD_LEN ((uint16_t)(sizeof(applying) / sizeof(applying[0])))
#define ROW_CHARS (WORD_LEN + 3) /* the word, a space, and two digits */
#define ROW_W ((ROW_CHARS * CHAR_STEP) - CHAR_GAP)
#define ROW_X (BOX_X + ((BOX_W - ROW_W) / 2))
#define ROW_Y (BOX_Y + 16)

/*
 * Wide enough that the box goes down in eight-row strips rather than in
 * seventy-two single ones.  Anything wider would be RAM spent on a thing that
 * is on the screen for half a second.
 */
#define STRIP_ROWS 8
#define BUF_PIXELS (BOX_W * STRIP_ROWS)

static uint8_t *buf_block;
static uint16_t *scaled_bitmap;

/* how much of the trough is already painted, so only the new slice is drawn */
static uint16_t filled_w;

void progress_init(void) {
    buf_block = k_malloc(BUF_PIXELS * 2u);
    scaled_bitmap = k_malloc(SCALED_BITMAP_BYTES(FONT_W, FONT_H, FONT_SCALE));
}

/* a solid block through a buffer that holds only part of it */
static void block(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (buf_block == NULL || w == 0 || h == 0) {
        return;
    }

    uint16_t rows = BUF_PIXELS / w;
    if (rows == 0) {
        rows = 1; /* wider than the buffer; the engine clips what it cannot hold */
    }
    for (uint16_t done = 0; done < h; done += rows) {
        uint16_t step = (h - done) < rows ? (h - done) : rows;
        print_filled_rectangle(buf_block, x, y + done, w, step, color);
    }
}

/* a border drawn as four blocks, since print_rectangle() wants one buffer wide
 * enough for the whole span and this one is not */
static void outline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    block(x, y, w, BORDER, color);
    block(x, y + h - BORDER, w, BORDER, color);
    block(x, y, BORDER, h, color);
    block(x + w - BORDER, y, BORDER, h, color);
}

void progress_open(uint8_t slot) {
    uint16_t ink = get_theme_font_color();
    uint16_t number = get_theme_font_color_1();
    uint16_t ground = get_menu_bg_color();

    block(BOX_X, BOX_Y, BOX_W, BOX_H, ground);
    outline(BOX_X, BOX_Y, BOX_W, BOX_H, get_frame_color());

    print_string(scaled_bitmap, applying, ROW_X, ROW_Y, FONT_SCALE, ink, ground, FONT_SIZE_3x5,
                 CHAR_GAP, (uint8_t)WORD_LEN);
    print_bitmap(scaled_bitmap, int_to_num_char(slot / 10), ROW_X + ((WORD_LEN + 1) * CHAR_STEP),
                 ROW_Y, FONT_SCALE, number, ground, FONT_SIZE_3x5);
    print_bitmap(scaled_bitmap, int_to_num_char(slot % 10), ROW_X + ((WORD_LEN + 2) * CHAR_STEP),
                 ROW_Y, FONT_SCALE, number, ground, FONT_SIZE_3x5);

    outline(BAR_X, BAR_Y, BAR_W, BAR_H, get_frame_color());
    block(TROUGH_X, TROUGH_Y, TROUGH_W, TROUGH_H, get_frame_color_1());
    filled_w = 0;
}

void progress_draw(uint16_t done, uint16_t total) {
    if (total == 0) {
        return;
    }
    if (done > total) {
        done = total;
    }

    uint16_t want = (uint16_t)(((uint32_t)TROUGH_W * done) / total);
    if (want <= filled_w) {
        return; /* not a whole pixel further along yet */
    }
    block(TROUGH_X + filled_w, TROUGH_Y, want - filled_w, TROUGH_H, get_theme_font_color_1());
    filled_w = want;
}
