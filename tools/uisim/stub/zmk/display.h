/*
 * Host stand-in for ZMK's display glue.
 *
 * The real macro fetches state on the system work queue and hands it to the
 * widget on the display queue.  Here there is one thread and no events, so
 * init just calls the widget's callback with whatever the stubs make up -
 * once per battery source, because that is the only widget that learns about
 * more than one thing through repeated events rather than a single query.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "uisim_state.h"

#define ZMK_DISPLAY_WIDGET_LISTENER(listener, state_type, cb, state_func)                          \
    static void listener##_init(void) {                                                            \
        for (int _i = 0; _i < UISIM_BATTERY_SOURCES; _i++) {                                       \
            cb(state_func((const zmk_event_t *)0));                                                \
        }                                                                                          \
    }
