# Every variable in subdir must be prefixed with subdir (emulating a namespace)

DEVICE_UNIT_TESTS_CFLAGS = $(CFLAGS) -Werror
DEVICE_UNIT_TESTS_INCLUDES = -I$(UMD_HOME) -I$(UMD_HOME)/device $(DEVICE_INCLUDES)

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_UNIT_TESTS = $(basename $(wildcard $(UMD_HOME)/tests/wormhole/*.c*))
  DEVICE_UNIT_TESTS_INCLUDES += -I$(UMD_HOME)/device/wormhole/ -I$(UMD_HOME)/src/firmware/riscv/wormhole 
else
  DEVICE_UNIT_TESTS = $(basename $(wildcard $(UMD_HOME)/tests/$(ARCH_NAME)/*.c*))
  DEVICE_UNIT_TESTS_INCLUDES += -I$(UMD_HOME)/device/$(ARCH_NAME)/ -I$(UMD_HOME)/src/firmware/riscv/$(ARCH_NAME)
endif

COMMON_UNIT_TESTS_SRCS = $(wildcard $(UMD_HOME)/tests/test_utils/*.cpp)

DEVICE_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/*.c*))

DEVICE_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(DEVICE_UNIT_TESTS))
DEVICE_UNIT_TESTS_SRCS += $(COMMON_UNIT_TESTS_SRCS)

# Build galaxy tests separately
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  GALAXY_UNIT_TESTS = $(DEVICE_UNIT_TESTS)
  GALAXY_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/galaxy/*.c*))
  GALAXY_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(GALAXY_UNIT_TESTS))
  GALAXY_UNIT_TESTS_SRCS += $(COMMON_UNIT_TESTS_SRCS)
endif

DEVICE_UNIT_TESTS_LDFLAGS = -L$(LIBDIR) -lyaml-cpp -lhwloc -lgtest -lgtest_main -lpthread -lstdc++fs

DEVICE_UNIT_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(DEVICE_UNIT_TESTS_SRCS:.cpp=.o))
DEVICE_UNIT_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(DEVICE_UNIT_TESTS_SRCS:.cpp=.d))

#build emulation tests separately
ifeq ($(EMULATION_DEVICE_EN),1)
  EMULATION_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/emulation/*.c*))
  EMULATION_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(EMULATION_UNIT_TESTS))
  EMULATION_UNIT_TESTS_SRCS += $(COMMON_UNIT_TESTS_SRCS)

  EMULATION_LDFLAGS += -L$(ZEBU_IP_ROOT)/lib -L$(ZEBU_ROOT)/lib -LDFLAGS "-g"
  EMULATION_LIBS += -lxtor_amba_master_svs -lZebuXtor -lZebu -lZebuZEMI3 -lZebuVpi \
                    -Wl,-rpath,$(ZEBU_IP_ROOT)/lib\
                    $(OUT)/zcui.work/zebu.work/xtor_amba_master_axi4_svs.so\
                    $(OUT)/zcui.work/zebu.work/tt_emu_cmd_xtor.so\
                    $(TENSIX_EMULATION_ZEBU)/lib/libtt_zebu_wrapper.so

  DEVICE_UNIT_TESTS_LDFLAGS += $(EMULATION_LDFLAGS) $(EMULATION_LIBS)
  
endif

-include $(DEVICE_UNIT_TESTS_DEPS)

DEVICE_CXX ?= /usr/bin/clang++-6.0
DEVICE_UNIT_TESTS_DEPS = $(UMD_DEVICE_LIB)

# Each module has a top level target as the entrypoint which must match the subdir name
device/tests: $(OUT)/tests/device_unit_tests
device/tests/galaxy: $(OUT)/tests/galaxy_unit_tests
device/tests/emulation: $(OUT)/tests/emulation_unit_tests

.PHONY: $(OUT)/tests/device_unit_tests
$(OUT)/tests/device_unit_tests: $(DEVICE_UNIT_TESTS_DEPS)
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(DEVICE_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(DEVICE_UNIT_TESTS_INCLUDES) $(DEVICE_UNIT_TESTS_SRCS) -o $@ $^ $(LDFLAGS) $(DEVICE_UNIT_TESTS_LDFLAGS)

.PHONY: $(OUT)/tests/galaxy_unit_tests
$(OUT)/tests/galaxy_unit_tests: $(DEVICE_UNIT_TESTS_DEPS)
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(DEVICE_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(DEVICE_UNIT_TESTS_INCLUDES) $(GALAXY_UNIT_TESTS_SRCS) -o $@ $^ $(LDFLAGS) $(DEVICE_UNIT_TESTS_LDFLAGS)

.PHONY: $(OUT)/tests/emulation_unit_tests
$(OUT)/tests/emulation_unit_tests: $(DEVICE_UNIT_TESTS_DEPS)
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(DEVICE_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(DEVICE_UNIT_TESTS_INCLUDES) $(EMULATION_UNIT_TESTS_SRCS) -o $@ $^ $(LDFLAGS) $(DEVICE_UNIT_TESTS_LDFLAGS)

