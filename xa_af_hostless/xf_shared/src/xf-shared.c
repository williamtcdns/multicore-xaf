/*
 * A shared data library project is required for programs which are built as
 * managed projects to run on a subsystem. All programs being linked for 
 * cores of a subsystem should have a "library dependency" on the shared 
 * data library project.
 *
 * You can add your shared data definitions here, or you can add other
 * C files to the project with your definitions.
 *
 * Even if there are no definitions in here, at least one C file is required
 * in this project so it will link correctly. The link of the shared project
 * automatically brings in definitions needed by shared reset code.
 */
 
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

#include "xf-shared.h"

/* Shared data. Each proc gets a unique location that is cache line wide, to
 * avoid false sharing */

char shared_mem[XF_SHMEM_SIZE] __attribute__ ((aligned (XMP_MAX_DCACHE_LINESIZE)));

