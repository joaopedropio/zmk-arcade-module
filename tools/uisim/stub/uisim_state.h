/*
 * The dongle state a host does not have.
 *
 * The dashboard widgets each ask ZMK something - which layer is on, what the
 * halves' batteries are at, which BLE profile is up - and on a laptop there is
 * nothing to ask.  So the stubs answer from here instead, with values chosen
 * to exercise the drawing rather than to be realistic: two digits of battery
 * on both halves, a layer with a name, a couple of modifiers held.
 *
 * Change these to see a different dashboard; nothing else needs touching.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* which profile slot the dashboard's PROF widget says the dongle is on */
#define UISIM_PROFILE_SLOT 3

#define UISIM_LAYER_INDEX 2
#define UISIM_LAYER_NAME "LOWER"
#define UISIM_WPM 45
#define UISIM_BLE_PROFILE 1
#define UISIM_BLE_CONNECTED 1
#define UISIM_BLE_BONDED 1
#define UISIM_USB_READY 0
#define UISIM_MODIFIERS (MOD_LSFT | MOD_LGUI)

/*
 * One battery level per peripheral.  The listener stub calls each widget's
 * callback once per entry, which is how both halves get drawn from a single
 * init - the firmware gets there one event at a time instead.
 */
#define UISIM_BATTERY_LEVELS {72, 58, 91}
#define UISIM_BATTERY_SOURCES 3
