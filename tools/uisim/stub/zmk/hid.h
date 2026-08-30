#pragma once
#include <stdint.h>
#include <dt-bindings/zmk/modifiers.h>
#include "uisim_state.h"

static inline uint8_t zmk_hid_get_explicit_mods(void) { return UISIM_MODIFIERS; }
