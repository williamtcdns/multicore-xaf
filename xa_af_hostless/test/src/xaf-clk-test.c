/*
* Copyright (c) 2015-2024 Cadence Design Systems Inc.
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
/* Clock functions. These are used for cycle measurements */

// Includes
#ifdef XAF_PROFILE
#ifdef __XCC__
#if defined(HAVE_XOS)
#ifdef __TOOLS_RF2__
#include <xtensa/xos/xos.h>
#else   // #ifdef __TOOLS_RF2__
#include <xtensa/xos.h>
#endif  // #ifdef __TOOLS_RF2__
#endif
#endif  // #ifdef __XCC__

#include "xaf-api.h"
#include "xaf-clk-test.h"
#include <sys/times.h>

#if defined(HAVE_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

#if defined(HAVE_ZEPHYR)
#include <zephyr/kernel.h>
#endif

/* XOS_OPT_STATS is enabled in RG.5+ tool chains. To get the clock cycles from thread stats. */
#if !XOS_OPT_STATS
// Comment to use thread stats based measurement
#define CLK_TEST_DISABLE_SCHEDULING
#endif

// Uncomment to check scheduling
#define CLK_TEST_CHK_SCHEDULING

#ifdef CLK_TEST_CHK_SCHEDULING

#include <stdlib.h>

#include "xaf-fio-test.h"

// Globals
int clk_thread_id;
#endif /* CLK_TEST_CHK_SCHEDULING */
int clk_start_flag, clk_thread_flag;
extern long long tot_cycles, frmwk_cycles, fread_cycles, fwrite_cycles,dsp_comps_cycles,capturer_cycles, renderer_cycles;
// Start
void clk_start(void)
{
    clk_start_flag = 1;
}

// Stop
void clk_stop(void)
{
    clk_start_flag = 0;
}

clk_t clk_read_start(clk_seln_t seln)
{
    clk_t ret_val;

    if (!clk_start_flag)         // Time counters not started
        return 0;

#if defined(HAVE_XOS)
#ifdef CLK_TEST_DISABLE_SCHEDULING
    /* Scheduling disabled, as thread specific times are not available in RF.2 */
    if (seln == CLK_SELN_THREAD)
    {
        xos_preemption_disable();
#ifdef CLK_TEST_CHK_SCHEDULING
        int thread_id = (int)xos_thread_id();
        if (clk_thread_flag == 0)
            clk_thread_id = thread_id;
        else if (thread_id ^ clk_thread_id)
        {
            FIO_PRINTF(stderr,"Scheduling error: 0x%x, 0x%x\n", thread_id, clk_thread_id);
            exit(-1);
        }
#endif /* CLK_TEST_CHK_SCHEDULING */

    }
#else /* CLK_TEST_DISABLE_SCHEDULING */
    /* This requires XOS_OPT_STATS to be enabled. This is enabled by default in RF.3 */
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        XosThreadStats stats = {0};
        xos_thread_get_stats(xos_thread_id(), &stats);
        ret_val = (clk_t)stats.cycle_count;
    }
    else // CLK_SELN_WALL: Total time elapsed
#endif /* CLK_TEST_DISABLE_SCHEDULING */
    {

        unsigned int intr_lvl_stat= 0;
		intr_lvl_stat = xos_disable_interrupts();
        ret_val =  (clk_t)xos_get_system_cycles();
		xos_restore_interrupts(intr_lvl_stat);

    }

    return ret_val;
#elif defined(HAVE_FREERTOS)
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        TaskHandle_t task;
        TaskStatus_t status;
        eTaskState state = eRunning;
        task = xTaskGetCurrentTaskHandle();
        vTaskGetTaskInfo(task, &status,0, state);
        ret_val = (clk_t)status.ulRunTimeCounter;
        return ret_val;
    }
    return 0;
#elif defined(HAVE_ZEPHYR)
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        k_thread_runtime_stats_t rt_stats;
        k_thread_runtime_stats_get(k_current_get(), &rt_stats);
        ret_val = rt_stats.execution_cycles;
        return ret_val;
    }
    return 0;
#else
#error "error: RTOS is neither Zephyr, XOS nor FreeRTOS"
#endif
}

clk_t clk_read_stop(clk_seln_t seln)
{
    clk_t ret_val;

    if (!clk_start_flag)         // Time counters not started
        return 0;

#if defined(HAVE_XOS)
#ifdef CLK_TEST_DISABLE_SCHEDULING
    /* Scheduling disabled, as thread specific times are not available in RF.2 */
    if (seln == CLK_SELN_THREAD)
    {

#ifdef CLK_TEST_CHK_SCHEDULING
        int thread_id = (int)xos_thread_id();
        if (clk_thread_flag == 0)
            clk_thread_id = thread_id;
        else if (thread_id ^ clk_thread_id)
        {
            FIO_PRINTF(stderr,"Scheduling error: 0x%x, 0x%x\n", thread_id, clk_thread_id);
            exit(-1);
        }
#endif /* CLK_TEST_CHK_SCHEDULING */

    }
#else /* CLK_TEST_DISABLE_SCHEDULING */
    /* This requires XOS_OPT_STATS to be enabled. This is enabled by default in RF.3 */
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        XosThreadStats stats = {0};
        xos_thread_get_stats(xos_thread_id(), &stats);
        ret_val = (clk_t)stats.cycle_count;
    }
    else // CLK_SELN_WALL: Total time elapsed
#endif /* CLK_TEST_DISABLE_SCHEDULING */
    {
        unsigned int intr_lvl_stat= 0;
		intr_lvl_stat = xos_disable_interrupts();
        ret_val =  (clk_t)xos_get_system_cycles();
		xos_restore_interrupts(intr_lvl_stat);

    }

#ifdef CLK_TEST_DISABLE_SCHEDULING
    /* Re-enable scheduling */
	if (seln == CLK_SELN_THREAD)
	{
        xos_preemption_enable();
	}

#endif /* CLK_TEST_DISABLE_SCHEDULING */

    return ret_val;
#elif defined(HAVE_FREERTOS)
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        TaskHandle_t task;
        TaskStatus_t status;
        eTaskState state = eRunning;
        task = xTaskGetCurrentTaskHandle();
        vTaskGetTaskInfo(task, &status,0, state);
        ret_val = (clk_t)status.ulRunTimeCounter;
        return ret_val;
    }
    return 0;
#elif defined(HAVE_ZEPHYR)
    if (seln == CLK_SELN_THREAD) // Time elapsed in this thread
    {
        k_thread_runtime_stats_t rt_stats;
        k_thread_runtime_stats_get(k_current_get(), &rt_stats);
        ret_val = rt_stats.execution_cycles;
        return ret_val;
    }
    return 0;
#else
#error "error: RTOS is neither Zephyr, XOS nor FreeRTOS"
#endif
}


// Compute the difference
clk_t clk_diff(clk_t stop, clk_t start)
{
    // TODO: Tackle wraparound
    if ((start == 0) || (stop == 0))
        return 0;
    else
        return (stop - start);
}

// Compute total and framework cycles
clk_t compute_total_frmwrk_cycles()
{
#if defined(HAVE_XOS)
    int num_thread = 20,i;
	frmwk_cycles = 0;
	tot_cycles = 0;
	XosThreadStats status_th[20];
	xos_get_cpu_load(status_th,&num_thread,0);

	for(i =0;i<num_thread;i++)
	{
		tot_cycles = tot_cycles + status_th[i].cycle_count;
//        if(i<(num_thread-1)) fprintf(stderr,"th:%s preempt:%d normal:%d\n", status_th[i].thread->name, status_th[i].preempt_switches, status_th[i].normal_switches);
    }
    frmwk_cycles = tot_cycles;
    frmwk_cycles -= status_th[0].cycle_count; /* main task */
    frmwk_cycles -= status_th[1].cycle_count; /* IDLE task */
    frmwk_cycles -= status_th[num_thread-1].cycle_count; /* Timer service */

	return tot_cycles;

#elif defined(HAVE_FREERTOS)
    int i;
    uint32_t ulTotalRunTime;
    long long cycles;
	frmwk_cycles = 0;

    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
	tot_cycles = 0;

    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    /* Generate raw status information about each task. */
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime );

    for(i = 0; i < uxArraySize; i++)
    {
        cycles = pxTaskStatusArray[ i ].ulRunTimeCounter;
        tot_cycles += cycles;

        frmwk_cycles += cycles;
        if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "IDLE",4))
        {
            frmwk_cycles -= cycles;
        }
        else if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "main ta",7))
        {
            frmwk_cycles -= cycles;
        }
        else if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "Tmr Svc",7))
        {
            frmwk_cycles -= cycles;
        }
    }

    vPortFree( pxTaskStatusArray );

    return tot_cycles;
#elif defined(HAVE_ZEPHYR)
    k_thread_runtime_stats_t rt_stats;
    k_thread_runtime_stats_all_get(&rt_stats);
    tot_cycles = rt_stats.total_cycles;
    frmwk_cycles = rt_stats.execution_cycles;
    return tot_cycles;
#else
#error "error: RTOS is neither Zephyr, XOS nor FreeRTOS"
#endif   /* #if defined(HAVE_XOS) */
}

/* ...The function whose pointer is provided as callback function in xaf_adev_config_t structure.
 * Worker DSP calls his before dsp-thread is deleted, to update thread cycles.
 * Thread cycles are used to caclculate Framework MCPS. */

int cb_total_frmwrk_cycles(xaf_perf_stats_t *pstats)
{
#if defined(HAVE_XOS)
    	int num_thread = 20,i;
	pstats->frmwk_cycles = 0;
	//pstats->tot_cycles = 0; /* ...not to be reset here, it may already contain from worker thread cycles of DSP core */
	XosThreadStats status_th[20];
	xos_get_cpu_load(status_th,&num_thread,0);

    /* ..the function can be called 2 times. Once from DSP-thread deinit, then from dev/dsp-close */
    if((!pstats->tot_cycles)
#if (XF_CFG_CORES_NUM > 1)
 	&& (pstats->dsp_shmem_buf_size_peak[XAF_MEM_ID_DSP] == 0) /* ...to avoid considering the call as 1st, if not called from deinit and to prevenet print_worker_stats function hang */
#endif
    )
    {
	    for(i =0;i<(num_thread-1);i++)
	    {
            if(!strncmp(status_th[i].thread->name, "DSP-worker",10))
            {
		        pstats->tot_cycles += status_th[i].cycle_count;
                //fprintf(stderr,"L#%d %s thread:%s preempt_switches:%d normal_switches:%d\n", __LINE__, __func__, status_th[i].thread->name, status_th[i].preempt_switches, status_th[i].normal_switches);
            }
        }
        return 0;
    }

	for(i =0;i<num_thread;i++)
	{
		pstats->tot_cycles += status_th[i].cycle_count;
        //fprintf(stderr,"L#%d %s thread:%s preempt_switches:%d normal_switches:%d\n", __LINE__, __func__, status_th[i].thread->name, status_th[i].preempt_switches, status_th[i].normal_switches);
    }
    pstats->frmwk_cycles = pstats->tot_cycles;
    pstats->frmwk_cycles -= status_th[0].cycle_count; /* main task */
    pstats->frmwk_cycles -= status_th[1].cycle_count; /* IDLE task */
    pstats->frmwk_cycles -= status_th[num_thread-1].cycle_count; /* Timer service */

    return 0;
#elif defined(HAVE_FREERTOS)
    int i;
    uint32_t ulTotalRunTime;
    long long cycles;
	pstats->frmwk_cycles = 0;

    TaskStatus_t *pxTaskStatusArray;
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
	//pstats->tot_cycles = 0; /* ...not to be reset here, it may already contain from worker thread cycles of DSP core */

    pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    /* Generate raw status information about each task. */
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime );

    /* .. can be called 2 times. Once from DSP-thread deinit, then from dev/dsp-close */
    if((!pstats->tot_cycles)
#if (XF_CFG_CORES_NUM > 1)
 	&& (pstats->dsp_shmem_buf_size_peak[XAF_MEM_ID_DSP] == 0) /* ...to avoid considering the call as 1st, if not called from deinit and to prevenet print_worker_stats function hang */
#endif
    )
    {
        for(i = 0; i < uxArraySize; i++)
        {
            cycles = pxTaskStatusArray[ i ].ulRunTimeCounter;
            if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "DSP-worker",10))
            {
                pstats->tot_cycles += cycles;
            }
        }
        return 0;
    }

    pstats->frmwk_cycles = pstats->tot_cycles;
    for(i = 0; i < uxArraySize; i++)
    {
        cycles = pxTaskStatusArray[ i ].ulRunTimeCounter;
        pstats->tot_cycles += cycles;

        pstats->frmwk_cycles += cycles;
        if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "IDLE",4))
        {
            pstats->frmwk_cycles -= cycles;
        }
        else if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "main ta",7))
        {
            pstats->frmwk_cycles -= cycles;
        }
        else if(!strncmp(pxTaskStatusArray[ i ].pcTaskName, "Tmr Svc",7))
        {
            pstats->frmwk_cycles -= cycles;
        }
    }

    vPortFree( pxTaskStatusArray );

    return 0;
#elif defined(HAVE_ZEPHYR)
    k_thread_runtime_stats_t rt_stats;
    k_thread_runtime_stats_all_get(&rt_stats);
    pstats->frmwk_cycles = rt_stats.execution_cycles;
    return 0;
#else
#error "error: RTOS is neither Zephyr, XOS nor FreeRTOS"
#endif   /* #if defined(HAVE_XOS) */
}

#endif //XAF_PROFILE
