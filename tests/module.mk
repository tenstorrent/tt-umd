# Every variable in subdir must be prefixed with subdir (emulating a namespace)

DEVICE_UNIT_TESTS_CFLAGS = $(CFLAGS) -Werror
DEVICE_UNIT_TESTS_INCLUDES = -I$(UMD_HOME) -I$(UMD_HOME)/device $(DEVICE_INCLUDES)

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/wormhole/*.c*))
  DEVICE_UNIT_TESTS_INCLUDES += -I$(UMD_HOME)/device/wormhole/ -I$(UMD_HOME)/src/firmware/riscv/wormhole -I$(UMD_HOME)/src/firmware/riscv/wormhole/wormhole_b0_defines
else
  DEVICE_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/$(ARCH_NAME)/*.c*))
  DEVICE_UNIT_TESTS_INCLUDES += -I$(UMD_HOME)/device/$(ARCH_NAME)/ -I$(UMD_HOME)/src/firmware/riscv/$(ARCH_NAME)
endif

DEVICE_UNIT_TESTS += $(basename $(wildcard $(UMD_HOME)/tests/*.c*))

DEVICE_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(DEVICE_UNIT_TESTS))

DEVICE_UNIT_TESTS_LDFLAGS = -L$(LIBDIR) -ldevice -lyaml-cpp -lhwloc -lgtest -lgtest_main -lpthread

DEVICE_UNIT_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(DEVICE_UNIT_TESTS_SRCS:.cpp=.o))
DEVICE_UNIT_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(DEVICE_UNIT_TESTS_SRCS:.cpp=.d))

-include $(DEVICE_UNIT_TESTS_DEPS)

DEVICE_CXX ?= /usr/bin/clang++-6.0
DEVICE_UNIT_TESTS_DEPS = $(UMD_DEVICE_LIB)

# Each module has a top level target as the entrypoint which must match the subdir name
device/tests: $(OUT)/tests/device_unit_tests

.PHONY: $(OUT)/tests/device_unit_tests
$(OUT)/tests/device_unit_tests: $(DEVICE_UNIT_TESTS_DEPS)
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(DEVICE_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(DEVICE_UNIT_TESTS_INCLUDES) $(DEVICE_UNIT_TESTS_SRCS) -o $@ $^ $(LDFLAGS) $(DEVICE_UNIT_TESTS_LDFLAGS)
