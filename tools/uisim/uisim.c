/*
 * Host preview for the splash screen and the dashboard's animated header.
 *
 * The UI helpers draw by handing rectangles to display_write(); here that
 * lands in a plain framebuffer which is written out as PPM, so the layout can
 * be looked at without flashing anything.  The widgets that read ZMK state
 * (battery, connectivity, layer, wpm, modifiers) are stubbed out - what this
 * shows is the splash, the frames and the logo lap.
 *
 *   cc -I tools/uisim/stub -I boards/shields/pacman_adapter/widgets \
 *      -o /tmp/uisim tools/uisim/uisim.c \
 *      boards/shields/pacman_adapter/widgets/helpers/display.c \
 *      boards/shields/pacman_adapter/widgets/splash.c \
 *      boards/shields/pacman_adapter/widgets/logo.c
 *   /tmp/uisim <out-dir> [theme] [rotation]
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/display.h>

#include "frames.h"
#include "helpers/display.h"
#include "logo.h"
#include "splash.h"

#define PANEL 240

const struct device sim_display_dev = {.name = "sim"};
static uint16_t fb[PANEL][PANEL];

void display_write(const struct device *dev, uint16_t x, uint16_t y,
                   const struct display_buffer_descriptor *desc, const void *buf) {
    (void)dev;
    const uint8_t *px = buf;
    for (uint16_t j = 0; j < desc->height; j++) {
        for (uint16_t i = 0; i < desc->width; i++) {
            uint16_t dx = x + i, dy = y + j;
            if (dx >= PANEL || dy >= PANEL) {
                continue;
            }
            const uint8_t *p = px + 2 * (j * desc->pitch + i);
            fb[dy][dx] = (uint16_t)((p[0] << 8) | p[1]);
        }
    }
}

/*
 * The panel can be mounted any way up, and every draw goes through the same
 * rotation.  Whatever the screens look like, then, turning the panel must be
 * exactly that - the same picture, turned - so render the splash at each
 * orientation and check it against the one at 0.  It catches a draw that
 * forgot to rotate, which is what render_filled_rectangle() used to do.
 */
static uint16_t upright[PANEL][PANEL];

static void map_pixel(int rotation, int x, int y, int *px, int *py) {
    switch (rotation) {
    case 90:  *px = PANEL - 1 - y; *py = x; break;
    case 180: *px = PANEL - 1 - x; *py = PANEL - 1 - y; break;
    case 270: *px = y; *py = PANEL - 1 - x; break;
    default:  *px = x; *py = y; break;
    }
}

static int check_rotations(void) {
    static const int rotations[] = {90, 180, 270};
    static const DisplayOrientation orientations[] = {
        DISPLAY_ORIENTATION_90, DISPLAY_ORIENTATION_180, DISPLAY_ORIENTATION_270};
    int failures = 0;

    set_display_orientation(DISPLAY_ORIENTATION_0);
    reset_splash();
    print_splash();
    memcpy(upright, fb, sizeof(fb));

    for (size_t i = 0; i < sizeof(rotations) / sizeof(rotations[0]); i++) {
        set_display_orientation(orientations[i]);
        memset(fb, 0, sizeof(fb));
        reset_splash();
        print_splash();

        int wrong = 0;
        for (int y = 0; y < PANEL; y++) {
            for (int x = 0; x < PANEL; x++) {
                int px, py;
                map_pixel(rotations[i], x, y, &px, &py);
                if (fb[py][px] != upright[y][x]) {
                    wrong++;
                }
            }
        }
        printf("rotation %3d: %s (%d pixels off)\n", rotations[i], wrong ? "FAIL" : "ok", wrong);
        failures += wrong ? 1 : 0;
    }

    set_display_orientation(DISPLAY_ORIENTATION_0);
    return failures;
}

static void write_ppm(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", PANEL, PANEL);
    for (int y = 0; y < PANEL; y++) {
        for (int x = 0; x < PANEL; x++) {
            uint16_t c = fb[y][x];
            unsigned char rgb[3] = {
                (unsigned char)((((c >> 11) & 0x1F) * 255) / 31),
                (unsigned char)((((c >> 5) & 0x3F) * 255) / 63),
                (unsigned char)(((c & 0x1F) * 255) / 31),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    int theme = argc > 2 ? atoi(argv[2]) : 0;
    int rotation = argc > 3 ? atoi(argv[3]) : 0;

    set_battery_slots(2);
    switch (rotation) {
    case 90: set_display_orientation(DISPLAY_ORIENTATION_90); break;
    case 180: set_display_orientation(DISPLAY_ORIENTATION_180); break;
    case 270: set_display_orientation(DISPLAY_ORIENTATION_270); break;
    default: set_display_orientation(DISPLAY_ORIENTATION_0); break;
    }
    set_slot_mode(SLOT_MODE_2);
    set_slot_5(SLOT_NAME_WPM);
    set_slot_6(SLOT_NAME_LAYER);
    set_custom_theme_colors(0xffb897u, 0x2121deu, 0x1a1a2eu, 0x000000u);
    apply_current_theme(theme);
    init_display();

    zmk_widget_splash_init();
    print_splash();
    write_ppm(dir, "splash");

    /* while the splash still owns its buffers */
    int failures = check_rotations();
    reset_splash();
    print_splash();
    write_ppm(dir, "splash");

    /* and the dashboard: its frames, and the header a few steps into its lap */
    clean_up_splash();
    clear_screen(get_menu_bg_color());
    uint8_t *buf_frame = malloc(320 * 2);
    print_frames(buf_frame);
    logo_animation_init();
    start_animation();
    for (int i = 0; i < 24; i++) {
        logo_animation_timer(NULL);
    }
    write_ppm(dir, "dashboard");

    return failures == 0 ? 0 : 1;
}
