/*
* Copyright (c) 2015-2023 Cadence Design Systems Inc.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xaf-utils-test.h"
#include "xaf-fio-test.h"

#define TICK_CYCLES       (10000)
#define CLK_FREQ      (100000000)

/* global data */
int g_continue_wait = 0;
int g_force_input_over[MAX_NUM_COMP_IN_GRAPH];
xaf_format_t comp_format_mcps;
int (*gpcomp_connect)(int comp_id_src, int port_src, int comp_id_dest, int port_dest, int create_delete_flag);
int (*gpcomp_disconnect)(int comp_id_src, int port_src, int comp_id_dest, int port_dest, int create_delete_flag);
UWORD32 g_active_disconnect_comp[MAX_NUM_COMP_IN_GRAPH];

xf_thread_t g_disconnect_thread;
int g_disc_thread_args[5];

int g_execution_abort_flag = 0;
event_list_t *g_event_list = NULL;
xa_app_event_handler_fxn_t *g_app_handler_fn;
UWORD32 worker_thread_scratch_size[XAF_MAX_WORKER_THREADS];

#ifndef XA_DISABLE_EVENT
UWORD32 g_enable_error_channel_flag = XAF_ERR_CHANNEL_DISABLE;
UWORD32 g_event_handler_exit = 0;
#endif

#if 1 //TENA-2376, TENA-2434
int g_num_comps_in_graph = 0;
xf_thread_t *g_comp_thread;
#endif

#if (XF_CFG_CORES_NUM == 1)
void *shared_mem = NULL;
char perf_stats[XF_SHMEM_SIZE_PERF_STATS];
#endif

/*... Global File pointer required for correct MCPS calculation */
FILE *mcps_p_input = NULL;
FILE *mcps_p_output = NULL;

extern unsigned int num_bytes_read, num_bytes_write;
int audio_frmwk_buf_size;
int audio_comp_buf_size;
extern double strm_duration;

/* ...comp config global variable (helps test application changes) */
xaf_comp_config_t comp_config;

#if 1 //TENA-2376, TENA-2434
int abort_blocked_threads()
{
    int i;

    /*...set global abort flag immediately */
    g_execution_abort_flag = 1;

    /* Ignore if not enabled in the testbench */
    if ( g_num_comps_in_graph == 0 )
        return -1;

    for( i=0; i<g_num_comps_in_graph; i++ )
    {
        if ( __xf_thread_get_state(&g_comp_thread[i]) == XF_THREAD_STATE_BLOCKED )
        {
            fprintf(stderr, "Aborting thread: %d\n", i);
            __xf_thread_cancel( (xf_thread_t *) &g_comp_thread[i] );
        }
    }

    if ( __xf_thread_get_state(&g_disconnect_thread) == XF_THREAD_STATE_BLOCKED )
    {
        fprintf(stderr, "Aborting disconnect thread\n");
        __xf_thread_cancel( &g_disconnect_thread );
    }

    return 0;
}
#endif

#ifdef XAF_PROFILE
    long long tot_cycles, frmwk_cycles, fread_cycles, fwrite_cycles;
    long long dsp_comps_cycles, enc_cycles, dec_cycles, mix_cycles, pcm_gain_cycles, src_cycles,capturer_cycles, renderer_cycles,aac_dec_cycles;
    long long aec22_cycles, aec23_cycles, pcm_split_cycles, mimo_mix_cycles;
    long long wwd_cycles, hbuf_cycles;
    long long inference_cycles, microspeech_fe_cycles;
    long long pd_inference_cycles;
    long long kd_inference_cycles;
    double dsp_mcps, xaf_mcps_master;
#endif

_XA_API_ERR_MAP error_map_table_api[XA_NUM_API_ERRS]=
{
    {(int)XAF_RTOS_ERR,       "rtos error"},
    {(int)XAF_INVALIDVAL_ERR,    "invalid value"},
    {(int)XAF_ROUTING_ERR,    "routing error"},
    {(int)XAF_INVALIDPTR_ERR,        "invalid pointer"},
    {(int)XAF_API_ERR,          "API error"},
    {(int)XAF_TIMEOUT_ERR,   "message queue Timeout"},
    {(int)XAF_MEMORY_ERR,   "memory error"},
};

#if 0
void xaf_update_mpu_entry(void)
{
#define MPU_ENTRY_START_INDEX   22
#define MPU_ENTRY_STOP_INDEX   XCHAL_MPU_ENTRIES
    xthal_MPU_entry entry[XCHAL_MPU_ENTRIES];
    int err;
    if((err = xthal_read_map(entry)) == XTHAL_SUCCESS)
    {
        int i=0;
        for(i=MPU_ENTRY_START_INDEX;i<MPU_ENTRY_STOP_INDEX;i++)
        {
            fprintf(stderr,"Readmap[%d] entry:%08x %08x\n", i, ((uint32_t *)&entry[i])[0], ((uint32_t *)&entry[i])[1]);
            fprintf(stderr,"## VSTARTADDR:%08x VALID:%08x ACCESS:%08x MEMORY_TYPE:%08x LOCK:%08x\n",
                XTHAL_MPU_ENTRY_GET_VSTARTADDR(entry[i]),
                XTHAL_MPU_ENTRY_GET_VALID(entry[i]),
                XTHAL_MPU_ENTRY_GET_ACCESS(entry[i]),
                XTHAL_MPU_ENTRY_GET_MEMORY_TYPE(entry[i]),
                XTHAL_MPU_ENTRY_GET_LOCK(entry[i]));
        }
    }
#if 0
    /* ...MPU set region attributes:  method1 */
    memcpy(&entry[23], &entry[24], sizeof(xthal_MPU_entry));
    memcpy(&entry[24], &entry[25], sizeof(xthal_MPU_entry));

    entry[25] = xthal_get_entry_for_address((void *)&shared_mem_ipc_lock_msgq[0][0], NULL);
    XTHAL_MPU_ENTRY_SET_MEMORY_TYPE(entry[25], XTHAL_MEM_NON_CACHEABLE|XTHAL_MEM_SYSTEM_SHAREABLE|XTHAL_MEM_BUFFERABLE);
    xthal_mpu_set_entry(entry[25]);

    xthal_write_map(entry, XCHAL_MPU_ENTRIES); /* ..this is must for changes to be effective */
#else
    /* ...MPU set region attributes:  method2 */
    xmp_set_mpu_attrs_region_uncached((void *)shared_mem_ipc_lock_msgq,  sizeof(shared_mem_ipc_lock_msgq));
    xmp_set_mpu_attrs_region_uncached((void *)shared_mem_ipc_lock_mem_pool,  sizeof(shared_mem_ipc_lock_mem_pool));
#endif
    err = xthal_check_map(entry, XCHAL_MPU_ENTRIES);
    if(err) fprintf(stderr,"#c[%d] xthal_check_map err:%d\n", XT_RSR_PRID(), err);

    if((err = xthal_read_map(entry)) == XTHAL_SUCCESS)
    {
        int i=0;
        for(i=MPU_ENTRY_START_INDEX;i<MPU_ENTRY_STOP_INDEX;i++)
        {
            fprintf(stderr,"Readmap2[%d] entry:%08x %08x\n", i, ((uint32_t *)&entry[i])[0], ((uint32_t *)&entry[i])[1]);
            fprintf(stderr,"## VSTARTADDR:%08x VALID:%08x ACCESS:%08x MEMORY_TYPE:%08x LOCK:%08x\n",
                XTHAL_MPU_ENTRY_GET_VSTARTADDR(entry[i]),
                XTHAL_MPU_ENTRY_GET_VALID(entry[i]),
                XTHAL_MPU_ENTRY_GET_ACCESS(entry[i]),
                XTHAL_MPU_ENTRY_GET_MEMORY_TYPE(entry[i]),
                XTHAL_MPU_ENTRY_GET_LOCK(entry[i]));
        }
    }
}
#endif

// Set cache attribute to Write Back No Allocate when the last argument is -wbna
void set_wbna(int *argc, char **argv)
{
    if ( *argc > 1 && !strcmp(argv[*argc-1], "-wbna") ) {
#ifdef __XCC__
        extern char _memmap_cacheattr_wbna_trapnull;

        xthal_set_cacheattr((unsigned)&_memmap_cacheattr_wbna_trapnull);
#endif
        (*argc)--;
    }
#if (XF_CFG_CORES_NUM>1)
/* ...if XF_LOCAL_IPC_NON_COHERENT is defined, it refers to caching is enabled */    
#if (!XF_LOCAL_IPC_NON_COHERENT) /* ...((CACHE_BYPASS)?(XF_LOCAL_IPC_NON_COHERENT=0):(XF_LOCAL_IPC_NON_COHERENT=1)); */
    {
        int err;
#if XCHAL_HAVE_EXCLUSIVE
        err = xaf_update_mpu();
        if(err != XTHAL_SUCCESS)
        {
          fprintf(stderr,"cache: set_attribute BYPASS error:%x\n", err);
        }
        else
        {
          fprintf(stderr,"cache: set_attribute BYPASS OK\n");
        }
#else //XCHAL_HAVE_EXCLUSIVE
        if( (err=xthal_set_region_attribute((void *)shared_mem, (XF_SHMEM_SIZE), XCHAL_CA_BYPASS, 0)) != XTHAL_SUCCESS)
        {
          fprintf(stderr,"cache: set_attribute BYPASS error:%x\n", err);
        }
        else
        {
          fprintf(stderr,"cache: set_attribute BYPASS attr:%08x mem:%p\n", xthal_get_cacheattr(), (void *)shared_mem);
        }
#endif //XCHAL_HAVE_EXCLUSIVE
    }
#elif (CACHE_WRITETHRU)
    {
        int err;
        if( (err=xthal_set_region_attribute((void *)shared_mem, (XF_SHMEM_SIZE), XCHAL_CA_WRITETHRU, 0)) != XTHAL_SUCCESS)
        {
          fprintf(stderr,"cache: set_attribute WRITETHRU error:%x\n", err);
        }
        else
        {
          fprintf(stderr,"cache: set_attribute WRITETHRU attr:%08x mem:%p\n", xthal_get_cacheattr(), (void *)shared_mem);
        }
    }
#elif XCHAL_HAVE_EXCLUSIVE
    //xaf_update_mpu_entry();
    {
        /* ...MPU set region attributes:  method2 */
        xmp_set_mpu_attrs_region_uncached((void *)shared_mem_ipc_lock_msgq,  sizeof(shared_mem_ipc_lock_msgq));
        xmp_set_mpu_attrs_region_uncached((void *)shared_mem_ipc_lock_mem_pool,  sizeof(shared_mem_ipc_lock_mem_pool));
        //xmp_set_mpu_attrs_region_uncached((void *)&_shared_sysram_uncached_data_start,  sizeof(shared_mem_ipc_lock_msgq)+sizeof(shared_mem_ipc_lock_mem_pool));
    }
#else
    //fprintf(stderr,"cache: set_attribute attr:%08x mem:%p wrthru:%08x wrbk:%08x bypass:%08x\n", xthal_get_cacheattr(), (void *)shared_mem, XCHAL_CA_WRITETHRU, XCHAL_CA_WRITEBACK, XCHAL_CA_BYPASS);
#endif
#endif //XF_CFG_CORES_NUM
}

int print_verinfo(pUWORD8 ver_info[],pUWORD8 app_name)
{
    TST_CHK_PTR(ver_info[0], "print_verinfo");
    TST_CHK_PTR(ver_info[1], "print_verinfo");
    TST_CHK_PTR(ver_info[2], "print_verinfo");

    FIO_PRINTF(stdout, "****************************************************************\n");
    FIO_PRINTF(stdout, "Cadence Audio Framework (Hostless) : %s \n",app_name);
    FIO_PRINTF(stdout, "Build: %s, RTOS: %s, On: %s %s\n", BUILD_STRING, BUILD_RTOS, __DATE__, __TIME__);
    FIO_PRINTF(stdout, "Copyright (c) 2015-2022 Cadence Design Systems, Inc.\n");
    FIO_PRINTF(stdout, "Lib Name        : %s\n", ver_info[0]);
    FIO_PRINTF(stdout, "Lib Version     : %s\n", ver_info[1]);
    FIO_PRINTF(stdout, "API Version     : %s\n", ver_info[2]);
    FIO_PRINTF(stdout, "****************************************************************\n");

    return 0;
}

#if XA_TFLM_MICROSPEECH
/* ...............Place holder for the doing something on recognized command */
void process_results(void *p_buf, int buf_length)
{
  const char* labels[4] = {"SILENCE", "UNKNOWN", "YES", "NO"};

  int *pOut = (int*)p_buf;
  if(pOut[0])
    printf("Heard \"%s\" with %d score\n", labels[pOut[1]], pOut[2]);

  return;
}
#endif
#if XA_TFLM_PERSON_DETECT
void print_pd_score(void *p_buf, int buf_length)
{
    char *pOut = (char*)p_buf;
    printf("person score: %d no-person score: %d\n", pOut[1], pOut[0]);
    return;
}
#endif
int consume_output(void *p_buf, int buf_length, void *p_output, xaf_comp_type comp_type, char *pcid)
{
    TST_CHK_PTR(p_buf, "consume_output");
    TST_CHK_PTR(p_output, "consume_output");
#if XA_TFLM_MICROSPEECH
    if(!strncmp(pcid, "post-proc/microspeech_inference", 31))
    {
        process_results(p_buf, buf_length);
        if( (comp_type != XAF_ENCODER) || (mcps_p_output == NULL) )
            num_bytes_write += buf_length;
        return 0;
    }
#endif
#if XA_TFLM_PERSON_DETECT
    if(!strncmp(pcid, "post-proc/person_detect_inference", 33))
    {
        print_pd_score(p_buf, buf_length);
        if( (comp_type != XAF_ENCODER) || (mcps_p_output == NULL) )
            num_bytes_write += buf_length;
        return 0;
    }
#endif

    FILE *fp = p_output;
    fio_fwrite(p_buf, 1, buf_length, fp);

    if((comp_type != XAF_ENCODER) && ((mcps_p_output == fp) || (mcps_p_output == NULL)))
        num_bytes_write += buf_length;

    return 0;
}

/* ... align pointer to 8 bytes */
#define XA_ALIGN_8BYTES(ptr)                (((unsigned int)(ptr) + 7) & ~7)
#define COMP_MAX_IO_PORTS                   8

int consume_probe_output(void *p_buf, int buf_length, int cid)
{
    TST_CHK_PTR(p_buf, "consume_probe_output");

    unsigned int  probe_length;
    unsigned int bytes_consumed = 0;
    void *probe_buf = p_buf;
    FILE *fp;

    char probefname[64];
    unsigned int port;
    do
    {
        probe_buf    = (void *) XA_ALIGN_8BYTES(probe_buf);
        port         = *(unsigned int *) probe_buf;
        probe_buf   += sizeof(unsigned int);
        probe_length = *(unsigned int *) probe_buf;
        probe_buf   += sizeof(unsigned int);

        sprintf(probefname,"comp%d_port%d.bin", cid, port);

        if(port >= COMP_MAX_IO_PORTS)
        {
            fprintf(stderr,"consume_probe: invalid port %x\n", port);
            return -1;
        }
        fp = fopen(probefname, "ab");
        if (probe_length)
        {
            fio_fwrite(probe_buf, 1, probe_length, fp);
            probe_buf += probe_length;
        }
        bytes_consumed = probe_buf - p_buf;
        fclose(fp);

    } while(bytes_consumed < buf_length);

    return (!(bytes_consumed == buf_length));
}

int read_input(void *p_buf, int buf_length, int *read_length, void *p_input, xaf_comp_type comp_type)
{
    TST_CHK_PTR(p_buf, "read_input");
    TST_CHK_PTR(read_length, "read_input");
    TST_CHK_PTR(p_input, "read_input");
    FILE *fp = p_input;
    *read_length = fio_fread(p_buf, 1, buf_length, fp);

    if((comp_type == XAF_ENCODER) || (mcps_p_input == fp))
        num_bytes_read += *read_length;

    return 0;
}

#if 0 /* unused function */
static int get_per_comp_size(int size, int num)
{
    int comp_size;

    comp_size = size/num;

    if((comp_size*num) < size)
        comp_size += 1;

    return(comp_size);
}
#endif

#define AUDIO_COMP_BUF_SIZE         (1024 << 10)
#define AUDIO_FRMWK_BUF_SIZE_MAX    (256<<13)

#if (XF_CFG_CORES_NUM > 1)
int worker_task(int argc, char **argv)
{
    void *p_adev = NULL;
    unsigned short board_id = 0;
    mem_obj_t* mem_handle;
    xaf_adev_config_t adev_config;
    xaf_perf_stats_t *pstats = (xaf_perf_stats_t *)perf_stats;

    audio_comp_buf_size = AUDIO_COMP_BUF_SIZE;

    // NOTE: set_wbna() should be called before any other dynamic
    // adjustment of the region attributes for cache.
    set_wbna(&argc, argv);

    /* ...start xos */
    board_id = start_rtos();

    /* ...initialize tracing facility */
    TRACE_INIT("Xtensa Audio Framework - \'Worker DSP\' Sample App");

    mem_handle = mem_init();

#ifdef XAF_PROFILE
    dsp_comps_cycles = enc_cycles = dec_cycles = mix_cycles = pcm_gain_cycles = src_cycles = capturer_cycles = renderer_cycles = aac_dec_cycles = 0;
    aec22_cycles = aec23_cycles = pcm_split_cycles = mimo_mix_cycles = 0;
#endif

    TST_CHK_API(xaf_adev_config_default_init(&adev_config), "xaf_adev_config_default_init");

    adev_config.audio_framework_buffer_size[XAF_MEM_ID_DEV] =  0; /* ...required to get correct shmem offset, but not used by worker DSPs */
    adev_config.audio_component_buffer_size[XAF_MEM_ID_COMP] = audio_comp_buf_size;
    adev_config.paudio_component_buffer[XAF_MEM_ID_COMP] = mem_malloc(audio_comp_buf_size, XAF_MEM_ID_COMP);
    adev_config.audio_shmem_buffer_size = XF_SHMEM_SIZE - AUDIO_FRMWK_BUF_SIZE_MAX*(1 + XAF_MEM_ID_DEV_MAX);
    adev_config.framework_local_buffer_size = FRMWK_APP_IF_BUF_SIZE;
    adev_config.pframework_local_buffer = mem_malloc(FRMWK_APP_IF_BUF_SIZE, XAF_MEM_ID_DEV);
    adev_config.core = XF_CORE_ID;
    adev_config.pshmem_dsp = shared_mem;

    {
        int k;
        int bufsize = AUDIO_COMP_FAST_BUF_SIZE;
        for(k=XAF_MEM_ID_COMP_FAST; k<= XAF_MEM_ID_COMP_MAX;k++)
        {
       	    adev_config.audio_component_buffer_size[k] = bufsize;
            adev_config.paudio_component_buffer[k] = mem_malloc(bufsize, k);
        }
    }

#ifdef XAF_PROFILE
    memset(&pstats[XF_CORE_ID], 0, sizeof(xaf_perf_stats_t));
    adev_config.cb_compute_cycles = cb_total_frmwrk_cycles; /* ...callback function pointer to update thread cycles */
    adev_config.cb_stats = &pstats[XF_CORE_ID]; /* ...pointer to this worker DSP's stats structure, to be updated at the end of execution, in the call-back function. */
#endif

    TST_CHK_API(xaf_dsp_open(&p_adev, &adev_config),  "xaf_dsp_open");
    FIO_PRINTF(stdout,"Audio Worker DSP[%d] Ready [Config:%s]\n", adev_config.core, XCHAL_CORE_ID);

#ifdef XAF_PROFILE
    clk_start();
#endif

    TST_CHK_API(xaf_dsp_close(p_adev), "xaf_dsp_close");
    FIO_PRINTF(stdout,"Audio Worker DSP[%d] closed\n\n", adev_config.core);

#ifdef XAF_PROFILE
    clk_stop();
#endif

    mem_free(adev_config.pframework_local_buffer, XAF_MEM_ID_DEV);
    {
        int k;
        for(k=XAF_MEM_ID_COMP;k<= XAF_MEM_ID_COMP_MAX;k++)
        {
    	    mem_free(adev_config.paudio_component_buffer[k], k);
        }
    }

    mem_exit();

    /* ...deinitialize tracing facility */
    TRACE_DEINIT();

#ifdef XAF_PROFILE
    dsp_comps_cycles = enc_cycles + dec_cycles + mix_cycles + pcm_gain_cycles + src_cycles + capturer_cycles + renderer_cycles + aac_dec_cycles;
    dsp_comps_cycles += aec22_cycles + aec23_cycles + pcm_split_cycles + mimo_mix_cycles;

    pstats[XF_CORE_ID].dsp_comps_cycles = dsp_comps_cycles;

#if XF_LOCAL_IPC_NON_COHERENT
    XF_IPC_FLUSH(&pstats[XF_CORE_ID], (sizeof(xaf_perf_stats_t)));
#endif
#endif //XAF_PROFILE

    return 0;
}

/* ...common function to print worker DSP MCPS */
void print_worker_stats(void)
{
#ifdef XAF_PROFILE
    int i, k;
    volatile xaf_perf_stats_t *pstats = (xaf_perf_stats_t *)perf_stats;
    double xaf_mcps_total = 0.0;
    double dsp_mcps_total = 0.0;
    for (i=0; i<XF_CFG_CORES_NUM; i++)
    {
        if(XF_CORE_ID_MASTER != i)
        {
#if XF_LOCAL_IPC_NON_COHERENT
            XF_IPC_INVALIDATE((void *)&pstats[i], (sizeof(xaf_perf_stats_t)));
#endif
            /* ...wait till worker updates its cycle numbers */
            while (1)
            {
                if(pstats[i].tot_cycles != 0)
                    break;
#if XF_LOCAL_IPC_NON_COHERENT
                XF_IPC_INVALIDATE((void *)&pstats[i], (sizeof(xaf_perf_stats_t)));
#endif
            }
            tot_cycles = pstats[i].tot_cycles;
            frmwk_cycles = pstats[i].frmwk_cycles;
            dsp_comps_cycles = pstats[i].dsp_comps_cycles;
            fprintf(stderr,"Worker DSP[%d] stats\n", i);

            for(k = XAF_MEM_ID_COMP ; k < XAF_MEM_ID_COMP_MAX ; k++)
            {
                if(pstats[i].dsp_comp_buf_size_peak[k])
                {
                    FIO_PRINTF(stderr,"DSP[%d] Local Memory type[%d] used by DSP Components, in bytes    : %8d\n", i, k, pstats[i].dsp_comp_buf_size_peak[k]);
                }
            }
            FIO_PRINTF(stderr,"DSP[%d] Shared Memory used by DSPs, in bytes                     : %8d\n", i, pstats[i].dsp_shmem_buf_size_peak);
            FIO_PRINTF(stderr,"DSP[%d] Local Memory used by Framework, in bytes                 : %8d\n\n", i, pstats[i].dsp_framework_local_buf_size_peak);

            if(strm_duration)
            {
                double worker_dsp_mcps = ((double)dsp_comps_cycles/((strm_duration)*1000000.0));
                double mcps;
                frmwk_cycles =  frmwk_cycles - (dsp_comps_cycles);
                FIO_PRINTF(stdout,"DSP[%d] Component MCPS                        :  %f\n", i, worker_dsp_mcps);
                mcps = ((double)frmwk_cycles/(strm_duration*1000000.0));
                FIO_PRINTF(stdout,"DSP[%d] Framework MCPS                        :  %f\n\n", i, mcps);

                xaf_mcps_total += mcps;
                dsp_mcps_total += worker_dsp_mcps;
            }
        }
    }
    {
        xaf_mcps_total += xaf_mcps_master;
        dsp_mcps_total += dsp_mcps;

        FIO_PRINTF(stdout,"Total XAF MCPS                               :  %f\n", xaf_mcps_total);
        FIO_PRINTF(stdout,"Total DSP MCPS                               :  %f\n", dsp_mcps_total);
        FIO_PRINTF(stdout,"Total MCPS (DSP + XAF)                       :  %f\n\n", xaf_mcps_total+dsp_mcps_total);
    }
#endif //XAF_PROFILE
}
#endif //XF_CFG_CORES_NUM

#if defined(HAVE_XOS)
int init_rtos(int argc, char **argv, int (*main_task)(int argc, char **argv))
{
    int err;
    err = main_task(argc, argv);
#if ((XF_CFG_CORES_NUM > 1) && (XF_CORE_ID == XF_CORE_ID_MASTER))
    if(!err) print_worker_stats();
#endif
    return err;
}

unsigned short start_rtos(void)
{
	volatile unsigned short  board_id = 0;

    xos_set_clock_freq(CLK_FREQ);

#ifdef __TOOLS_RF2__
    xos_start("main", XAF_MAIN_THREAD_PRIORITY, 0);
#else   // #ifdef __TOOLS_RF2__
    xos_start_main("main", XAF_MAIN_THREAD_PRIORITY, 0);
#endif  // #ifdef __TOOLS_RF2__

#if XCHAL_NUM_TIMERS > 0
    /* TENA-2586/2587. For XOS, passing -1 ias argument choses timer with appropriate interrupt_level (<= EXCM).
     * Or passing, timer number exclusively meeting the above criteria also works */
    xos_start_system_timer(-1, TICK_CYCLES);
    FIO_PRINTF(stdout, "DSP[%d] XOS is using Timer:%d\n", XT_RSR_PRID(), xos_get_system_timer_num());

#endif

    return board_id;
}
#endif
#ifdef HAVE_FREERTOS

#define MAIN_STACK_SIZE 65536

struct args {
    int argc;
    char **argv;
    int (*main_task)(int argc, char **argv);
};

static void *main_task_wrapper(void *arg)
{
    struct args *args = arg;
    int err;
    err = args->main_task(args->argc, args->argv);
#if ((XF_CFG_CORES_NUM > 1) && (XF_CORE_ID == XF_CORE_ID_MASTER))
    if(!err) print_worker_stats();
#endif
    fprintf(stderr,"%s c[%d] err:%x\n",__func__, XF_CORE_ID, err);
    exit(err);
}

int init_rtos(int argc, char **argv, int (*main_task)(int argc, char **argv))
{
    struct args args = {
        .argc = argc,
        .argv = argv,
        .main_task = main_task,
    };
    xf_thread_t thread;

    __xf_thread_create(&thread, main_task_wrapper, &args, "main task",
                       NULL, MAIN_STACK_SIZE , XAF_MAIN_THREAD_PRIORITY);
    vTaskStartScheduler();
    return 1;
}
unsigned short start_rtos(void)
{
    return 0;
}
void vApplicationTickHook( void )
{
}
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName )
{
}
#endif

int main(int argc, char **argv)
{
    int ret;

#if (XF_CORE_ID == XF_CORE_ID_MASTER)
    ret = init_rtos(argc, argv, main_task);
#elif (XF_CFG_CORES_NUM > 1)
    ret = init_rtos(argc, argv, worker_task);
#endif
    fprintf(stderr,"%s c[%d] err:%x\n", __func__, XF_CORE_ID, ret);
    return ret;
}

static int _comp_process_entry(void *arg)
{
    void *p_adev, *p_comp;
    void *p_input, *p_output;
    xaf_comp_status comp_status;
    int comp_info[4];
    int input_over, read_length;
    void * (*arg_arr)[10];
    xaf_comp_type comp_type;
    int error;
#ifdef XAF_PROFILE
    clk_t fread_start, fread_stop, fwrite_start, fwrite_stop;
#endif
    TST_CHK_PTR(arg, "comp_process_entry");

    arg_arr = arg;
    p_adev = (*arg_arr)[0];
    p_comp = (*arg_arr)[1];
    p_input = (*arg_arr)[2];
    p_output = (*arg_arr)[3];
    comp_type = *(xaf_comp_type *)(*arg_arr)[4];
    char *pcid = (char *)(*arg_arr)[5];
    int cid = *(int *)(*arg_arr)[6];
    char get_status_cid[64];
    sprintf(get_status_cid, "xaf_comp_get_status for %d (%s)", cid, pcid);
#ifdef REGR_TIMEOUT_CHECK
    int timeout_cnt=0;
#endif

    /* workaround to run some test usecases which are not passing cid properly */
    if ( (cid >= MAX_NUM_COMP_IN_GRAPH) || (cid < 0) )
    {
        cid = 0;
    }

    FIO_PRINTF(stderr,"thread_entry cid:%d pcid:%s\n", cid, pcid);

    g_force_input_over[cid] = 0;

    TST_CHK_PTR(p_adev, "comp_process_entry");
    TST_CHK_PTR(p_comp, "comp_process_entry");

    input_over = 0;

    TST_CHK_API(xaf_comp_process(NULL, p_comp, NULL, 0, XAF_EXEC_FLAG), "xaf_comp_process");

    while (1)
    {
        error = xaf_comp_get_status(NULL, p_comp, &comp_status, &comp_info[0]);
        if ((error == XAF_TIMEOUT_ERR) && (g_continue_wait))
        {
#ifdef REGR_TIMEOUT_CHECK
            timeout_cnt++;
            if(timeout_cnt>10)
                FIO_PRINTF(stderr, "----- %d:%s Hanged returning API error -----\n",cid, pcid);
            else
#endif
            {
                FIO_PRINTF(stderr, "#%d:%s\n",cid, pcid);
                continue;    // If resume is expected, then wait. Else timeout.
            }
        }

        TST_CHK_API(error, (char *)&get_status_cid);

        if (comp_status == XAF_EXEC_DONE) break;

        if (comp_status == XAF_NEED_INPUT && !input_over)
        {
            read_length = 0;
            if (g_force_input_over[cid] != 1)
            {

                void *p_buf = (void *)comp_info[0];
                int size = comp_info[1];
                static int frame_count = 0;

#ifdef XAF_PROFILE
                fread_start = clk_read_start(CLK_SELN_THREAD);
#endif

                TST_CHK_API(read_input(p_buf, size, &read_length, p_input, comp_type), "read_input");
                FIO_PRINTF(stdout, " frame_count:%d\n", ++frame_count);

#ifdef XAF_PROFILE
                fread_stop = clk_read_stop(CLK_SELN_THREAD);
                fread_cycles += clk_diff(fread_stop, fread_start);
#endif

                /* Set g_force_input_over[cid] to 2 for natural input over */
                if ( read_length == 0 )
                {
                    g_force_input_over[cid] = 2;
                }

            }
            if (read_length)
                TST_CHK_API(xaf_comp_process(NULL, p_comp, (void *)comp_info[0], read_length, XAF_INPUT_READY_FLAG), "xaf_comp_process");
            else
            {
                TST_CHK_API(xaf_comp_process(NULL, p_comp, NULL, 0, XAF_INPUT_OVER_FLAG), "xaf_comp_process");
                input_over = 1;
            }
        }

        if (comp_status == XAF_OUTPUT_READY)
        {
            void *p_buf = (void *)comp_info[0];
            int size = comp_info[1];

#ifdef XAF_PROFILE
            fwrite_start = clk_read_start(CLK_SELN_THREAD);
#endif

            TST_CHK_API(consume_output(p_buf, size, p_output, comp_type, pcid), "consume_output");

#ifdef XAF_PROFILE
            fwrite_stop = clk_read_stop(CLK_SELN_THREAD);
            fwrite_cycles += clk_diff(fwrite_stop, fwrite_start);
#endif

            TST_CHK_API(xaf_comp_process(NULL, p_comp, (void *)comp_info[0], comp_info[1], XAF_NEED_OUTPUT_FLAG), "xaf_comp_process");
        }

        if (comp_status == XAF_PROBE_READY)
        {
            void *p_buf = (void *)comp_info[0];
            int size = comp_info[1];

            TST_CHK_API(consume_probe_output(p_buf, size, cid), "consume_probe_output");

            TST_CHK_API(xaf_comp_process(NULL, p_comp, (void *)comp_info[0], comp_info[1], XAF_NEED_PROBE_FLAG), "xaf_comp_process");
        }

        if (comp_status == XAF_PROBE_DONE)
        {
            /* ...tbd - improve break condition */
            if ((p_input == NULL) && (p_output == NULL)) break;
        }
    }

    FIO_PRINTF(stderr,"thread_exit cid:%d pcid:%s\n", cid, pcid);

    return 0;
}

void *comp_process_entry(void *arg)
{
    return (void *)_comp_process_entry(arg);
}

static int _comp_disconnect_entry(void *arg)
{
    TST_CHK_PTR(arg, "comp_disconnect_entry");

    int *arg_arr = (int *) arg;
    int src_component_id = arg_arr[0];
    int src_port = arg_arr[1];
    int dst_component_id = arg_arr[2];
    int dst_port = arg_arr[3];
    int comp_create_delete_flag = arg_arr[4];

    fprintf(stderr, "DISCONNECT3 cid:%d port:%d start\n", src_component_id, src_port);
    if (gpcomp_disconnect(src_component_id, src_port, dst_component_id, dst_port, comp_create_delete_flag))
    {
        fprintf(stderr, "ERROR: DISCONNECT cid:%d port:%d\n", src_component_id, src_port);
    }

    return 0;
}

void *comp_disconnect_entry(void *arg)
{
    return (void *)_comp_disconnect_entry(arg);
}

#ifndef XA_DISABLE_EVENT
static int _event_handler_entry(void *arg)
{
    FIO_PRINTF(stderr,"thread_entry : event_handler\n");
    TST_CHK_PTR(arg, "event_handler_entry");
    if (!g_app_handler_fn)
        return 0;

    while(1)
    {
        __xf_thread_sleep_msec(50);

        fprintf(stderr, "Event Handler Thread: Checking for events\n");

        xa_app_process_events();

        if (g_execution_abort_flag || g_event_handler_exit)
            break;
    }
    FIO_PRINTF(stderr,"thread_exit : event_handler\n");

    return 0;
}

void *event_handler_entry(void *arg)
{
    return (void *)_event_handler_entry(arg);
}
#endif

double compute_comp_mcps(unsigned int num_bytes, long long comp_cycles, xaf_format_t comp_format, double *strm_duration)
{
    double mcps;
    unsigned int num_samples;
    int pcm_width_in_bytes;

    *strm_duration = 0.0;

    switch(comp_format.pcm_width)
    {
        case 8:
        case 16:
        case 24:
        case 32:
            break;

        default:
            FIO_PRINTF(stdout,"Insufficient PCM-Width data to compute MCPS %d ...\n", comp_format.pcm_width);
            return 0;
    }

    switch(comp_format.sample_rate)
    {
        case 4000:
        case 8000:
        case 11025:
        case 12000:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
        case 64000:
        case 88200:
        case 96000:
        case 128000:
        case 176400:
        case 192000:
            break;

        default:
            FIO_PRINTF(stdout,"Insufficient sample rate data to compute MCPS %d ...\n", comp_format.sample_rate);
            return 0;
    }

    if(comp_format.channels > 32)
    {
        FIO_PRINTF(stdout,"Insufficient channels data to compute MCPS %d ...\n", comp_format.channels);
        return 0;
    }

    if( comp_cycles < 0 )
    {
        FIO_PRINTF(stdout,"Negative cycles data to compute MCPS %lld...\n", comp_cycles);
        return 0;
    }

    pcm_width_in_bytes = (comp_format.pcm_width)/8;
    num_samples = num_bytes/pcm_width_in_bytes;
    *strm_duration = (double)num_samples/((comp_format.sample_rate)*(comp_format.channels));

    mcps = ((double)comp_cycles/((*strm_duration)*1000000.0));

    FIO_PRINTF(stdout, "PCM Width                                    :  %8d\n", comp_format.pcm_width);
    FIO_PRINTF(stdout, "Sample Rate                                  :  %8d\n", comp_format.sample_rate);
    FIO_PRINTF(stdout, "No of channels                               :  %8d\n", comp_format.channels);
    FIO_PRINTF(stdout, "Stream duration (seconds)                    :  %8f\n\n", *strm_duration);

#if ((XF_CFG_CORES_NUM > 1) && (XF_CORE_ID == XF_CORE_ID_MASTER))
    /* ...update stream format to calculate worker core stats */
    memcpy(&comp_format_mcps, &comp_format, sizeof(comp_format_mcps));
#endif

    return mcps;
}


int print_mem_mcps_info(mem_obj_t* mem_handle, int num_comp)
{
    int tot_dev_mem_size, tot_comp_mem_size, mem_for_comp, tot_size;
    double read_write_mcps;

    /* ...printing memory info*/

    tot_dev_mem_size = mem_get_alloc_size(mem_handle, XAF_MEM_ID_DEV);
    tot_comp_mem_size = mem_get_alloc_size(mem_handle, XAF_MEM_ID_COMP);
    tot_size = tot_dev_mem_size + tot_comp_mem_size;
    mem_for_comp = (audio_frmwk_buf_size + audio_comp_buf_size - XAF_SHMEM_STRUCT_SIZE);
    /* XAF_SHMEM_STRUCT_SIZE is used internally by the framework. Computed as sizeof(xf_shmem_data_t)-XF_CFG_REMOTE_IPC_POOL_SIZE*/

    /* ...printing mcps info*/

#ifdef XAF_PROFILE
    if(strm_duration)
    {
        frmwk_cycles =  frmwk_cycles - (dsp_comps_cycles) - (fread_cycles + fwrite_cycles);
        read_write_mcps = ((double)(fread_cycles + fwrite_cycles)/(strm_duration*1000000.0));

        FIO_PRINTF(stdout,"DSP[%d] Component MCPS                        :  %f\n", XT_RSR_PRID(), dsp_mcps);

        FIO_PRINTF(stdout,"DSP[%d] File Read/Write MCPS                  :  %f\n", XT_RSR_PRID(), read_write_mcps);

        xaf_mcps_master = ((double)frmwk_cycles/(strm_duration*1000000.0));
        FIO_PRINTF(stdout,"DSP[%d] Framework MCPS                        :  %f\n\n",  XT_RSR_PRID(), xaf_mcps_master);

#if (XF_CFG_CORES_NUM == 1)
        FIO_PRINTF(stdout,"XAF MCPS                                     :  %f\n", xaf_mcps_master);
        FIO_PRINTF(stdout,"Total MCPS (DSP + XAF)                       :  %f\n\n",(xaf_mcps_master + dsp_mcps));
#endif
    }
#endif

    return 0;
}

void sort_runtime_action_commands(cmd_array_t *command_array)  //Implementation of Insertion Sort Algorithm(incremental sort)
{
    int size = command_array->size;
    int i, j, key;
    cmd_info_t **cmdlist = command_array->commands;

    cmd_info_t *key_command = NULL;

    for (i = 1; i < size; i++)
    {
        key = cmdlist[i]->time;
        key_command = cmdlist[i];
        j = i-1;

        while (j >= 0 && cmdlist[j]->time > key)
        {
            cmdlist[j+1] = cmdlist[j];
            j = j-1;
        }
        cmdlist[j+1] = key_command;
    }
    command_array->commands = cmdlist;
}

int parse_runtime_params(void **runtime_params, int argc, char **argv, int num_comp )
{
    cmd_array_t *cmd_array;

    cmd_array = (cmd_array_t *)malloc(sizeof(cmd_array_t));
    cmd_array->commands = (cmd_info_t **)malloc(RUNTIME_MAX_COMMANDS * 2 * sizeof(cmd_info_t *));
    cmd_array->size = 0;

    char *pr_string;
    int i, x;
    int cmd_params[6];
    cmd_info_t *cmd1, *cmd2;
    int probe_flag, connect_flag;

    memset(cmd_array->commands, 0, RUNTIME_MAX_COMMANDS * 2 * sizeof(cmd_info_t *));

    for (i = 0; i < (argc); i++)
    {
        cmd_params[0] = cmd_params[1] = cmd_params[2] = cmd_params[3] = cmd_params[4] = cmd_params[5] = -1;
        probe_flag = 0;

        if ((NULL != strstr(argv[i + 1], "-pr:")) || (NULL != strstr(argv[i + 1], "-probe:")))
        {
            if (NULL != strstr(argv[i + 1], "-probe:")) probe_flag = 1;

            if (probe_flag)
                pr_string = (char *)&(argv[i + 1][7]);
            else
                pr_string = (char *)&(argv[i + 1][4]);

            x = 0;
#if (XF_CFG_CORES_NUM > 1)
            char pr_char, pr_char_next;
            int cid;
            cid = atoi(pr_string);
            cmd_params[x++] = cid;

            while (1) //parse until one command string is over
            {
                i++;
                pr_string = (char *)&(argv[i + 1][0]);

                if (pr_string == NULL)
                {
                    i--;
                    break;
                }
                else
                {
                    pr_char = (char)pr_string[0];
                    pr_char_next = (char)pr_string[1];

                    if ((NULL != strstr(pr_string, "-")) && (pr_char_next != '\0') && !(pr_char_next >= '0' && pr_char_next <= '9'))
                    {
                        i--;
                        break;
                    }

                    cmd_params[x++] = atoi(pr_string);

                    if (x == 4) break;
                }
            }
#else
            char *ptr = NULL;
            char *token;
            for (token = strtok_r(pr_string, ",", &ptr); token != NULL; token = strtok_r(NULL, ",", &ptr))
            {
                cmd_params[x] = atoi(token);
                x++;

                if (x == 4) break;
            }
#endif

            if (probe_flag && (x < 3))
            {
                fprintf(stderr, "Runtime-Params-Parse: Invalid probe command. Minimum 3 values required.\n");
                goto error_exit;
            }
            else if (!probe_flag && (x < 4))
            {
                fprintf(stderr, "Runtime-Params-Parse: Invalid pause/resume command. Minimum 4 values required.\n");
                goto error_exit;
            }
            if (cmd_params[0] >= num_comp)
            {
                fprintf(stderr, "Runtime-Params-Parse: Invalid component ID. Allowed range: 0-%d\n", num_comp - 1);
                goto error_exit;
            }

            cmd1 = (cmd_info_t *)malloc(sizeof(cmd_info_t));
            memset(cmd1, 0, sizeof(cmd_info_t));
            cmd2 = (cmd_info_t *)malloc(sizeof(cmd_info_t));
            memset(cmd2, 0, sizeof(cmd_info_t));

            cmd1->component_id = cmd_params[0];
            cmd2->component_id = cmd_params[0];

            if (probe_flag)
            {
                cmd1->action = PROBE_START;
                cmd2->action = PROBE_STOP;
                cmd1->time = cmd_params[1];
                cmd2->time = cmd_params[2];
            }
            else
            {
                cmd1->action = PAUSE;
                cmd2->action = RESUME;
                cmd1->port = cmd_params[1];
                cmd2->port = cmd_params[1];
                cmd1->time = cmd_params[2];
                cmd2->time = cmd_params[3];
                if (cmd2->time <= cmd1->time)
                {
                    fprintf(stderr, "Runtime-Params-Parse: Resume time must be > pause time.\n");
                    goto error_exit;
                }
                /* g_continue_wait = 1; */ /* ...commented to help regression flow */
            }

            cmd_array->commands[cmd_array->size] = cmd1;
            cmd_array->commands[cmd_array->size + 1] = cmd2;
            cmd_array->size = cmd_array->size + 2;

            if (cmd_array->size >= (RUNTIME_MAX_COMMANDS * 2))
            {
                fprintf(stderr, "Runtime-Params-Parse: Max number of runtime commands exceeded.\n");
                goto error_exit;
            }
        }
        if ((NULL != strstr(argv[i + 1], "-D:")) || (NULL != strstr(argv[i + 1], "-C:")))
        {
            connect_flag = 0;
            if (NULL != strstr(argv[i + 1], "-C:"))
            {
                connect_flag = 1;
            }

            pr_string = (char *)&(argv[i + 1][3]);

            x = 0;
#if (XF_CFG_CORES_NUM > 1)
            int cid;
            cid = atoi(pr_string);
            cmd_params[x++] = cid;

            while (1) //parse until one command string is over
            {
                i++;
                pr_string = (char *)&(argv[i + 1][0]);

                if ((pr_string == NULL) || (NULL != strstr(pr_string, "-")))
                {
                    i--;
                    break;
                }
                else
                {
                    cmd_params[x++] = atoi(pr_string);
                    if (x == 6) break;
                }
            }
#else
            char *ptr = NULL;
            char *token;
            for (token = strtok_r(pr_string, ",", &ptr); token != NULL; token = strtok_r(NULL, ",", &ptr))
            {
                cmd_params[x] = atoi(token);
                x++;

                if (x == 6) break;
            }
#endif

            if (x < 6)
            {
                fprintf(stderr, "Runtime-Params-Parse: Invalid disconnect/connect command.\n");
                goto error_exit;
            }

            cmd1 = (cmd_info_t *)malloc(sizeof(cmd_info_t));
            memset(cmd1, 0, sizeof(cmd_info_t));

            cmd1->component_id = cmd_params[0];
            cmd1->port = cmd_params[1];
            cmd1->component_dest_id = cmd_params[2];
            cmd1->port_dest = cmd_params[3];
            cmd1->time = cmd_params[4];
            cmd1->comp_create_delete_flag = cmd_params[5];
            if (connect_flag)
            {
                cmd1->action = CONNECT;
            }
            else
            {
                cmd1->action = DISCONNECT;
            }
            /* g_continue_wait = 1; */ /* ...commented to help regression flow */

            cmd_array->commands[cmd_array->size] = cmd1;
            cmd_array->size = cmd_array->size + 1;

            if (cmd_array->size >= (RUNTIME_MAX_COMMANDS * 2))
            {
                fprintf(stderr, "Runtime-Params-Parse: Max number of runtime commands exceeded.\n");
                goto error_exit;
            }
        }
    }
    *runtime_params = (void *)cmd_array;

    return 0;

error_exit:
    /* ...free the resource that was allocated */
    for (i = 0; i < cmd_array->size; i++) {
        if (cmd_array->commands[i])
            free(cmd_array->commands[i]);
    }
    free(cmd_array->commands);
    free(cmd_array);
    *runtime_params = NULL;
    return -1;
}

void xa_app_initialize_event_list(int max_events)
{
    g_event_list = (event_list_t *)malloc(sizeof(event_list_t));
    g_event_list->events = (event_info_t **)malloc(max_events*sizeof(event_info_t *));
    g_event_list->num_events = 0;
    g_event_list->curr_idx = 0;
}

/* ... just receive the event, copy event data and return immediately */
int xa_app_receive_events_cb(void *comp, UWORD32 event_id, void *buf, UWORD32 buf_size, UWORD32 comp_error_flag)
{
    event_info_t *e;

    if(g_event_list->num_events >= MAX_EVENTS)
    {
        /* ...drop any event if the application event-list index is exceeded */
        fprintf(stderr, "Drop the received new event. Event ID: %08x\n", event_id);
        return 0;
    }

    e = (event_info_t *) malloc(sizeof(event_info_t));
    e->event_buf = malloc(buf_size);

    e->comp_addr = (UWORD32)comp;
    e->event_id = event_id;
    e->comp_error_flag = comp_error_flag;
    e->buf_size = buf_size;
    memcpy(e->event_buf, buf, buf_size);

    g_event_list->events[g_event_list->num_events++] = e;
    fprintf(stderr, "Received new event. Event ID: %08x\n", event_id);

    return 0;
}

void xa_app_process_events()
{
    if (g_event_list == NULL)
        return;

    int i;
    for (i=g_event_list->curr_idx; i<g_event_list->num_events; i++)
    {
        /* ...do event processing if a handler exists */
        if (g_app_handler_fn)
            g_app_handler_fn(g_event_list->events[i]);

        g_event_list->curr_idx++;
    }

    if(g_event_list->num_events >= MAX_EVENTS)
    {
        /* ...freeup buffers and reset the application event-list indexes */
        for (i = 0; i < g_event_list->num_events; i++)
        {
            free(g_event_list->events[i]->event_buf);
            free(g_event_list->events[i]);
        }
        g_event_list->num_events = 0;
        g_event_list->curr_idx = 0;
    }

}

void xa_app_free_event_list()
{
    int k;

    for (k = 0; k < g_event_list->num_events; k++)
    {
        free(g_event_list->events[k]->event_buf);
        free(g_event_list->events[k]);
    }
    free(g_event_list->events);
    free(g_event_list);
}

int all_threads_exited(void **comp_threads, int num_threads)
{
    int thread_exit_count = 0;
    int disconnect_thread_exited = 0;
    int thread_state, k;

    for (k = 0; k < num_threads; k++)
    {
        thread_state = __xf_thread_get_state(comp_threads[k]);
        if ((thread_state == XF_THREAD_STATE_EXITED) || (thread_state == XF_THREAD_STATE_INVALID))
            thread_exit_count++;
    }

    thread_state = __xf_thread_get_state(&g_disconnect_thread);
    if ((thread_state == XF_THREAD_STATE_EXITED) || (thread_state == XF_THREAD_STATE_INVALID))
        disconnect_thread_exited = 1;

    return ((thread_exit_count == num_threads) && (disconnect_thread_exited));
}

int get_disconnect_thread_status()
{
    int active = 0;

    /* ...if the disconnect thread has exited, join and delete */
    if ( (__xf_thread_get_state(&g_disconnect_thread) != XF_THREAD_STATE_INVALID) && (__xf_thread_get_state(&g_disconnect_thread) == XF_THREAD_STATE_EXITED) )
    {
        __xf_thread_join(&g_disconnect_thread, NULL);
        __xf_thread_destroy(&g_disconnect_thread);
        active = 0;
    }
    /* ... check if disconnect thread is active */
    else if ( (__xf_thread_get_state(&g_disconnect_thread) != XF_THREAD_STATE_INVALID) && (__xf_thread_get_state(&g_disconnect_thread) != XF_THREAD_STATE_EXITED) )
    {
        active = 1;
    }

    return active;
}

cmd_info_t* get_next_runtime_command(cmd_array_t *cmd_array_main, int idx_main, cmd_array_t *cmd_array_cd, int idx_cd, int elapsed_time)
{
    /* ...if one of the arrays are empty, then just get next command from the non-empty one */
    if ((cmd_array_main->size - idx_main) == 0)
        return cmd_array_cd->commands[idx_cd];
    if ((cmd_array_cd->size - idx_cd) == 0)
        return cmd_array_main->commands[idx_main];

    /* ...if the next command from any of the arrays have -ve time, get that so the index can be updated(-ve time commands are ignored) */
    if ((cmd_array_main->commands[idx_main]->time < 0) && (cmd_array_cd->commands[idx_cd]->time < 0))
        return NULL;
    else if (cmd_array_main->commands[idx_main]->time < 0)
        return cmd_array_main->commands[idx_main];
    else if (cmd_array_cd->commands[idx_cd]->time < 0)
        return cmd_array_cd->commands[idx_cd];

    /* ...get the next command from both arrays based on time, if both are having same time - prefer main array */
    return ((cmd_array_main->commands[idx_main]->time - elapsed_time) <= (cmd_array_cd->commands[idx_cd]->time - elapsed_time)) ? cmd_array_main->commands[idx_main] : cmd_array_cd->commands[idx_cd];

}

int execute_runtime_actions(void *command_array,
                                void *p_adev,
                                void **comp_ptr,
                                int *comp_nbufs,
                                void **comp_threads,
                                int num_threads,
                                void *comp_thread_args[],
                                int num_thread_args,
                                unsigned char *comp_stack)
{
    int k;
    int idx_main = 0, idx_cd = 0;
    int sleep_time =0, elapsed_time = 0;
    int active_disconnect_thread = 0;
    unsigned char disc_thread_stack[STACK_SIZE];
    cmd_array_t *cmd_array_main, *cmd_array_cd;
    cmd_info_t *cmd, *cmd_cd;

    /* ...required for freertos */
    __xf_thread_init(&g_disconnect_thread);

    cmd_array_main = (cmd_array_t *)command_array;
    sort_runtime_action_commands(cmd_array_main);

    /* ...a separate command array to execute connect/disconnect commands */
    cmd_array_cd           = (cmd_array_t *)malloc(sizeof(cmd_array_t));
    cmd_array_cd->commands = (cmd_info_t **)malloc(RUNTIME_MAX_COMMANDS* sizeof(cmd_info_t *));
    cmd_array_cd->size     = 0;

    /* ...copy all connect/disconnect commands from the main array */
    for (k=0; k<cmd_array_main->size; k++)
    {
        cmd    = cmd_array_main->commands[k];

        if ((cmd->action == CONNECT) || (cmd->action == DISCONNECT))
        {
            cmd_cd = (cmd_info_t *)malloc(sizeof(cmd_info_t));
            memset(cmd_cd, 0, sizeof(cmd_info_t));
            memcpy(cmd_cd, cmd, sizeof(cmd_info_t));
            cmd_array_cd->commands[cmd_array_cd->size++] = cmd_cd;
            cmd->action = IGNORE; // This disables execution from main array
        }
    }

    while(1)
    {
        /* ... don't issue runtime commands if force close has occured */
        if (g_execution_abort_flag)
        {
            fprintf(stdout, "\nRuntime Action. Execution has been aborted. Further commands(if any) will be skipped\n\n");
            break;
        }

        /*  ...check and quit if all threads are exited */
        if (all_threads_exited(comp_threads, num_threads))
        {
            fprintf(stdout, "\nRuntime Action: All threads exited. Further commands(if any) will be skipped\n\n");
            break;
        }

        if (((cmd_array_main->size - idx_main) == 0) && ((cmd_array_cd->size - idx_cd) == 0))
        {
            /* ...no commands to process, exit loop */
            //break;
            __xf_thread_sleep_msec(100);
            continue;
        }

        /* ...pick the best command to execute from the two command arrays */
        cmd = get_next_runtime_command(cmd_array_main, idx_main, cmd_array_cd, idx_cd, elapsed_time);

        if (cmd == NULL)
        {
            idx_main++;
            idx_cd++;
            continue;
        }

        if ((cmd->action == CONNECT) || (cmd->action == DISCONNECT))
            idx_cd++;
        else
            idx_main++;

        if (cmd->action == IGNORE) continue;
        if (cmd->time < 0) continue;    // Commands with negative time value are ignored

        sleep_time = cmd->time - elapsed_time;
        if (sleep_time < 0) sleep_time = 0;
        __xf_thread_sleep_msec(sleep_time);
        elapsed_time += sleep_time;

        /*  ...check and quit if all threads are exited, again - since it might have been a very long sleep! */
        if (all_threads_exited(comp_threads, num_threads))
        {
            fprintf(stdout, "\nRuntime Action: All threads exited. Further commands will be skipped\n\n");
            break;
        }

        /* ... check if disconnect thread is active */
        active_disconnect_thread = get_disconnect_thread_status();

        switch (cmd->action)
        {
        case PAUSE:
            TST_CHK_API(xaf_pause(comp_ptr[cmd->component_id], cmd->port), "xaf_pause");
            fprintf(stdout, "Runtime Action: Component %d, Port %d : PAUSE command issued\n", cmd->component_id, cmd->port);
            break;

        case RESUME:
            TST_CHK_API(xaf_resume(comp_ptr[cmd->component_id], cmd->port), "xaf_resume");
            fprintf(stdout, "Runtime Action: Component %d, Port %d : RESUME command issued\n", cmd->component_id, cmd->port);
            break;

        case PROBE_START:
            TST_CHK_API(xaf_probe_start(comp_ptr[cmd->component_id]), "xaf_probe_start");
            fprintf(stdout, "Runtime Action: Component %d : PROBE-START command issued\n", cmd->component_id);
            if (comp_nbufs[cmd->component_id] == 0)
            {
                __xf_thread_create(comp_threads[cmd->component_id], comp_process_entry, &comp_thread_args[num_thread_args * cmd->component_id], "Probe Thread", &comp_stack[STACK_SIZE * cmd->component_id], STACK_SIZE, XAF_APP_THREADS_PRIORITY);
            }
            break;

        case PROBE_STOP:
            TST_CHK_API(xaf_probe_stop(comp_ptr[cmd->component_id]), "xaf_probe_stop");
            fprintf(stdout, "Runtime Action: Component %d : PROBE-STOP command issued\n", cmd->component_id);
            if (comp_nbufs[cmd->component_id] == 0)
            {
                __xf_thread_join(comp_threads[cmd->component_id], NULL);
                fprintf(stdout, "Probe thread for component %d joined\n", cmd->component_id);
                __xf_thread_destroy(comp_threads[cmd->component_id]);
            }
            break;

        case DISCONNECT:
            if (active_disconnect_thread)
            {
                idx_cd--;                // Attempt the same command again, once the on-going disconnect with delete is over.
                cmd->time += 20;         // Sleep in steps until disconnect thread exits.
                continue;
            }

            if ( cmd->comp_create_delete_flag == 0 )
            {
                cmd_info_t *cmd_next;
                int re_connect_flag = 0;

                for (k = idx_cd; k < cmd_array_cd->size; k++)
                {
                    cmd_next = cmd_array_cd->commands[k];

                    if ( ( cmd_next->action == CONNECT ) && ( cmd->component_id == cmd_next->component_id) && ( cmd_next->comp_create_delete_flag == 0) )
                    {
                        re_connect_flag = 1;
                    }
                }
                if ( re_connect_flag == 0 )
                {
                    fprintf(stderr, "INFO: DISCONNECT is initiated with component and thread deletion since reconnect is not issued for this component \n");
                    cmd->comp_create_delete_flag = 1;
                }
            }

            if (cmd->comp_create_delete_flag == 0)
            {
                fprintf(stderr, "DISCONNECT2 cid:%d port:%d start\n", cmd->component_id, cmd->port);
                if (gpcomp_disconnect(cmd->component_id, cmd->port, cmd->component_dest_id, cmd->port_dest, cmd->comp_create_delete_flag))
                {
                    fprintf(stderr, "ERROR: DISCONNECT cid:%d port:%d\n", cmd->component_id, cmd->port);
                }
            }
            else
            {
                /* ...create a thread whenever disconnect and delete is required */
                g_disc_thread_args[0] = cmd->component_id;
                g_disc_thread_args[1] = cmd->port;
                g_disc_thread_args[2] = cmd->component_dest_id;
                g_disc_thread_args[3] = cmd->port_dest;
                g_disc_thread_args[4] = cmd->comp_create_delete_flag;

                __xf_thread_create(&g_disconnect_thread, comp_disconnect_entry, g_disc_thread_args, "disconnect thread", disc_thread_stack, STACK_SIZE, XAF_APP_THREADS_PRIORITY);
                active_disconnect_thread = 1;
            }

            break;

        case CONNECT:
            if (active_disconnect_thread)
            {
                idx_cd--;                // Attempt the same command again, once the on-going disconnect with delete is over.
                cmd->time += 20;         // Sleep in steps until disconnect thread exits.
                continue;
            }

            fprintf(stderr, "CONNECT cid:%d port:%d start\n", cmd->component_id, cmd->port);
            if (gpcomp_connect(cmd->component_id, cmd->port, cmd->component_dest_id, cmd->port_dest, cmd->comp_create_delete_flag))
            {
                fprintf(stderr, "ERROR: CONNECT cid:%d port:%d\n", cmd->component_id, cmd->port);
            }
            break;

        default:
            fprintf(stdout, "Runtime Action: Unknown command. Ignored.\n");
        }
    }

    /* ...join and destroy the disconnect thread (this may be redundant) */
    __xf_thread_join(&g_disconnect_thread, NULL);
    __xf_thread_destroy(&g_disconnect_thread);

    for (k = 0; k < cmd_array_main->size; k++)
    {
        free(cmd_array_main->commands[k]);
    }
    free(cmd_array_main->commands);
    free(cmd_array_main);

    for (k = 0; k < cmd_array_cd->size; k++)
    {
        free(cmd_array_cd->commands[k]);
    }
    free(cmd_array_cd->commands);
    free(cmd_array_cd);

    return 0;
}
