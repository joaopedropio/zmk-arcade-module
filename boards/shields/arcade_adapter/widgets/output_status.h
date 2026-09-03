/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
 
#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

void zmk_widget_output_status_init(void);
void start_output_status(void);
void stop_output_status(void);
void set_status_symbol(void);

/*
 * A flattened view of the connectivity state for the cabinet dashboard, so it
 * does not have to pull in ZMK's endpoint headers or the private state struct.
 */
typedef struct {
    bool usb_selected;  /* USB is the active endpoint */
    bool usb_ready;     /* and the host has it enumerated as a keyboard */
    bool ble_selected;  /* BLE is the active endpoint */
    int ble_profile;    /* 0-based active BLE profile, -1 if none */
    bool ble_connected; /* that profile has a live link */
    bool ble_bonded;    /* and it is bonded rather than open */
} connectivity_snapshot;

connectivity_snapshot connectivity_get(void);