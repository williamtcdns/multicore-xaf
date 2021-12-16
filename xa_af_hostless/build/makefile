#
# Copyright (c) 2015-2021 Cadence Design Systems, Inc.
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

NCORES ?= 2
ROOTDIR ?= ..

ifeq ($(NCORES), 4)
    CORE_LIST= 0 1 2 3
else ifeq ($(NCORES), 3)
    CORE_LIST= 0 1 2
else ifeq ($(NCORES), 2)
    CORE_LIST= 0 1
endif

sysbuild:
	make -C $(ROOTDIR)/xtsc NCORES=$(NCORES) clean all

all:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib clean XTCORE="core$(var)" XF_CORE_ID=${var};\
	$(MAKE) -f makefile_lib $@ XTCORE="core$(var)" XF_CORE_ID=${var} XF_CFG_CORES_NUM=$(NCORES);\
	)

install:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib $@ XTCORE="core$(var)" XF_CORE_ID=${var} XF_CFG_CORES_NUM=$(NCORES);\
	)

pack_all: sysbuild all


clean:
	@$(foreach var, $(CORE_LIST),\
	$(MAKE) -f makefile_lib clean XTCORE="core$(var)" XF_CORE_ID=${var};\
	)
