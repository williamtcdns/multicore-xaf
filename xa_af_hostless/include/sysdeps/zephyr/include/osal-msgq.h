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
#ifndef _OSAL_MSGQ_H
#define _OSAL_MSGQ_H

#include <zephyr/kernel.h>

/*******************************************************************************
 * Global Definitions
 ******************************************************************************/

typedef struct {
    struct k_msgq queue;
} *xf_msgq_t;

static inline int __xf_msgq_get_size(size_t n_items, size_t item_size)
{
    xf_msgq_t q;
    return (ROUND_UP(sizeof(*q), sizeof(void *)) + (n_items * item_size));
}

/* ...open proxy interface on proper DSP partition */
static inline xf_msgq_t __xf_msgq_create(size_t n_items, size_t item_size, void *pbuf)
{
    xf_msgq_t q = (xf_msgq_t)pbuf;
    k_msgq_init(&q->queue, (pbuf + ROUND_UP(sizeof(*q), sizeof(void *))), item_size, n_items);
    return q;
}

/* ...close proxy handle */
static inline void __xf_msgq_destroy(xf_msgq_t q)
{
    k_msgq_purge(&q->queue);
}

static inline int __xf_msgq_send(xf_msgq_t q, const void *data, size_t sz)
{
    return ((k_msgq_put(&q->queue, data, K_FOREVER) == 0) ? XAF_NO_ERR : XAF_RTOS_ERR);
}

#define MAXIMUM_TIMEOUT 10000

static inline int __xf_msgq_recv_blocking(xf_msgq_t q, void *data, size_t sz)
{
    int ret = k_msgq_get(&q->queue, data, K_FOREVER);

    if ( ret == 0 )
    {
        ret = XAF_NO_ERR;
    }
    else
    {
        ret = XAF_RTOS_ERR;
    }

    return ret;
}

static inline int __xf_msgq_recv(xf_msgq_t q, void *data, size_t sz)
{
    int ret = k_msgq_get(&q->queue, data, K_MSEC(MAXIMUM_TIMEOUT));

    if ( ret == 0 )
    {
        ret = XAF_NO_ERR;
    }
    else
    {
        ret = XAF_TIMEOUT_ERR;
    }

    return ret;
}

static inline int __xf_msgq_empty(xf_msgq_t q)
{
    return (k_msgq_num_used_get(&q->queue) == 0);
}

static inline int __xf_msgq_full(xf_msgq_t q)
{
    return (k_msgq_num_free_get(&q->queue) == 0);
}

#endif
