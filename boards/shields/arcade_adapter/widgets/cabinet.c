/*
 * Arcade dongle - the cabinet dashboard.
 *
 * The second way the status screen can look, chosen by `dashboard-style`.
 * Where the classic dashboard is a configurable grid of slots with an animated
 * header, this is one fixed cabinet-HUD layout: the active layer name across
 * the top, the WPM as a big score with the two connectivity lamps beside it,
 * the modifiers as lit buttons, and the two peripheral batteries as ENERGY
 * bars.
 *
 * It carries its own art.  The classic widgets share a 3x5 font and hand-drawn
 * modifier and connectivity sprites; none of that is used here.  This file
 * defines a chunky 5x7 cabinet font and a round lamp of its own, and blits them
 * with render_bitmap() - the panel's content-agnostic "scale this 0/1 grid and
 * push one rectangle" call, the equivalent of putImageData().  The ENERGY bars
 * get a scanline over them: a thin line every fourth row in the frame's dark
 * colour, laid down as its own one-pixel fills so it survives the panel
 * rotation the same way every other fill does.  A translucent overlay across
 * the whole panel would need reading it back, and this shield never does that,
 * and opaque lines through 14-pixel-tall text at this resolution take more off
 * than they add - so the texture stays on the one box that is a display.  The
 * only other things it borrows are clear_screen() and the colour getters,
 * because those colours are the settings and sharing them is what lets a
 * `arcade set` repaint this the same way it repaints the classic one, through
 * refresh_screen().
 *
 * It owns the whole panel while it is up, so the slot widgets are never
 * started in this mode.  Their listeners still keep their state current, and
 * this file reads it back through the small getter each widget exposes; a
 * listener then calls the matching cabinet_refresh_*(), the same place it would
 * call its own print_*.  Each refresher repaints just its own corner and does
 * nothing unless this dashboard is the one on screen.  Positions are fixed -
 * there is no slot table and no per-slot buffer - so nothing here has to be
 * rebuilt when a slot setting moves, which is what lets it hot-reload.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>

#include <dt-bindings/zmk/modifiers.h>

#include "cabinet.h"
#include "battery_status.h"
#include "helpers/display.h"
#include "layer_status.h"
#include "modifier.h"
#include "output_status.h"
#include "wpm.h"

#define A_PANEL 240
#define A_BORDER 2

/* the fill buffer goes down in strips, the way progress.c's does */
#define A_STRIP_ROWS 8
#define A_FILL_PIXELS (A_PANEL * A_STRIP_ROWS)

/* --- the font: a bold 5x7, one row per byte, bit 4 the leftmost pixel --- */
#define GLYPH_W 5
#define GLYPH_H 7
#define GLYPH_ADV(scale) ((GLYPH_W + 1) * (scale))

/* A-Z, then 0-9, then '-', '%', and a blank for a space or anything unknown */
#define GLYPH_DASH 36
#define GLYPH_PCT 37
#define GLYPH_BLANK 38
#define GLYPH_DIGIT(d) (26 + (d))
#define GLYPH_COUNT 39

static const uint8_t FONT[GLYPH_COUNT][GLYPH_H] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x19, 0x1A, 0x04, 0x0B, 0x13, 0x00, 0x00}, /* % */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* blank */
};

/* a round lamp, 10x10, drawn at scale 1 */
#define LAMP_D 10
static const uint16_t LAMP[LAMP_D] = {
    0x078, 0x1FE, 0x1FE, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x1FE, 0x1FE, 0x078,
};

/* --- geometry, top to bottom (dongle pixels) --- */
#define LAYER_SCALE 5
#define LAYER_Y 10
#define LAYER_LIMIT 7
#define RULE1_Y 50
#define LAMP_Y1 60
#define LAMP_Y2 82
#define TAG_SCALE 2
#define SCORE_SCALE 5
#define SCORE_X 116
#define SCORE_Y 58
#define RULE2_Y 102
#define MODS_Y 110
#define MOD_W 52
#define MOD_H 30
#define MOD_STEP 57
#define MOD_SCALE 2
#define RULE3_Y 150
#define ENERGY_Y 156
#define LABEL_SCALE 2
#define BAR1_Y 178
#define BAR2_Y 202 /* 8px between the two bars, not 14 */
#define BAR_X 36
#define BAR_W 160
#define BAR_H 16
#define BAR_SEGS 14
#define PCT_X (BAR_X + BAR_W + 6) /* room for three digits before the border */

/* the score digit at its scale is the widest single blit */
#define SCRATCH_BYTES SCALED_BITMAP_BYTES(GLYPH_W, GLYPH_H, SCORE_SCALE)

static uint8_t *fill_buf;
static uint16_t *scratch;
static uint16_t bits[LAMP_D * LAMP_D]; /* the 0/1 grid render_bitmap() wants */
static bool active;

/* a solid block through a buffer that only holds part of it (progress.c's trick) */
static void block(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (fill_buf == NULL || w == 0 || h == 0) {
        return;
    }
    uint16_t rows = A_FILL_PIXELS / w;
    if (rows == 0) {
        rows = 1; /* wider than the buffer; the engine clips what it cannot hold */
    }
    for (uint16_t done = 0; done < h; done += rows) {
        uint16_t step = (h - done) < rows ? (h - done) : rows;
        print_filled_rectangle(fill_buf, x, y + done, w, step, color);
    }
}

/*
 * A scanline over a box already drawn: a one-pixel line every fourth row, in
 * the frame's dark colour so it reads as monitor texture rather than a missing
 * row.  Each line is its own uniform fill, so it rotates with everything else.
 * The phase is on the panel row so the lines line up across neighbouring boxes.
 */
#define SCAN_GAP 4
static void scanlines(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t line = get_frame_color_1();

    for (uint16_t r = y + ((SCAN_GAP - 1) - (y % SCAN_GAP)); r < y + h; r += SCAN_GAP) {
        block(x, r, w, 1, line);
    }
}

static void outline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    block(x, y, w, A_BORDER, color);
    block(x, y + h - A_BORDER, w, A_BORDER, color);
    block(x, y, A_BORDER, h, color);
    block(x + w - A_BORDER, y, A_BORDER, h, color);
}

/* unpack a packed bitmap into the 0/1 grid and hand it to the panel, crisp */
static void blit(const uint16_t *packed, uint16_t w, uint16_t h, uint16_t x, uint16_t y,
                 uint16_t scale, uint16_t fg, uint16_t bg) {
    if (scratch == NULL) {
        return;
    }
    for (uint16_t r = 0; r < h; r++) {
        for (uint16_t c = 0; c < w; c++) {
            bits[(r * w) + c] = (packed[r] >> (w - 1 - c)) & 1u;
        }
    }
    render_bitmap(scratch, bits, x, y, w, h, scale, fg, bg);
}

static int glyph_of(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a';
    }
    if (ch >= '0' && ch <= '9') {
        return GLYPH_DIGIT(ch - '0');
    }
    if (ch == '-') {
        return GLYPH_DASH;
    }
    if (ch == '%') {
        return GLYPH_PCT;
    }
    return GLYPH_BLANK;
}

static void aglyph(int idx, uint16_t x, uint16_t y, uint16_t scale, uint16_t fg, uint16_t bg) {
    uint16_t rows[GLYPH_H];

    for (uint16_t r = 0; r < GLYPH_H; r++) {
        rows[r] = FONT[idx][r];
    }
    blit(rows, GLYPH_W, GLYPH_H, x, y, scale, fg, bg);
}

static void atext(const char *s, uint16_t x, uint16_t y, uint16_t scale, uint16_t fg, uint16_t bg) {
    for (uint16_t i = 0; s[i] != '\0'; i++) {
        aglyph(glyph_of(s[i]), x + (i * GLYPH_ADV(scale)), y, scale, fg, bg);
    }
}

/* a fixed-width decimal, zero-padded, the way a cabinet score reads */
static void adigits(uint32_t value, uint8_t count, uint16_t x, uint16_t y, uint16_t scale,
                    uint16_t fg, uint16_t bg) {
    for (int i = count - 1; i >= 0; i--) {
        aglyph(GLYPH_DIGIT(value % 10), x + (GLYPH_ADV(scale) * i), y, scale, fg, bg);
        value /= 10;
    }
}

/* the battery colours double as the ENERGY bar's; there is no third scale here */
static uint16_t level_color(uint8_t level) {
    if (level >= 70) {
        return get_bt_status_ok_color();
    }
    if (level >= 35) {
        return get_bt_status_open_color();
    }
    return get_bt_status_not_ok_color();
}

/* --- the pieces; each clears its own region so the partials can call it --- */

/* just the active layer's name, centred across the top */
static void draw_layer(void) {
    uint16_t ground = get_menu_bg_color();
    const char *name = layer_current_label();
    uint8_t len;
    uint16_t w, x;

    if (name == NULL || name[0] == '\0') {
        name = "---";
    }
    len = (uint8_t)strlen(name);
    if (len > LAYER_LIMIT) {
        len = LAYER_LIMIT;
    }
    w = (len * GLYPH_ADV(LAYER_SCALE)) - LAYER_SCALE;
    x = (w < A_PANEL) ? ((A_PANEL - w) / 2) : A_BORDER;

    block(A_BORDER, LAYER_Y, A_PANEL - (2 * A_BORDER), GLYPH_H * LAYER_SCALE, ground);
    for (uint8_t i = 0; i < len; i++) {
        aglyph(glyph_of(name[i]), x + (i * GLYPH_ADV(LAYER_SCALE)), LAYER_Y, LAYER_SCALE,
               get_layer_font_color(), ground);
    }
}

static void draw_wpm(void) {
    uint16_t ground = get_menu_bg_color();

    block(SCORE_X, SCORE_Y, 3 * GLYPH_ADV(SCORE_SCALE), GLYPH_H * SCORE_SCALE, ground);
    adigits(wpm_current(), 3, SCORE_X, SCORE_Y, SCORE_SCALE, get_wpm_font_1_color(), ground);
}

/* the one label that never moves; drawn once by a full render */
static void draw_labels(void) {
    atext("ENERGY", 8, ENERGY_Y, LABEL_SCALE, get_wpm_font_color(), get_menu_bg_color());
}

static void draw_lamp(uint16_t y, const char *tag, const char *value, bool lit) {
    uint16_t ground = get_menu_bg_color();

    blit(LAMP, LAMP_D, LAMP_D, 8, y + 1, 1,
         lit ? get_bt_status_ok_color() : get_bt_status_not_ok_color(), ground);
    atext(tag, 24, y, TAG_SCALE, get_wpm_font_color(), ground);
    atext(value, 24 + (3 * GLYPH_ADV(TAG_SCALE)), y, TAG_SCALE,
          lit ? get_symbol_selected_color() : get_symbol_unselected_color(), ground);
}

static void draw_lamps(void) {
    connectivity_snapshot c = connectivity_get();
    char bt[4] = "BT-";

    /* narrow enough that a lamp refresh never reaches the score beside it */
    block(6, LAMP_Y1 - 2, SCORE_X - 12, (LAMP_Y2 - LAMP_Y1) + (GLYPH_H * TAG_SCALE) + 4,
          get_menu_bg_color());

    if (c.ble_connected && c.ble_profile >= 0 && c.ble_profile < 9) {
        bt[2] = (char)('1' + c.ble_profile);
    }
    draw_lamp(LAMP_Y1, "1P", c.usb_ready ? "USB" : "OFF", c.usb_ready);
    draw_lamp(LAMP_Y2, "2P", c.ble_connected ? bt : "OFF", c.ble_connected);
}

static void draw_mods(void) {
    static const char *const label[4] = {"CMD", "OPT", "CTL", "SFT"};
    uint16_t ground = get_menu_bg_color();
    uint16_t on = get_modifier_selected_color();
    uint16_t off = get_modifier_unselected_color();
    uint8_t m = modifier_current_mask();
    const bool held[4] = {
        (m & (MOD_LGUI | MOD_RGUI)) != 0,
        (m & (MOD_LALT | MOD_RALT)) != 0,
        (m & (MOD_LCTL | MOD_RCTL)) != 0,
        (m & (MOD_LSFT | MOD_RSFT)) != 0,
    };

    block(8, MODS_Y, A_PANEL - 16, MOD_H, ground);
    for (int i = 0; i < 4; i++) {
        uint16_t x = 8 + (i * MOD_STEP);
        uint16_t tw = (3 * GLYPH_ADV(MOD_SCALE)) - MOD_SCALE;
        uint16_t tx = x + ((MOD_W - tw) / 2);
        uint16_t ty = MODS_Y + ((MOD_H - (GLYPH_H * MOD_SCALE)) / 2);

        if (held[i]) {
            block(x, MODS_Y, MOD_W, MOD_H, on);
        } else {
            outline(x, MODS_Y, MOD_W, MOD_H, off);
        }
        atext(label[i], tx, ty, MOD_SCALE, held[i] ? ground : off, held[i] ? on : ground);
    }
}

static void draw_bar(uint16_t y, const char *tag, uint8_t level) {
    uint16_t ground = get_menu_bg_color();
    uint16_t col = level_color(level);
    uint16_t ty = y + ((BAR_H - (GLYPH_H * LABEL_SCALE)) / 2);
    uint8_t filled = (uint8_t)((((uint32_t)level * BAR_SEGS) + 50) / 100);

    atext(tag, 8, ty, LABEL_SCALE, get_wpm_font_color(), ground);
    block(BAR_X, y, BAR_W, BAR_H, get_frame_color_1());
    for (uint8_t i = 0; i < filled && i < BAR_SEGS; i++) {
        uint16_t sx = BAR_X + 1 + ((i * (BAR_W - 2)) / BAR_SEGS);
        block(sx, y + 2, ((BAR_W - 2) / BAR_SEGS) - 2, BAR_H - 4, col);
    }
    scanlines(BAR_X, y, BAR_W, BAR_H);
    block(PCT_X, y, A_PANEL - A_BORDER - PCT_X, GLYPH_H * LABEL_SCALE, ground);
    adigits(level, level >= 100 ? 3 : (level >= 10 ? 2 : 1), PCT_X, ty, LABEL_SCALE, col, ground);
}

static void draw_battery(void) {
    block(6, BAR1_Y - 2, A_PANEL - 12, (BAR2_Y - BAR1_Y) + BAR_H + 4, get_menu_bg_color());
    draw_bar(BAR1_Y, "P1", battery_current_level(0));
    draw_bar(BAR2_Y, "P2", battery_current_level(1));
}

/* --- entry points --- */

void cabinet_init(void) {
    fill_buf = k_malloc(A_FILL_PIXELS * 2u);
    scratch = k_malloc(SCRATCH_BYTES);
}

void cabinet_set_active(bool on) { active = on; }

void cabinet_render(void) {
    if (fill_buf == NULL || scratch == NULL) {
        return;
    }
    uint16_t rule = get_frame_color();
    uint16_t hair = get_frame_color_1();

    clear_screen(get_menu_bg_color());
    block(0, 0, A_PANEL, A_BORDER, rule);
    block(0, A_PANEL - A_BORDER, A_PANEL, A_BORDER, rule);
    block(0, 0, A_BORDER, A_PANEL, rule);
    block(A_PANEL - A_BORDER, 0, A_BORDER, A_PANEL, rule);

    draw_layer();
    block(8, RULE1_Y, A_PANEL - 16, 1, hair); /* the same hairline as RULE2 and RULE3 */
    draw_lamps();
    draw_wpm();
    draw_labels();
    block(8, RULE2_Y, A_PANEL - 16, 1, hair);
    draw_mods();
    block(8, RULE3_Y, A_PANEL - 16, 1, hair);
    draw_battery();
}

void cabinet_refresh_wpm(void) {
    if (active) {
        draw_wpm();
    }
}

void cabinet_refresh_layer(void) {
    if (active) {
        draw_layer();
    }
}

void cabinet_refresh_mods(void) {
    if (active) {
        draw_mods();
    }
}

void cabinet_refresh_battery(void) {
    if (active) {
        draw_battery();
    }
}

void cabinet_refresh_connectivity(void) {
    if (active) {
        draw_lamps();
    }
}
