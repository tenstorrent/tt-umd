
CONFIG ?= develop
UMD_HOME ?= .
OUT ?= $(UMD_HOME)/build
LIBDIR ?= $(OUT)/lib
ARCH_NAME ?= grayskull
OBJDIR ?= $(OUT)/obj
BINDIR ?= $(OUT)/bin
INCDIR ?= $(OUT)/include
TESTDIR ?= $(OUT)/test
DOCSDIR ?= $(OUT)/docs
UMD_VERSIM_STUB ?= 1
SHARED_LIB_FLAGS ?= -shared -fPIC
STATIC_LIB_FLAGS ?= -fPIC
UMD_LD_LIBRARY_PATH ?= $(LIBDIR)

TEST_TARGETS ?= device/tests
ifeq ($(ARCH_NAME), wormhole_b0)
  TEST_TARGETS += device/tests/galaxy
endif
ifeq ($(EMULATION_DEVICE_EN),1)
  TEST_TARGETS = device/tests/emulation
  TENSIX_EMULATION_ZEBU    	 ?= $(TENSIX_EMULATION_ROOT)/zebu
  TENSIX_EMULATION_ZCUI_WORK ?= $(TENSIX_EMULATION_ROOT)/targets/tensix_2x2_1dram_BH/zcui.work
  TENSIX_EMULATION_RUNDIR 	 ?= $(OUT)/rundir_zebu
  UMD_LD_LIBRARY_PATH = $(LIBDIR):$(ZEBU_IP_ROOT)/lib:$(LD_LIBRARY_PATH)
endif

#MAKEFLAGS := --jobs=$(shell nproc)

DEVICE_CXX ?= /usr/bin/g++
CXXFLAGS = -MMD -I$(UMD_HOME)/. --std=c++17
ifeq ($(CONFIG), release)
CXXFLAGS += -O3 -fno-lto
else ifeq ($(CONFIG), develop)
CXXFLAGS += -O3 -fno-lto -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), ci)
CXXFLAGS += -O3 -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), assert)
CXXFLAGS += -O3 -g -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), asan)
CXXFLAGS += -O3 -g -fsanitize=address -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), ubsan)
CXXFLAGS += -O3 -g -fsanitize=undefined -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), debug)
CXXFLAGS += -O0 -g -DTT_DEBUG_LOGGING
else
$(error Unknown value for CONFIG "$(CONFIG)")
endif
UMDHEADERS := device/device_api.h


include new_device/module.mk
include tests/module.mk

init:
	echo $(LIBDIR)
	mkdir -p $(OUT)
	mkdir -p $(INCDIR)
	cp $(UMDHEADERS) $(INCDIR)
ifeq ($(EMULATION_DEVICE_EN),1)
	mkdir -p $(TENSIX_EMULATION_RUNDIR)
	cp -f $(TENSIX_EMULATION_ZEBU)/scripts/designFeatures $(TENSIX_EMULATION_RUNDIR)/designFeatures	
	ln -sf $(TENSIX_EMULATION_ZCUI_WORK) $(OUT)/.
	ln -sf ../../build $(TENSIX_EMULATION_RUNDIR)/.
endif

build: init umd_device

test: build $(TEST_TARGETS)
run: test
	LD_LIBRARY_PATH=$(UMD_LD_LIBRARY_PATH) ./$(OUT)/tests/device_unit_tests
run-galaxy: test
	LD_LIBRARY_PATH=$(UMD_LD_LIBRARY_PATH) ./$(OUT)/tests/galaxy_unit_tests

ifeq ($(EMULATION_DEVICE_EN),1)
run-emu:
	cd $(TENSIX_EMULATION_RUNDIR) && \
		LD_LIBRARY_PATH=$(UMD_LD_LIBRARY_PATH) && \
		$(OUT)/tests/emulation_unit_tests |& tee run.log
wave-conv-emu:
	cd $(TENSIX_EMULATION_RUNDIR) && \
		zSimzilla 	--zebu-work ../zcui.work/zebu.work \
					--ztdb fwc.ztdb \
					--zwd fwc.ztdb.zwd \
					--timescale 1ns --mudb --threads 10 --jobs 10 --log simzilla.log
wave-view-emu:
	cd $(TENSIX_EMULATION_RUNDIR) && \
		verdi -dbdir ../zcui.work/vcs_splitter/simv.daidir -ssf fwc.ztdb.zwd
endif

clean:
	rm -rf $(OUT)
