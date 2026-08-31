#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zmk/display.h>
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "helpers/settings.h"

/*
 * The slot widget shows which profile the dongle is on, not which theme.  A
 * theme number was never anything anybody could act on from the panel - the
 * button stepped through a list of four-colour sets and the dashboard changed
 * shade - whereas a profile is the whole look under a name, and the same
 * button now moves between them.  So the number here is the profile's slot,
 * read as it is drawn, and the theme below is what the colours are still
 * derived through.
 */
static uint8_t current_theme = 0;

static uint16_t theme_font_scale = 4;
static uint16_t theme_font_width = 3;
static uint16_t theme_font_height = 6;
static uint16_t *scaled_bitmap_theme_font;

Slot theme_slot;
static uint16_t theme_x = 17;
static uint16_t theme_y = 11;

/*
 * PROF is four glyphs where SKIN was four, so the number keeps the place the
 * layout already had for it.  Slot 0 is drawn as a number like any other:
 * under profiles it is the default one, not the "no theme chosen" the old C-
 * stood for.
 */
static const Character profile_template[] = {
    CHAR_P,
    CHAR_R,
    CHAR_O,
    CHAR_F,
};

void print_themes_5_slot_top() {
    uint8_t gap = 4;
    uint8_t char_len = (theme_font_scale * theme_font_width) + gap;
    uint16_t theme_num_x = theme_x + (char_len * 4);
    uint8_t slot = (uint8_t)pacman_profile_current();
    uint16_t first_num = slot / 10;
    uint16_t second_num = slot % 10;

    uint16_t char_gap_pixels = 2;
    print_string(scaled_bitmap_theme_font, (Character *)profile_template, theme_x, theme_y, theme_font_scale, get_theme_font_color(), get_theme_font_bg_color(), FONT_SIZE_3x5, char_gap_pixels, 4);
    print_bitmap(scaled_bitmap_theme_font, int_to_num_char(first_num), theme_num_x, theme_y, theme_font_scale, get_theme_font_color_1(), get_theme_font_bg_color(), FONT_SIZE_3x5);
    print_bitmap(scaled_bitmap_theme_font, int_to_num_char(second_num), theme_num_x + char_len, theme_y, theme_font_scale, get_theme_font_color_1(), get_theme_font_bg_color(), FONT_SIZE_3x5);
}

void print_themes() {
    if (theme_slot.number == SLOT_NUMBER_NONE) {
        return;
    }
    SlotMode mode = get_slot_mode();
    if (mode == SLOT_MODE_5 && theme_slot.number == SLOT_NUMBER_2) {
        print_themes_5_slot_top();
        return;
    }
    uint8_t slot = (uint8_t)pacman_profile_current();
    uint16_t first_num = slot / 10;
    uint16_t second_num = slot % 10;

    uint16_t char_gap_pixels = 2;
    print_string(scaled_bitmap_theme_font, (Character *)profile_template, theme_x, theme_y, theme_font_scale, get_theme_font_color(), get_theme_font_bg_color(), FONT_SIZE_3x5, char_gap_pixels, 4);
    print_bitmap(scaled_bitmap_theme_font, int_to_num_char(first_num), theme_x + 62, theme_y, theme_font_scale, get_theme_font_color_1(), get_theme_font_bg_color(), FONT_SIZE_3x5);
    print_bitmap(scaled_bitmap_theme_font, int_to_num_char(second_num), theme_x + 76, theme_y, theme_font_scale, get_theme_font_color_1(), get_theme_font_bg_color(), FONT_SIZE_3x5);
}

/*
 * The theme somebody named.  Nothing steps through them any more - the button
 * moves between profiles instead - so this never writes flash: whoever asked
 * for the theme, the shell or a profile being loaded, has already stored it on
 * a thread that is not the display queue about to repaint.
 */
void set_theme_number(uint8_t theme) {
    if (theme >= get_themes_colors_len()) {
        return;
    }
    current_theme = theme;
    apply_current_theme(current_theme);
}

void theme_init() {
    SlotMode mode = get_slot_mode();
    theme_slot = get_slot_by_name(SLOT_NAME_THEME);
    if (mode == SLOT_MODE_5 && theme_slot.number == SLOT_NUMBER_2) {
        theme_font_scale = 9;
        theme_x = 30;
        theme_y = 12;
    } else {
        theme_x += theme_slot.x;
        theme_y += theme_slot.y;
    }
    if (theme_slot.number != SLOT_NUMBER_NONE) {
        scaled_bitmap_theme_font =
            k_malloc(SCALED_BITMAP_BYTES(theme_font_width, theme_font_height, theme_font_scale));
    }

    current_theme = pacman_settings_get_current_theme();
    apply_current_theme(current_theme);
}