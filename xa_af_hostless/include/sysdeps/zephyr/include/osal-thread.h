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
 * osal-thread.h
 *
 * OS absraction layer (minimalistic) for Zephyr
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/

#ifndef _OSAL_THREAD_H
#define _OSAL_THREAD_H

#include <string.h>
#include <stdlib.h>

#include <zephyr/kernel.h>

/*******************************************************************************
 * Tracing primitive
 ******************************************************************************/

#define __xf_puts(str)                  \
    puts((str))

/*******************************************************************************
 * Lock operation
 ******************************************************************************/

/* ...lock definition */
typedef struct k_sem xf_lock_t;

/* ...lock initialization */
static inline void __xf_lock_init(xf_lock_t *lock)
{
   k_sem_init(lock, 1, 1);
}

/* ...lock deletion */
static inline void __xf_lock_destroy(xf_lock_t *lock)
{
    k_sem_reset(lock);
}

/* ...lock acquisition */
static inline void __xf_lock(xf_lock_t *lock)
{
    k_sem_take(lock, K_FOREVER);
}

/* ...lock release */
static inline void __xf_unlock(xf_lock_t *lock)
{
    k_sem_give(lock);
}

/*******************************************************************************
 * Event support
 ******************************************************************************/

typedef struct k_event xf_event_t;

static inline void __xf_event_init(xf_event_t *event, uint32_t mask)
{
    k_event_init(event);
}

static inline void __xf_event_destroy(xf_event_t *event)
{
    k_event_clear(event, -1);
}

static inline uint32_t __xf_event_get(xf_event_t *event)
{
    return k_event_test(event, -1);
}

static inline void __xf_event_set(xf_event_t *event, uint32_t mask)
{
    k_event_post(event, mask);
}

static inline void __xf_event_set_isr(xf_event_t *event, uint32_t mask)
{
    k_event_post(event, mask);
}

static inline void __xf_event_clear(xf_event_t *event, uint32_t mask)
{
    k_event_clear(event, mask);
}

static inline void __xf_event_wait_any(xf_event_t *event, uint32_t mask)
{
    k_event_wait(event, mask, false, K_FOREVER);
}

static inline void __xf_event_wait_all(xf_event_t *event, uint32_t mask)
{
    k_event_wait_all(event, mask, false, K_FOREVER);
}

/*******************************************************************************
 * Thread support
 ******************************************************************************/

/* ...thread handle definition */
typedef struct {
    struct k_thread thrd;
    k_tid_t tid;
    k_thread_stack_t *stack;
    bool is_allocated_stack;
} xf_thread_t;
typedef void *xf_entry_t(void *);

static inline int __xf_thread_init(xf_thread_t *thread)
{
    return 0;
}

/* ...thread creation */
static inline int __xf_thread_create(xf_thread_t *thread, xf_entry_t *f,
                                     void *arg, const char *name, void * stack,
                                     unsigned int stack_size, int priority)
{
    if (!stack) {
        stack = malloc(stack_size);
        if (!stack)
            return -1;
        thread->is_allocated_stack = 1;
    } else {
        thread->is_allocated_stack = 0;
    }
    thread->stack = stack;
    void *aligned_stack = (void*)ROUND_UP((unsigned long)stack, Z_KERNEL_STACK_OBJ_ALIGN);
    thread->tid = k_thread_create(&thread->thrd,
                                  aligned_stack,
                                  ROUND_DOWN((stack_size - ((unsigned long)aligned_stack - (unsigned long)stack)),
                                             Z_KERNEL_STACK_OBJ_ALIGN),
                                  (k_thread_entry_t)f,
                                  arg, 0, 0,
                                  ((priority >= CONFIG_NUM_PREEMPT_PRIORITIES) ?
                                      0 : ((CONFIG_NUM_PREEMPT_PRIORITIES -1) - priority)),
                                  0, K_NO_WAIT);
    if (name)
        k_thread_name_set(thread->tid, name);
    return 0;
}

static inline void __xf_thread_yield(void)
{
    k_yield();
}

static inline int __xf_thread_cancel(xf_thread_t *thread)
{
    if (!thread->tid)
        return -1;
    k_thread_abort(thread->tid);
    return 0;
}

/* TENA-2117*/
static inline int __xf_thread_join(xf_thread_t *thread, int32_t * p_exitcode)
{
    if (!thread->tid || k_thread_join(&thread->thrd, K_FOREVER) < 0)
        return -1;
    if (p_exitcode)
        *p_exitcode = 0;
    return 0;
}

/* ...terminate thread operation */
static inline int __xf_thread_destroy(xf_thread_t *thread)
{
    if (thread->is_allocated_stack) {
        free(thread->stack);
    }
    return 0;
}

static inline const char *__xf_thread_name(xf_thread_t *thread)
{
    return k_thread_name_get(thread ? thread->tid : k_current_get());
}

/* ... Put thread to sleep for at least the specified number of msec */
static inline int32_t __xf_thread_sleep_msec(uint64_t msecs)
{
    k_sleep(K_MSEC(msecs));
    return 0;
}

/* ... state of the thread */
#define XF_THREAD_STATE_READY   (0)
#define XF_THREAD_STATE_RUNNING (1)
#define XF_THREAD_STATE_BLOCKED (2)
#define XF_THREAD_STATE_EXITED  (3)
#define XF_THREAD_STATE_INVALID (4)

static inline int32_t __xf_thread_get_state (xf_thread_t *thread)
{
    if (!thread->tid)
        return XF_THREAD_STATE_INVALID;
    int thread_state = thread->tid->base.thread_state;
    if (!(thread_state & (
        (1<<0) | /* DUMMY */
        (1<<1) | /* PENDING */
        (1<<2) | /* PRESTART */
        (1<<3) | /* DEAD */
        (1<<4) | /* SUSPENDED */
        (1<<5) | /* ABORTING */
        (1<<6)   /* SUSPENDING */)))
        return XF_THREAD_STATE_READY;
    if (!(thread_state & (
        (1<<0) | /* DUMMY */
        (1<<1) | /* PENDING */
        (1<<2) | /* PRESTART */
        (1<<3) | /* DEAD */
        (1<<4) | /* SUSPENDED */
        (1<<5) | /* ABORTING */
        (1<<6) | /* SUSPENDING */
        (1<<7)   /* QUEUED */)))
        return XF_THREAD_STATE_RUNNING;
    if (thread_state & (
        (1<<1) | /* PENDING */
        (1<<4) | /* SUSPENDED */
        (1<<6)   /* SUSPENDING */))
        return XF_THREAD_STATE_BLOCKED;
    if (thread_state & (
        (1<<3) | /* DEAD */
        (1<<5)   /* ABORTING */))
        return XF_THREAD_STATE_EXITED;
    return XF_THREAD_STATE_INVALID;
}

static inline void __xf_disable_preemption(void)
{
    k_sched_lock();
}

static inline void __xf_enable_preemption(void)
{
    k_sched_unlock();
}

static inline int32_t __xf_thread_get_priority (xf_thread_t *thread)
{
    return k_thread_priority_get(thread ? thread->tid : k_current_get());
}

static inline int32_t __xf_thread_set_priority (xf_thread_t *thread, int32_t priority)
{
    k_thread_priority_set((thread ? thread->tid : k_current_get()),
        ((priority >= CONFIG_NUM_PREEMPT_PRIORITIES) ?
            0 : ((CONFIG_NUM_PREEMPT_PRIORITIES -1) - priority)));
    return 0;
}

#endif
