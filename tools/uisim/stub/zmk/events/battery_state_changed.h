/*
 * The battery widget dereferences what as_zmk_...() hands back without
 * checking it, so this must always return something - and it returns a
 * different source each call, which is what fills both halves' slots from the
 * one init the listener stub does.
 */
#pragma once
#include <stdint.h>
#include <zmk/event_manager.h>
#include "uisim_state.h"

struct zmk_peripheral_battery_state_changed {
    uint8_t source;
    uint8_t state_of_charge;
};

static inline const struct zmk_peripheral_battery_state_changed *
as_zmk_peripheral_battery_state_changed(const zmk_event_t *eh) {
    static const uint8_t levels[] = UISIM_BATTERY_LEVELS;
    static struct zmk_peripheral_battery_state_changed ev;
    static uint8_t next;

    (void)eh;
    ev.source = next % UISIM_BATTERY_SOURCES;
    ev.state_of_charge = levels[ev.source];
    next++;
    return &ev;
}
