#pragma once
#include <zmk/event_manager.h>
#include "uisim_state.h"

struct zmk_wpm_state_changed {
    int state;
};

static inline struct zmk_wpm_state_changed *as_zmk_wpm_state_changed(const zmk_event_t *eh) {
    static struct zmk_wpm_state_changed ev = {.state = UISIM_WPM};
    (void)eh;
    return &ev;
}
