
.PHONY:

NCORES ?= 2
ROOTDIR ?= ..

ifeq ($(NCORES), 4)
    YML_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_4c.yml
    XTSYS_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_4c.xtsys
else ifeq ($(NCORES), 3)
    YML_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_3c.yml
    XTSYS_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_3c.xtsys
else ifeq ($(NCORES), 2)
    YML_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_2c.yml
    XTSYS_FILE=$(ROOTDIR)/xtsc/xaf_xtsc_sys_2c.xtsys
endif

XTENSA_TOOLS=`xt-run --show-config=xttools`

all sysbuild:
	$(XTENSA_TOOLS)/libexec/xt-mbuild -root ./ -socfile $(XTSYS_FILE) -syspkg $(ROOTDIR)/xtsc/mbuild/package -sysbuild $(ROOTDIR)/xtsc/mbuild/build -preclean -build sw -lsps sim,sim-local
	$(XTENSA_TOOLS)/libexec/xt-sysbuilder -subsys $(YML_FILE) -swtools $(XTENSA_TOOLS) -build $(ROOTDIR)/xtsc/sysbuilder --xtensa-system $(XTENSA_SYSTEM) 

clean:
	rm -rf $(ROOTDIR)/xtsc/mbuild $(ROOTDIR)/xtsc/sysbuilder
