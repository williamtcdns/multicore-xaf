#
# Copyright (c) 2015-2024 Cadence Design Systems, Inc.
# 
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
# 
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#


.PHONY:

CORE?="hifi4"
NCORES ?= 1
CACHE_BYPASS?=0
ROOTDIR ?= ..
CACHE_ENABLED=1

ifeq ($(NCORES), 4)
    CORE_LIST= 0 1 2 3
else ifeq ($(NCORES), 3)
    CORE_LIST= 0 1 2
else ifeq ($(NCORES), 2)
    CORE_LIST= 0 1
else
    CORE_LIST= 0
endif

ifneq ($(NCORES), 1)
ifeq ($(CACHE_BYPASS), 1)
CACHE_ENABLED=0
endif
endif

sysbuild:
	$(MAKE) -C $(ROOTDIR)/xtsc NCORES=$(NCORES) clean all


ifeq ($(NCORES), 1)
XTCORE="core0"
all all-dec af_hostless:
	$(MAKE) -f makefile_lib clean XTCORE=$(XTCORE)
	$(MAKE) -f makefile_lib $@ XF_LOCAL_IPC_NON_COHERENT=$(CACHE_ENABLED) XF_CFG_CORES_NUM=$(NCORES) XTCORE=$(XTCORE)

install:
	$(MAKE) -f makefile_lib $@ XF_LOCAL_IPC_NON_COHERENT=$(CACHE_ENABLED) XF_CFG_CORES_NUM=$(NCORES) XTCORE=$(XTCORE)

clean:
	$(MAKE) -f makefile_lib clean XTCORE=$(XTCORE)

else

all af_hostless all-dec:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib clean XTCORE="core$(var)";\
	$(MAKE) -f makefile_lib $@ XTCORE="core$(var)" XF_LOCAL_IPC_NON_COHERENT=$(CACHE_ENABLED) XF_CFG_CORES_NUM=$(NCORES);\
	)

install:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib $@ XTCORE="core$(var)" XF_LOCAL_IPC_NON_COHERENT=$(CACHE_ENABLED) XF_CFG_CORES_NUM=$(NCORES);\
	)

clean:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib clean XTCORE="core$(var)";\
	)

endif

pack_all: sysbuild all

