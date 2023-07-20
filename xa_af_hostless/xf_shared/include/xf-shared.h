/*
 * Copyright (c) 2009 Tensilica Inc.  ALL RIGHTS RESERVED.
 * These coded instructions, statements, and computer programs are the
 * copyrighted works and confidential proprietary information of
 * Tensilica Inc.  They may be adapted and modified by bona fide
 * purchasers for internal use, but neither the original nor any adapted
 * or modified version may be disclosed or distributed to third parties
 * in any manner, medium, or form, in whole or in part, without the prior
 * written consent of Tensilica Inc.
 */

#ifndef __XF_SHARED__
#define __XF_SHARED__

#include "xaf-api.h"

#ifndef XF_SHMEM_SIZE
#define AUDIO_FRMWK_BUF_SIZE_MAX    (256<<13)   /* ...this should match the framework buffer size of master core, can vary from test to test or can set to a maximum. */
#define XF_SHMEM_SIZE  (AUDIO_FRMWK_BUF_SIZE_MAX * (XAF_MEM_ID_DEV_MAX + 1) + ((1024<<8)*XF_CFG_CORES_NUM))
#endif

#if (XF_CFG_CORES_NUM > 1)
#include <xtensa/system/xmp_subsystem.h>
/*! Synchronization object type. The minimum size of an object of this type is
 * \ref IPC_RESET_SYNC_STRUCT_SIZE. For a cached subsystem, the size is
 * rounded and aligned to the maximum dcache line size across all cores in the
 * subsystem. */

#define IPC_RESET_SYNC_STRUCT_SIZE  (4)
struct ipc_reset_sync_struct {
  char _[((IPC_RESET_SYNC_STRUCT_SIZE+XMP_MAX_DCACHE_LINESIZE-1)/XMP_MAX_DCACHE_LINESIZE)*XMP_MAX_DCACHE_LINESIZE];
} __attribute__((aligned(XMP_MAX_DCACHE_LINESIZE)));

typedef struct ipc_reset_sync_struct ipc_reset_sync_t;

#define XF_SHMEM_SIZE_OF_IPC_INIT_SYNC          (sizeof(struct ipc_reset_sync_struct)*XF_CFG_CORES_NUM)
#define XF_SHMEM_SIZE_OF_IPC_LOCK_MSGQ          (2*XMP_MAX_DCACHE_LINESIZE)
#define XF_SHMEM_SIZE_OF_IPC_LOCK_MEM_POOL      (2*XMP_MAX_DCACHE_LINESIZE)
#define XF_CFG_SHMEM_POOLS_NUM                  1

/* ...buffer required for core synchronization at reset */
extern char shared_mem_ipc_reset_sync[XF_SHMEM_SIZE_OF_IPC_INIT_SYNC];

/* ...buffer required for IPC-Msgq locks */
extern char shared_mem_ipc_lock_msgq[XF_CFG_CORES_NUM][XF_SHMEM_SIZE_OF_IPC_LOCK_MSGQ];

/* ...buffer required for IPC memory management locks */
extern char shared_mem_ipc_lock_mem_pool[XF_CFG_SHMEM_POOLS_NUM][XF_SHMEM_SIZE_OF_IPC_LOCK_MEM_POOL];

extern unsigned _shared_sysram_uncached_data_start; /* ...'shared':Prefix from memmap.xmm; 'sysram_uncached':mem region name; 'data': .data section;  '_start':postfix generated forlinker */

extern char perf_stats[];

#endif //#if (XF_CFG_CORES_NUM > 1)

/*! Shared memory buffer for IPC.
 * For a cached subsystem, the size is
 * rounded and aligned to the maximum dcache line size across all cores in the
 * subsystem. */
extern char shared_mem[];

#endif //__XF_SHARED__
