#pragma once
#include <stdint.h>
#include "uisim_state.h"

static inline uint8_t zmk_keymap_highest_layer_active(void) { return UISIM_LAYER_INDEX; }
static inline const char *zmk_keymap_layer_name(uint8_t index) {
    (void)index;
    return UISIM_LAYER_NAME;
}
