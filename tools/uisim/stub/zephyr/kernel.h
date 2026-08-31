/* Host stand-in for Zephyr's kernel API - just enough for the UI helpers. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ARG_UNUSED(x) ((void)(x))

/*
 * Atomics are how the firmware's threads talk without a lock.  There is one
 * thread here, so they are plain reads and writes - kept as the same calls so
 * the widgets compile exactly as the firmware compiles them.
 */
typedef long atomic_t;
#define ATOMIC_INIT(v) (v)

static inline atomic_t atomic_get(const atomic_t *target) { return *target; }
static inline atomic_t atomic_set(atomic_t *target, atomic_t value) {
    atomic_t was = *target;
    *target = value;
    return was;
}
static inline atomic_t atomic_clear(atomic_t *target) { return atomic_set(target, 0); }
static inline bool atomic_cas(atomic_t *target, atomic_t expect, atomic_t desired) {
    if (*target != expect) {
        return false;
    }
    *target = desired;
    return true;
}

/*
 * The firmware uses work items to put a job on a particular thread - the flash
 * writes of a profile switch off the queue that has to repaint next, and the
 * repaint back onto it.  There is one thread here and nothing to keep apart,
 * so a submitted item simply runs where it was submitted.
 */
struct k_work;
typedef void (*k_work_handler_t)(struct k_work *work);
struct k_work { k_work_handler_t handler; };
struct k_work_q;

#define K_WORK_DEFINE(name, fn) struct k_work name = {.handler = (fn)}

static inline int k_work_submit(struct k_work *work) {
    work->handler(work);
    return 0;
}

static inline int k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work) {
    ARG_UNUSED(queue);
    work->handler(work);
    return 0;
}

static inline void *k_malloc(size_t n) { return malloc(n); }
static inline void k_free(void *p) { free(p); }
static inline int64_t k_uptime_get(void) { return 0; }
