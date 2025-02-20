/*
* Copyright (c) 2015-2025 Cadence Design Systems Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*******************************************************************************
 * osal-timer.h
 *
 * OS absraction layer (minimalistic) for Zephyr
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/

#ifndef _OSAL_TIMER_H
#define _OSAL_TIMER_H

#include <zephyr/kernel.h>

/*******************************************************************************
 * Timer support
 ******************************************************************************/

typedef void xf_timer_fn_t(void *arg);
typedef struct {
    struct k_timer timer;
    xf_timer_fn_t *fn;
    void *arg;
    int autoreload;
} xf_timer_t;

static void __xf_zephyr_timer_wrapper(struct k_timer *t)
{
    xf_timer_t *timer = CONTAINER_OF(t, xf_timer_t, timer);
    timer->fn(timer->arg);
}

static inline int __xf_timer_init(xf_timer_t *timer, xf_timer_fn_t *fn,
                                  void *arg, int autoreload)
{
    timer->fn = fn;
    timer->arg = arg;
    timer->autoreload = autoreload;
    k_timer_init(&timer->timer, __xf_zephyr_timer_wrapper, NULL);
    return 0;
}

static inline unsigned long __xf_timer_ratio_to_period(unsigned long numerator,
                                                       unsigned long denominator)
{
    return K_USEC(numerator * 1000000ull / denominator).ticks;
}

static inline int __xf_timer_start(xf_timer_t *timer, unsigned long period)
{
    k_timer_start(&timer->timer, K_TICKS(period), (timer->autoreload ? K_TICKS(period) : K_NO_WAIT));
    return 0;
}

static inline int __xf_timer_stop(xf_timer_t *timer)
{
    k_timer_stop(&timer->timer);
    return 0;
}

static inline int __xf_timer_destroy(xf_timer_t *timer)
{
    return 0;
}

#if 0
static inline void __xf_sleep(unsigned long period)
{
    k_sleep(period);
}
#endif

#endif
