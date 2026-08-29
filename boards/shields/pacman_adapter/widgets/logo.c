/*
 * Pac-Man dongle - the animated header of the dashboard.
 *
 * The top slot of the menu is a lap of the maze in miniature: a ring of
 * pellets round the edge of the slot with Pac-Man running it and a ghost a
 * few steps behind, the wordmark sitting in the middle.  Each cell of the
 * ring is a 6x6 sprite scaled up, and one tick of the timer moves everyone on
 * by one cell, so the whole animation costs four small blits a frame rather
 * than a repaint of the slot.
 *
 * The ring is walked as a single sequence of sections numbered clockwise from
 * the top-left corner; get_section() turns a count into a cell and the
 * direction being travelled, which is what picks the sprite.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include "logo.h"
#include "helpers/display.h"

/* how far behind Pac-Man the ghost runs, in cells */
#define GHOST_LAG 3

static bool animation_running = false;
static bool animation_initialized = false;
static bool mouth_open = true;

static uint8_t logo_animation_width = 17;  /* cells across the ring */
static uint8_t logo_animation_height = 6;  /* and down it */
static uint8_t logo_animation_scale = 2;

static uint16_t logo_animation_x = 12;
static uint16_t logo_animation_y = 14;

static uint16_t animation_sections_total;
static uint16_t animation_cycle_count = 0;

static uint16_t logo_text_x = 58;
static uint16_t logo_text_y = 44;
static uint16_t logo_text_font_width = 3;
static uint16_t logo_text_font_height = 5;
static uint16_t logo_text_font_scale = 5;

static uint16_t *animation_buf;
static uint16_t *logo_text_buf;

typedef struct section {
    uint16_t x;
    uint16_t y;
    uint8_t num; /* the way this cell is travelled: 0 right, 1 down, 2 left, 3 up */
} Section;

/* Pac-Man mid-bite, one sprite per direction of travel */
const static uint16_t animation_pac_open[4][36] = {
    {
        0, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 0, 0,
        1, 1, 1, 0, 0, 0,
        1, 1, 1, 0, 0, 0,
        1, 1, 1, 1, 0, 0,
        0, 1, 1, 1, 1, 0,
    }, {
        0, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 0, 0, 1, 1,
        0, 1, 0, 0, 1, 0,
    }, {
        0, 1, 1, 1, 1, 0,
        0, 0, 1, 1, 1, 1,
        0, 0, 0, 1, 1, 1,
        0, 0, 0, 1, 1, 1,
        0, 0, 1, 1, 1, 1,
        0, 1, 1, 1, 1, 0,
    }, {
        0, 1, 0, 0, 1, 0,
        1, 1, 0, 0, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1,
        0, 1, 1, 1, 1, 0,
    }
};

/* and with his mouth shut, which is the same whichever way he faces */
const static uint16_t animation_pac_closed[] = {
    0, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    0, 1, 1, 1, 1, 0,
};

const static uint16_t animation_ghost[] = {
    0, 1, 1, 1, 1, 0,
    1, 1, 1, 1, 1, 1,
    1, 0, 1, 1, 0, 1,
    1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
    1, 0, 1, 1, 0, 1,
};

const static uint16_t animation_pellet[] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
};

const static uint16_t animation_space[] = {
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
};

static void print_animation(Section s, const uint16_t animation_sprite[], uint16_t color) {
    uint16_t x = (s.x * 6 * logo_animation_scale) + logo_animation_x;
    uint16_t y = (s.y * 6 * logo_animation_scale) + logo_animation_y;
    render_bitmap(animation_buf, (uint16_t *)animation_sprite, x, y, 6, 6, logo_animation_scale,
                  color, get_logo_bg_color());
}

static Section get_section(uint16_t cc) {
    Section s = {0, 0, 0};
    cc = cc % animation_sections_total;

    if (cc < logo_animation_width) {
        s.x = cc;
        s.y = 0;
        s.num = 0;
        return s;
    }
    if (cc < logo_animation_width + logo_animation_height) {
        s.x = logo_animation_width;
        s.y = cc - logo_animation_width;
        s.num = 1;
        return s;
    }
    if (cc < (logo_animation_width * 2) + logo_animation_height) {
        s.x = (logo_animation_width * 2) + logo_animation_height - cc;
        s.y = logo_animation_height;
        s.num = 2;
        return s;
    }
    s.x = 0;
    s.y = animation_sections_total - cc;
    s.num = 3;
    return s;
}

static void print_animation_pac(Section s) {
    if (mouth_open) {
        print_animation(s, animation_pac_open[s.num], get_logo_font_color());
        return;
    }
    print_animation(s, animation_pac_closed, get_logo_font_color());
}

static void print_animation_ghost(Section s) {
    print_animation(s, animation_ghost, get_logo_accent_color());
}

static void print_animation_pellet(Section s) {
    print_animation(s, animation_pellet, get_logo_accent_color());
}

static void print_animation_space(Section s) {
    print_animation(s, animation_space, get_logo_bg_color());
}

static void print_logo_text(void) {
    Character logo_chars[] = {CHAR_P, CHAR_A, CHAR_C, CHAR_DASH, CHAR_M, CHAR_A, CHAR_N};
    uint16_t char_gap_pixels = 3;

    print_string(logo_text_buf, logo_chars, logo_text_x, logo_text_y, logo_text_font_scale,
                 get_logo_font_color(), get_logo_bg_color(), FONT_SIZE_3x5, char_gap_pixels,
                 ARRAY_SIZE(logo_chars));
}

static void print_initial_animation(void) {
    print_logo_text();
    for (uint16_t i = 0; i < animation_sections_total; i++) {
        print_animation_pellet(get_section(i));
    }
}

void logo_animation_timer(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    if (!animation_running) {
        return;
    }
    if (!animation_initialized) {
        print_initial_animation();
        animation_cycle_count = 0;
        animation_initialized = true;
    }

    /* he eats what he runs over, the ghost follows, and the lap fills in
     * again behind it ready for the next time round */
    print_animation_space(get_section(animation_cycle_count));
    animation_cycle_count = (animation_cycle_count + 1) % animation_sections_total;
    print_animation_pac(get_section(animation_cycle_count));
    print_animation_ghost(get_section(animation_sections_total + animation_cycle_count - GHOST_LAG));
    print_animation_pellet(
        get_section(animation_sections_total + animation_cycle_count - GHOST_LAG - 1));

    mouth_open = !mouth_open;
}

void stop_animation(void) { animation_running = false; }

void start_animation(void) {
    if (get_slot_mode() != SLOT_MODE_2) {
        print_logo_text(); /* no room for the lap: the wordmark alone */
        return;
    }
    animation_initialized = false;
    animation_running = true;
}

void logo_animation_init(void) {
    uint16_t text_size = (logo_text_font_width * logo_text_font_scale) *
                         (logo_text_font_height * logo_text_font_scale);
    logo_text_buf = k_malloc(text_size * 2 * sizeof(uint16_t));

    if (get_slot_mode() != SLOT_MODE_2) {
        logo_text_y = 20;
        logo_text_font_scale = 4;
        return;
    }
    animation_sections_total = (logo_animation_width * 2) + (logo_animation_height * 2);
    animation_buf = k_malloc(36 * logo_animation_scale * logo_animation_scale * 2 * sizeof(uint16_t));
}
