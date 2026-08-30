/* Host stand-in: the events never fire, so the state comes from uisim_state.h. */
#pragma once

typedef struct { int unused; } zmk_event_t;

#define ZMK_EV_EVENT_BUBBLE 0

#define ZMK_LISTENER(...)
#define ZMK_SUBSCRIPTION(...)
