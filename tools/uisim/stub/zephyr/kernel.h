/* Host stand-in for Zephyr's kernel API - just enough for the UI helpers. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) ((void)(x))

static inline void *k_malloc(size_t n) { return malloc(n); }
static inline void k_free(void *p) { free(p); }
static inline int64_t k_uptime_get(void) { return 0; }
