#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "uisim_state.h"

static inline uint8_t zmk_ble_active_profile_index(void) { return UISIM_BLE_PROFILE; }
static inline bool zmk_ble_active_profile_is_connected(void) { return UISIM_BLE_CONNECTED; }
static inline bool zmk_ble_active_profile_is_open(void) { return !UISIM_BLE_BONDED; }
