#pragma once
#include <stdint.h>
#include "uisim_state.h"

enum zmk_transport {
    ZMK_TRANSPORT_NONE,
    ZMK_TRANSPORT_USB,
    ZMK_TRANSPORT_BLE,
};

struct zmk_endpoint_instance {
    enum zmk_transport transport;
};

static inline struct zmk_endpoint_instance zmk_endpoint_get_selected(void) {
    struct zmk_endpoint_instance e = {.transport = UISIM_USB_READY ? ZMK_TRANSPORT_USB
                                                                  : ZMK_TRANSPORT_BLE};
    return e;
}
