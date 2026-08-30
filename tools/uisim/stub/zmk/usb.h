#pragma once
#include <stdbool.h>
#include "uisim_state.h"

static inline bool zmk_usb_is_hid_ready(void) { return UISIM_USB_READY; }
