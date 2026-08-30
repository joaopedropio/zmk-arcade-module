#pragma once
#include <stdint.h>
#include "uisim_state.h"

static inline uint8_t bt_bas_get_battery_level(void) {
    static const uint8_t levels[] = UISIM_BATTERY_LEVELS;
    return levels[0];
}
