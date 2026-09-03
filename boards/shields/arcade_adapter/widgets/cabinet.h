/*
 * Arcade dongle - the cabinet dashboard.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/* allocate the fixed-layout scratch buffers; called once from the status screen */
void cabinet_init(void);

/*
 * Whether the cabinet dashboard owns the panel right now - menu up and
 * `dashboard-style` set to cabinet.  print_menu() and toggle_menu() set it; the
 * refreshers below do nothing while it is false.
 */
void cabinet_set_active(bool on);

/* full repaint; print_menu() calls this instead of the classic body */
void cabinet_render(void);

/*
 * Partial repaints, one per readout.  A widget listener calls the matching one
 * after storing its new state - the same place it would call its own print_*.
 * Each is a no-op unless the cabinet dashboard is active.
 */
void cabinet_refresh_wpm(void);
void cabinet_refresh_layer(void);
void cabinet_refresh_mods(void);
void cabinet_refresh_battery(void);
void cabinet_refresh_connectivity(void);
