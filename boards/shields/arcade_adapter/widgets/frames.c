/*
 * Arcade dongle - the dashboard's frames.
 *
 * The boxes the slot widgets draw inside.  Which boxes there are depends on
 * the slot mode and on how many batteries are being shown, and every widget
 * positions itself from the same slot table, so this is only the chrome -
 * nothing here knows what goes in a slot.
 *
 * It lives apart from the action button that calls it so the host preview in
 * tools/uisim can draw the layout without dragging in the ZMK event plumbing.
 *
 * SPDX-License-Identifier: MIT
 */

#include "frames.h"
#include "helpers/display.h"

void print_frames(uint8_t *buf_frame) {
    uint16_t thickness = 1;
    SlotMode slot_mode = get_slot_mode();

    print_rectangle(buf_frame, 0, 239, 0, 239, get_frame_color(), thickness);

    if (slot_mode == SLOT_MODE_2) {
        print_container(buf_frame, 1, 239, 1, 113, thickness); // the animated header

        print_container(buf_frame, 1, 120, 113, 161, thickness);   // slot 5
        print_container(buf_frame, 120, 239, 113, 161, thickness); // slot 6
    }

    if (slot_mode == SLOT_MODE_4 || slot_mode == SLOT_MODE_5) {
        print_container(buf_frame, 1, 239, 1, 73, thickness); // the header

        print_container(buf_frame, 1, 120, 73, 117, thickness);   // slot 3
        print_container(buf_frame, 120, 239, 73, 117, thickness); // slot 4

        print_container(buf_frame, 1, 120, 117, 161, thickness);   // slot 5
        print_container(buf_frame, 120, 239, 117, 161, thickness); // slot 6
    }

    if (slot_mode == SLOT_MODE_6) {
        print_container(buf_frame, 1, 120, 1, 55, thickness);      // slot 1
        print_container(buf_frame, 120, 239, 1, 55, thickness);    // slot 2
        print_container(buf_frame, 1, 120, 55, 108, thickness);    // slot 3
        print_container(buf_frame, 120, 239, 55, 108, thickness);  // slot 4
        print_container(buf_frame, 1, 120, 108, 161, thickness);   // slot 5
        print_container(buf_frame, 120, 239, 108, 161, thickness); // slot 6
    }

    /* and the batteries along the bottom */
    if (get_battery_slots() == 1) {
        print_container(buf_frame, 1, 239, 161, 239, thickness);
    } else if (get_battery_slots() == 3) {
        print_container(buf_frame, 1, 81, 161, 239, thickness);
        print_container(buf_frame, 81, 159, 161, 239, thickness);
        print_container(buf_frame, 159, 239, 161, 239, thickness);
    } else {
        print_container(buf_frame, 1, 120, 161, 239, thickness);
        print_container(buf_frame, 120, 239, 161, 239, thickness);
    }
}
