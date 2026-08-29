#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct display_buffer_descriptor {
    uint32_t buf_size;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
};

struct device { const char *name; };

/* the harness owns the panel */
void display_write(const struct device *dev, uint16_t x, uint16_t y,
                   const struct display_buffer_descriptor *desc, const void *buf);
static inline bool device_is_ready(const struct device *dev) { return dev != NULL; }

extern const struct device sim_display_dev;
#define DT_CHOSEN(x) x
#define DEVICE_DT_GET(x) (&sim_display_dev)
