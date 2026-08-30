/* the action button never fires on a host; the screens are chosen directly */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zmk/event_manager.h>

struct zmk_dongle_actioned {
    bool pressed;
    int64_t timestamp;
};

static inline const struct zmk_dongle_actioned *as_zmk_dongle_actioned(const zmk_event_t *eh) {
    (void)eh;
    return (const struct zmk_dongle_actioned *)0;
}
