# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEVICE_BUILDDIR = $(OUT)
UMD_DEVICE_LIB = $(LIBDIR)/libdevice.so

DEVICE_OBJDIR = $(OBJDIR)/umd
DEVICE_SRCS = \
	device/tt_device.cpp \
	device/tt_silicon_driver.cpp \
	device/tt_silicon_driver_common.cpp \
  device/tt_soc_descriptor.cpp \
  device/tt_cluster_descriptor.cpp \
  device/cpuset_lib.cpp \
  device/architecture_implementation.cpp \
  device/blackhole_implementation.cpp \
  device/grayskull_implementation.cpp \
  device/tlb.cpp \
  device/wormhole_implementation.cpp \

DEVICE_INCLUDES=      	\
  -DFMT_HEADER_ONLY     \
  -I$(UMD_HOME)/third_party/fmt/include \
  -I$(UMD_HOME)/common \
  -I$(UMD_HOME)/utils \
  -I$(UMD_HOME)/ \


ifeq ($(EMULATION_DEVICE_EN),1)
  DEVICE_SRCS +=  device/tt_emulation_device.cpp
  DEVICE_INCLUDES +=      	\
    -I$(TENSIX_EMULATION_ROOT)/zebu/include \
    -I$(ZEBU_IP_ROOT)/include \
    -I$(ZEBU_ROOT)/include
else 
  DEVICE_SRCS += device/tt_emulation_stub.cpp
endif

DEVICE_OBJS = $(addprefix $(DEVICE_OBJDIR)/, $(DEVICE_SRCS:.cpp=.o))
DEVICE_DEPS = $(addprefix $(DEVICE_OBJDIR)/, $(DEVICE_SRCS:.cpp=.d))

DEVICE_LDFLAGS = -lhwloc

ifneq ($(filter "$(ARCH_NAME)","wormhole" "wormhole_b0" "blackhole"),)
  DEVICE_CXXFLAGS += -DEN_DRAM_ALIAS
  ifeq ($(DISABLE_ISSUE_3487_FIX), 1)
    DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
  endif
else
  DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
endif


# If compile warnings were specified in top level Makefile, use them here
ifeq ($(findstring clang,$(DEVICE_CXX)),clang)
DEVICE_WARNINGS := $(filter-out -Wmaybe-uninitialized,$(WARNINGS))
DEVICE_WARNINGS += -Wsometimes-uninitialized
else
DEVICE_WARNINGS = $(WARNINGS)
endif
DEVICE_WARNING += -Werror
-include $(DEVICE_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
umd_device: $(UMD_DEVICE_LIB) 
clean_umd_device:
	rm -rf $(DEVICE_BUILDDIR)

$(UMD_DEVICE_LIB): $(DEVICE_OBJS)
	mkdir -p $(LIBDIR)
	$(DEVICE_CXX) $(OPT_LEVEL) $(DEBUG_FLAGS) $(CXXFLAGS) $(DEVICE_WARNINGS) $(DEVICE_CXXFLAGS) $(SHARED_LIB_FLAGS) -o $(UMD_DEVICE_LIB) $^ $(LDFLAGS) $(DEVICE_LDFLAGS)

$(DEVICE_OBJDIR)/device/%.o: $(UMD_HOME)/device/%.cpp
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(OPT_LEVEL) $(DEBUG_FLAGS) $(CXXFLAGS) $(DEVICE_WARNINGS) $(DEVICE_CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEVICE_INCLUDES) -c -o $@ $<
