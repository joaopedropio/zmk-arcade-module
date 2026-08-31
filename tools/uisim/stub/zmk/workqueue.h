/*
 * Host stand-in for ZMK's low-priority work queue.
 *
 * The firmware puts a profile switch's flash writes there so they sit below
 * the sound thread and the display.  There is one thread here and nothing to
 * sit below, and k_work_submit_to_queue() ignores which queue it is given.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <zephyr/kernel.h>

static inline struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return (struct k_work_q *)0; }
