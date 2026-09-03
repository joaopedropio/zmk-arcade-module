/*
 * Pac-Man dongle - the arcade dashboard.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/* allocate the fixed-layout scratch buffers; called once from the status screen */
void arcade_init(void);

/*
 * Whether the arcade dashboard owns the panel right now - menu up and
 * `dashboard-style` set to arcade.  print_menu() and toggle_menu() set it; the
 * refreshers below do nothing while it is false.
 */
void arcade_set_active(bool on);

/* full repaint; print_menu() calls this instead of the classic body */
void arcade_render(void);

/*
 * Partial repaints, one per readout.  A widget listener calls the matching one
 * after storing its new state - the same place it would call its own print_*.
 * Each is a no-op unless the arcade dashboard is active.
 */
void arcade_refresh_wpm(void);
void arcade_refresh_layer(void);
void arcade_refresh_mods(void);
void arcade_refresh_battery(void);
void arcade_refresh_connectivity(void);
