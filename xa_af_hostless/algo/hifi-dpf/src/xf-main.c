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
 * xf-main.c
 *
 * DSP processing framework main file
 ******************************************************************************/

#define MODULE_TAG                      MAIN

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "xf-dp.h"

/*******************************************************************************
 * Global data definition
 ******************************************************************************/

xf_dsp_t *xf_g_dsp;

/*******************************************************************************
 * Timer interrupt - tbd
 ******************************************************************************/

/*******************************************************************************
 * IPC layer initialization
 ******************************************************************************/

/* ...system-specific IPC layer deinitialization */
int xf_ipc_deinit(UWORD32 core)
{
    xf_core_data_t  *cd = XF_CORE_DATA(core);

    XF_CHK_API(xf_mm_deinit(&cd->shared_pool));

    return 0;
}

/* ...system-specific IPC layer initialization */
int xf_ipc_init(UWORD32 core)
{
    xf_core_data_t  *cd = XF_CORE_DATA(core);
    xf_core_ro_data_t  *ro = XF_CORE_RO_DATA(core);

    xf_shmem_data_t *shmem = (xf_shmem_data_t *) xf_g_dsp->xf_ap_shmem_buffer;
    //xf_shmem_data_t *shmem = (xf_shmem_data_t *) malloc(XF_CFG_REMOTE_IPC_POOL_SIZE + 4095);
    //shmem = (xf_shmem_data_t *)((int) (shmem+4095) & 0xfffff000);

    /* ...initialize pointer to shared memory */
    cd->shmem = (xf_shmem_handle_t *)shmem;

    /* ...global memory pool initialization */

    XF_CHK_API(xf_mm_init(&cd->shared_pool, (shmem->buffer), (xf_g_dsp->xf_ap_shmem_buffer_size-(sizeof(xf_shmem_data_t)-XF_CFG_REMOTE_IPC_POOL_SIZE))));

    /* ...open xos-msgq interface */
    XF_CHK_API(ipc_msgq_init(&ro->ipc.cmd_msgq, &ro->ipc.resp_msgq, &ro->ipc.msgq_event));

    return 0;
}

/*******************************************************************************
 * Core executive loop
 ******************************************************************************/
#if (XF_CFG_CORES_NUM > 1)
static void xf_core_loop(UWORD32 core, xf_ipc_config_t *pipc_cfg)
#else
static void xf_core_loop(UWORD32 core)
#endif
{

    /* ...initialize internal core structures */
    xf_core_init(core);

#if (XF_CFG_CORES_NUM > 1)
    /* ...enable IPC interrupts from other cores */
	__xf_enable_interrupt(INT_NUMBER);
#endif

    if(core == XF_CORE_ID_MASTER)
    {
       /* ...wait in a low-power mode until event is triggered */
 	   while ( xf_ipi_wait(core)){
 
           /* ...service core event */
 	       xf_core_service(core);
 	   }
    }
#if (XF_CFG_CORES_NUM > 1)
    else
    {
       /* ...wait in a low-power mode until interrupt is triggered */
 	   while ( xf_ipc_wait2(core)){
 
            /* ...service core event */
 	       xf_core_service(core);
 	   }
    }
#endif

    xf_core_deinit(core);
}

/*******************************************************************************
 * DSP entry point
 ******************************************************************************/

static int _dsp_thread_entry(void *arg)
{

    int aligned_offset;
    void *(*dsp_args)[XAF_NUM_THREAD_ARGS] = arg;
    UWORD32 core = *(UWORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_CORE_ID];

    {
        UWORD8 *pxf_dsp_local_buffer = (UWORD8 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_DSP_LOCAL_BUF];
        UWORD32 xf_dsp_local_buffer_size = *(UWORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_DSP_LOCAL_BUF_SIZE];

	    xf_g_dsp = (xf_dsp_t *)pxf_dsp_local_buffer;
        aligned_offset = XF_MM(XF_DSP_STRUCT_SIZE /* sizeof(xf_dsp_t) */);
	    pxf_dsp_local_buffer += aligned_offset;
	    xf_dsp_local_buffer_size -= aligned_offset;

        xf_g_dsp->xf_dsp_local_buffer       = pxf_dsp_local_buffer;
        xf_g_dsp->xf_dsp_local_buffer_size  = xf_dsp_local_buffer_size;
    }

    if(core == XF_CORE_ID_MASTER)
    {
    	xf_g_dsp->xf_ap_shmem_buffer        = (UWORD8 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_AP_SHMEM_BUF];
    	xf_g_dsp->xf_ap_shmem_buffer_size   = *(UWORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_AP_SHMEM_BUF_SIZE];
    }

    xf_g_dsp->pdsp_comp_buf_size_peak  = (WORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_STATS_COMP_BUF_PEAK];
    xf_g_dsp->pdsp_comp_buf_size_curr  = (WORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_STATS_COMP_BUF_CURR];
    xf_g_dsp->pdsp_frmwk_buf_size_peak = (WORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_STATS_FRMWK_BUF_PEAK];
    xf_g_dsp->pdsp_frmwk_buf_size_curr = (WORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_STATS_FRMWK_BUF_CURR];

    memcpy(XF_CORE_DATA(core)->worker_thread_scratch_size, (UWORD32 (*)[XAF_MAX_WORKER_THREADS])(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_WORKER_SCRATCH_SIZE], sizeof(XF_CORE_DATA(core)->worker_thread_scratch_size));

    /* ...reset ro/rw core data - tbd */
    memset(xf_g_dsp->xf_core_rw_data, 0, sizeof(xf_g_dsp->xf_core_rw_data));
    memset(xf_g_dsp->xf_core_ro_data, 0, sizeof(xf_g_dsp->xf_core_ro_data));

    TRACE(INFO, _b("Xtensa Audio DSP Codec Server"));

    /* ...initialize board */
    xf_board_init();

    /* ...global framework data initialization */
    xf_global_init();

    /* ...initialize per-core memory */
    XF_CHK_API(xf_mm_init(&(XF_CORE_DATA(core)->local_pool), xf_g_dsp->xf_dsp_local_buffer, xf_g_dsp->xf_dsp_local_buffer_size));

#if (XF_CFG_CORES_NUM > 1)
    xf_ipc_config_t ipc_cfg;

    {
        /* ...initialize per-core shared memory data structures for IPC */
        UWORD8 *pxf_dsp_shmem_buffer = (UWORD8 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_DSP_SHMEM_BUF];
        UWORD32 xf_dsp_shmem_buffer_size = *(UWORD32 *)(*dsp_args)[XF_DSP_THREAD_ARGS_IDX_DSP_SHMEM_BUF_SIZE];
        
        /* ...initialize IPC */
        memset(&ipc_cfg, 0, sizeof(ipc_cfg));

        ipc_cfg = (xf_ipc_config_t){.handle_size = xf_dsp_shmem_buffer_size,
        .phandle = (void *)pxf_dsp_shmem_buffer,
        .msg_next_offset = (UWORD32)offset_of(xf_message_t, next),
        .cd = XF_CORE_DATA(core),
        };

        xf_ipc_open2(core, &ipc_cfg);

        if(core == XF_CORE_ID_MASTER)
        {
            /* ...initialize shared memory heap */
            //XF_CHK_API(xf_ipc_mm_init(&xf_g_dsp->xf_dsp_shmem_pool, pxf_dsp_shmem_buffer, xf_dsp_shmem_buffer_size));
            XF_CHK_API(xf_ipc_mm_init(&(XF_SHMEM_IPC_HANDLE(core)->xf_dsp_shmem_pool), ipc_cfg.phandle, ipc_cfg.handle_size));
            
            /* ...worker core communication message pool */
            /* ...For now used only for core-deinit from master to worker DSPs */
            if (xf_msg_pool_init(&XF_CORE_DATA(core)->dsp_dsp_shmem_pool, XF_CFG_CORES_NUM-1, core, 1))
            {
                return XAF_INVALIDPTR_ERR;
            }
        }

        /* ...setup interrupt handler for IPC */
#if defined(HAVE_XOS)
        xos_register_interrupt_handler(INT_NUMBER, xf_ipc_interrupt_handler, &core);
#else
        xt_set_interrupt_handler(INT_NUMBER, xf_ipc_interrupt_handler, &core);
#endif
    }
#endif //XF_CFG_CORES_NUM 

#if  0 //(XF_CFG_CORES_NUM > 1)
    UWORD32     i;
    /* ...bring up all cores */
    for (i = 1; i < XF_CFG_CORES_NUM; i++)
    {
        /* ...wake-up secondary core somehow and make it execute xf_core_loop */
        xf_core_secondary_startup(i, xf_core_loop, i);
        
    }
#endif

    /* ...enter execution loop on master core #0 */
#if (XF_CFG_CORES_NUM > 1)
    xf_core_loop(core, &ipc_cfg);
#else
    xf_core_loop(core);
#endif

    /* ...deinitialize per-core memory loop */
    XF_CHK_API(xf_mm_deinit(&(XF_CORE_DATA(core)->local_pool)));

#if (XF_CFG_CORES_NUM > 1)
    xf_ipc_close2(core);

    if(core == XF_CORE_ID_MASTER)
    {
        xf_msg_pool_destroy(&XF_CORE_DATA(core)->dsp_dsp_shmem_pool, core, 1);

        /* ...deinit shared memory heap */
        //XF_CHK_API(xf_ipc_mm_deinit(&xf_g_dsp->xf_dsp_shmem_pool));
        XF_CHK_API(xf_ipc_mm_deinit(&(XF_SHMEM_IPC_HANDLE(core)->xf_dsp_shmem_pool)));
    }
#endif

    return 0;
}

void *dsp_thread_entry(void *arg)
{
    _dsp_thread_entry(arg);
    return NULL;
}
