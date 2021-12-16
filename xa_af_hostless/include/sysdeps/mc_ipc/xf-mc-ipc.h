/*
* Copyright (c) 2015-2021 Cadence Design Systems Inc.
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
 * xf-ipc.h
 *
 * IPC absraction layer (minimalistic)
 ******************************************************************************/

/*******************************************************************************
 * Includes
 ******************************************************************************/
#ifndef _XF_IPC_H
#define _XF_IPC_H

#include "osal-thread.h"

/*******************************************************************************
 * Core compatibility check macros
 ******************************************************************************/
#if (!XCHAL_HAVE_S32C1I)
#error "core configuration: S32C1I is absent"
#endif

#if (!XCHAL_HAVE_PIF_WR_RESP)
#error "core configuration: PIFWriteResponse is absent"
#endif

#if (XCHAL_NUM_INTERRUPTS<2)
#error "core configuration: need at least 2 edge triggered interrupts"
#endif

/*******************************************************************************
 * IPC data type definitions
 ******************************************************************************/
typedef struct xf_ipc_lock_s {
    uint8_t buf[2*XCHAL_DCACHE_LINESIZE];
} xf_ipc_lock_t;

#define LOCK_ADDR(lock) (((uint32_t)&(lock->buf[0])+XCHAL_DCACHE_LINESIZE-1) & ~(XCHAL_DCACHE_LINESIZE-1))

/*******************************************************************************
 * Helper functions
 ******************************************************************************/

static inline void __delay(int32_t delay_count)
{
    int32_t i;
#pragma loop_count min=1
    for (i = 0; i < delay_count; i++)
        asm volatile ("_nop");
}

#if XCHAL_HAVE_S32C1I

/* If successful, returns 'from' else returns value that is not 'from' */
static inline uint32_t __atomic_conditional_set(volatile uint32_t *addr, uint32_t from, uint32_t to)
{
#if XCHAL_DCACHE_SIZE>0 && !XCHAL_DCACHE_IS_COHERENT
    xthal_dcache_line_invalidate((void *)addr);
#endif
    uint32_t val = *addr;
    
    if (val == from) {
        XT_WSR_SCOMPARE1(from);
        val = to;
        XT_S32C1I(val, (uint32_t *)addr, 0);
    }

#if XCHAL_DCACHE_SIZE>0 && !XCHAL_DCACHE_IS_COHERENT
    //xthal_dcache_line_writeback((void *)addr);
#endif

    return val;
}
#endif

/*******************************************************************************
 * Lock operation
 ******************************************************************************/

/* ...lock definition */
//typedef uint32_t xf_ipc_lock_t __attribute__ ((aligned(XCHAL_DCACHE_LINESIZE)));

/* ...lock initialization */
static inline uint32_t __xf_ipc_lock_init(xf_ipc_lock_t *lock)
{
	return 0;
}

/* ...lock destroy */
static inline uint32_t __xf_ipc_lock_destroy(xf_ipc_lock_t *lock)
{
	return 0;
}

/* ...lock acquisition */
static inline uint32_t __xf_ipc_lock(xf_ipc_lock_t *lock)
{
	uint32_t *lock_ptr = (uint32_t *) LOCK_ADDR(lock);
   
    __xf_disable_preemption();
    uint32_t pid = XT_RSR_PRID();

    while (__atomic_conditional_set((volatile uint32_t *)lock_ptr, 0, pid + 1) != 0)
        __delay(16);
 
	return 0;
}

/* ...lock release */
static inline uint32_t __xf_ipc_unlock(xf_ipc_lock_t *lock)
{
	uint32_t *lock_ptr = (uint32_t *) LOCK_ADDR(lock);
   
    uint32_t pid = XT_RSR_PRID();
    while (__atomic_conditional_set((volatile uint32_t *)lock_ptr, pid + 1, 0) != pid + 1)
        __delay(16);
    
    __xf_enable_preemption();
	
    return 0;
}

static inline void __proc_notify(uint32_t core)
{
    switch(core)
    {
        case 0:
        *((volatile unsigned *) XMP_core0_BINTR7_ADDR) = XMP_core0_BINTR7_MASK;
        *((volatile unsigned *) XMP_core0_BINTR7_ADDR) = 0;
        break;

#if XF_CFG_CORES_NUM>1
        case 1:
        *((volatile unsigned *) XMP_core1_BINTR7_ADDR) = XMP_core1_BINTR7_MASK;
        *((volatile unsigned *) XMP_core1_BINTR7_ADDR) = 0;
        break;
#endif

#if XF_CFG_CORES_NUM>2
        case 2:
        *((volatile unsigned *) XMP_core2_BINTR7_ADDR) = XMP_core2_BINTR7_MASK;
        *((volatile unsigned *) XMP_core2_BINTR7_ADDR) = 0;
        break;
#endif

#if XF_CFG_CORES_NUM>3
        case 3:
        *((volatile unsigned *) XMP_core3_BINTR7_ADDR) = XMP_core3_BINTR7_MASK;
        *((volatile unsigned *) XMP_core3_BINTR7_ADDR) = 0;
        break;
#endif
        default:
            /* ... log/notify error */
        break;
    }
}

#endif //_XF_IPC_H
