/*
 * Pac-Man dongle - the band both games blit through.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panel.h"

uint8_t pm_band[PM_BAND_PX * 2];

uint16_t pm_rgb565(uint32_t rgb888) {
    uint16_t r = (uint16_t)(((rgb888 >> 16) & 0xFF) * 31 / 255);
    uint16_t g = (uint16_t)(((rgb888 >> 8) & 0xFF) * 63 / 255);
    uint16_t b = (uint16_t)((rgb888 & 0xFF) * 31 / 255);
    return (uint16_t)((r << 11) | (g << 5) | b);
}
