/*
 * Host preview for the splash screen and the dashboard's animated header.
 *
 * The UI helpers draw by handing rectangles to display_write(); here that
 * lands in a plain framebuffer which is written out as PPM, so the layout can
 * be looked at without flashing anything.  The widgets that read ZMK state
 * (battery, connectivity, layer, wpm, modifiers) get their answers from
 * stub/uisim_state.h instead, so the dashboard comes out filled in rather
 * than empty - plausible, not live.
 *
 *   cc -I tools/uisim/stub -I boards/shields/pacman_adapter/widgets \
 *      -o /tmp/uisim tools/uisim/uisim.c \
 *      boards/shields/pacman_adapter/widgets/helpers/display.c \
 *      boards/shields/pacman_adapter/widgets/splash.c \
 *      boards/shields/pacman_adapter/widgets/logo.c
 *   /tmp/uisim <out-dir> [theme] [rotation] [slot-mode]
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/display.h>

#include "action_button.h"
#include "arcade.h"
/* the made-up dongle state the widgets ask for; see the header */
#include "uisim_state.h"
#include "battery_status.h"
#include "frames.h"
#include "helpers/display.h"
#include "helpers/profiles.h"
#include "layer_status.h"
#include "logo.h"
#include "modifier.h"
#include "pacman.h"
#include "output_status.h"
#include "progress.h"
#include "sound.h"
#include "splash.h"
#include "theme.h"
#include "wpm.h"

#define PANEL 240

const struct device sim_display_dev = {.name = "sim"};

/*
 * The dashboard reaches for the speaker when a half connects, and for the game
 * when the action button swaps screens.  Neither exists here, so they are
 * answered rather than compiled out - keeping the widgets exactly as the
 * firmware builds them is the whole point of this harness.
 */
void pacman_sound_init(void) {}
void pacman_sound_quiet(void) {}
void pacman_sound_connected(bool connected) { (void)connected; }
void pacman_sound_set_mute(bool muted) { (void)muted; }
void pacman_sound_set_volume(uint8_t volume) { (void)volume; }
void pacman_sound_set_bass_floor(uint16_t floor_hz) { (void)floor_hz; }

void pacman_start(void) {}
void pacman_stop(void) {}
void pacman_toggle_pause(void) {}
bool pacman_is_paused(void) { return true; }

/*
 * The slot widget draws which profile the dongle is on, and the action button
 * steps between them - both of which are flash on the dongle and nothing here.
 * The number comes out of uisim_state.h with the rest of the made-up state, so
 * the dashboard shows a plausible one; stepping stays where it is.
 */
int pacman_profile_current(void) { return UISIM_PROFILE_SLOT; }
int pacman_profile_next(void) { return UISIM_PROFILE_SLOT; }
int pacman_profile_load(int slot, bool *reboot, pacman_profile_progress_cb progress) {
    (void)slot;
    (void)progress;
    if (reboot) {
        *reboot = false;
    }
    return 0;
}

void pacman_reload_palette(void) {}
void pacman_set_frame_interval(uint32_t ms) { (void)ms; }
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
    int slots = argc > 4 ? atoi(argv[4]) : 2;

    set_battery_slots(2);
    switch (rotation) {
    case 90: set_display_orientation(DISPLAY_ORIENTATION_90); break;
    case 180: set_display_orientation(DISPLAY_ORIENTATION_180); break;
    case 270: set_display_orientation(DISPLAY_ORIENTATION_270); break;
    default: set_display_orientation(DISPLAY_ORIENTATION_0); break;
    }
    switch (slots) {
    case 4: set_slot_mode(SLOT_MODE_4); break;
    case 5: set_slot_mode(SLOT_MODE_5); break;
    case 6: set_slot_mode(SLOT_MODE_6); break;
    default: set_slot_mode(SLOT_MODE_2); break;
    }
    set_slot_1(SLOT_NAME_LAYER);
    set_slot_2(SLOT_NAME_WPM);
    set_slot_3(SLOT_NAME_THEME);
    set_slot_4(SLOT_NAME_CONNECTIVITY);
    set_slot_5(SLOT_NAME_MODIFIERS);
    set_slot_6(SLOT_NAME_NONE);
    set_custom_theme_colors(0xffb897u, 0x2121deu, 0x1a1a2eu, 0x000000u);
    apply_current_theme(theme);
    init_display();

    /*
     * The slot widget that says which profile the dongle is on sizes itself
     * from its slot, the way the firmware builds it after init_display().
     * Without this it drew nothing, which is how it went unwatched here.  It
     * settles the theme from stored state, so the one asked for goes back on.
     */
    theme_init();
    apply_current_theme(theme);

    zmk_widget_splash_init();
    print_splash();
    write_ppm(dir, "splash");

    /* while the splash still owns its buffers */
    int failures = check_rotations();
    reset_splash();
    print_splash();
    write_ppm(dir, "splash");

    /* and the other splash-style, which draws a picture instead and has to
     * come out of the same rotation check the drawn one does */
    clean_up_splash();
    set_splash_style(SPLASH_STYLE_IMAGE);
    zmk_widget_splash_init();
    failures += check_rotations();
    reset_splash();
    print_splash();
    write_ppm(dir, "splash-image");
    clean_up_splash();
    set_splash_style(SPLASH_STYLE_DRAWN);

    /* and the dashboard: its frames, and the header a few steps into its lap */
    logo_animation_init();
    arcade_init();
    zmk_widget_output_status_init();
    zmk_widget_peripheral_battery_status_init();
    zmk_widget_layer_init();
    zmk_widget_wpm_init();
    zmk_widget_modifier_init();
    zmk_widget_action_button_init();
    initialize_battery_status();
    print_menu();
    /* print_menu() leaves the header to an LVGL timer there is nobody to run */
    for (int i = 0; i < 24; i++) {
        logo_animation_timer(NULL);
    }
    write_ppm(dir, "dashboard");

    /* and the same readouts drawn the other way, as the arcade dashboard */
    set_dashboard_style(DASHBOARD_STYLE_ARCADE);
    print_menu();
    write_ppm(dir, "dashboard-arcade");
    set_dashboard_style(DASHBOARD_STYLE_CLASSIC);
    arcade_set_active(false);

    /*
     * And the modal a profile switch puts over whichever screen is up.  Drawn
     * part-way along, because a bar at nothing and a bar at full both hide the
     * thing worth looking at - whether the fill lands inside its own trough.
     */
    progress_init();
    progress_open(UISIM_PROFILE_SLOT);
    progress_draw(2, 3);
    write_ppm(dir, "applying");

    return failures == 0 ? 0 : 1;
}
