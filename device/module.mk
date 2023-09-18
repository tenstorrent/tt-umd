# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEVICE_BUILDDIR = $(OUT)
UMD_DEVICE_LIB = $(LIBDIR)/libdevice.so
DEVICE_OBJDIR = $(OBJDIR)
DEVICE_SRCS = \
	device/tt_device.cpp \
	device/tt_silicon_driver.cpp \
	device/tt_silicon_driver_common.cpp \
  device/tt_soc_descriptor.cpp \
  device/tt_cluster_descriptor.cpp \
  device/cpuset_lib.cpp \
  device/util.cpp \

DEVICE_INCLUDES=      	\
  -DFMT_HEADER_ONLY     \
  -I$(UMD_HOME)/third_party/fmt/include \
  -I$(UMD_HOME)/common \
  -I$(UMD_HOME)/utils \
  -I$(UMD_HOME)/ \

ifeq ($(UMD_VERSIM_STUB),1)
  DEVICE_SRCS += device/tt_versim_stub.cpp
else
  DEVICE_SRCS += device/tt_versim_device.cpp
  ifndef UMD_VERSIM_HEADERS
    $(error VERSIM build is enabled but UMD_VERSIM_HEADERS is not defined)
  endif
  ifndef UMD_USER_ROOT
    $(error VERSIM build is enabled but UMD_USER_ROOT is not defined. This should point to firmware header files)
  endif
endif

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_SRCS += device/wormhole/impl_device.cpp
  DEVICE_INCLUDES += -I$(UMD_HOME)/device/wormhole/ -Isrc/firmware/riscv/wormhole
else
  DEVICE_SRCS += device/$(ARCH_NAME)/impl_device.cpp
  DEVICE_INCLUDES += -I$(UMD_HOME)/device/$(ARCH_NAME)/ -Isrc/firmware/riscv/$(ARCH_NAME)
endif

DEVICE_OBJS = $(addprefix $(DEVICE_OBJDIR)/, $(DEVICE_SRCS:.cpp=.o))
DEVICE_DEPS = $(addprefix $(DEVICE_OBJDIR)/, $(DEVICE_SRCS:.cpp=.d))

ifeq ($(BACKEND_VERSIM_FULL_DUMP), 1)
ifneq ("$(ARCH_NAME)", "wormhole_b0")
  $(error "FPU wave dump only available for wormhole_b0")
endif

VERSIM_LIB = fpu_waves_lib
else
VERSIM_LIB = lib
endif

DEVICE_LDFLAGS = -lhwloc

ifneq ($(UMD_VERSIM_STUB),1)
# Build Versim  based on configs specified in Buda or Metal build flow
# For this UMD_VERSIM_HEADERS and UMD_USER_ROOT must be specified 
DEVICE_INCLUDES+=      	\
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include         \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include/vltstd  \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/yaml-cpp/include                                   \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/fc4sc/includes                                     \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/tclap/include                                      \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/range-v3/include          \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/hardware/tdma/tb/tile                                 \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/instructions/inc                       \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/types/inc                              \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/software/command_assembler/inc                        \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/common                                          \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/versim-core                                     \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/versim-core/common                              \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/versim-core/common/inc                          \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/versim-core/monitors                            \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/t6ifc/versim-core/checkers                            \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/src/tvm/inc                                               \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers/usr_include                                               \
  -I$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/headers \
  
  ifeq ("$(ARCH_NAME)", "wormhole_b0")
    DEVICE_INCLUDES += -I$(UMD_USER_ROOT)/src/firmware/riscv/wormhole
    DEVICE_INCLUDES += -I$(UMD_USER_ROOT)/src/firmware/riscv/wormhole/wormhole_b0_defines
  else
    DEVICE_INCLUDES += -I$(UMD_USER_ROOT)/src/firmware/riscv/$(ARCH_NAME)   
  endif

  
  DEVICE_LDFLAGS += \
    -lpthread \
    -latomic \
    -lyaml-cpp
  # Buda links boost .so files stored in $(BUDA_HOME)/common_lib. Metal uses $(UMD_VERSIM_HEADERS)/required_libraries for linking these files. Different handling...
  ifdef BUDA_HOME
    # Specify location of VERSIM shared libraries before linking them with DEVICE_LDFLAGS
    DEVICE_LDFLAGS += -Wl,-rpath,$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/$(VERSIM_LIB):$(BUDA_HOME)/common_lib  -L$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/$(VERSIM_LIB) -L$(BUDA_HOME)/common_lib
  else
    DEVICE_LDFLAGS += -Wl,-rpath,$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/$(VERSIM_LIB):$(UMD_VERSIM_HEADERS)/required_libraries -L$(UMD_VERSIM_HEADERS)$(ARCH_NAME)/$(VERSIM_LIB) -L$(UMD_VERSIM_HEADERS)/required_libraries
  endif

  DEVICE_LDFLAGS += \
  -l:libboost_system.so.1.65.1 \
  -l:libboost_filesystem.so.1.65.1 \
  -l:libicudata.so.60 \
  -l:libicui18n.so.60 \
  -l:libicuuc.so.60 \
  -l:libboost_thread.so.1.65.1 \
  -l:libboost_regex.so.1.65.1 \
  -l:versim-core.so \
  -l:libc_verilated_hw.so

endif

ifneq ($(filter "$(ARCH_NAME)","wormhole" "wormhole_b0"),)
  DEVICE_CXXFLAGS += -DEN_DRAM_ALIAS
  ifeq ($(DISABLE_ISSUE_3487_FIX), 1)
    DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
  endif
else
  DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
endif

# Can set the compiler in the top level Makefile based on build config. Use clang by default.
ifeq ("$(HOST_ARCH)", "aarch64")
  DEVICE_CXX ?= /usr/bin/clang++
  # Cannot build Versim on ARM.
  ifneq ($(UMD_VERSIM_STUB),1)
    $(error VERSIM build is not enabled for non x86 hosts)
  endif
else
  DEVICE_CXX ?= /usr/bin/clang++-6.0
  # Compiling VERSIM with /usr/bin/g++ causes build issues (not compatible with boost version)
  # Force clang usage.
  ifneq ($(UMD_VERSIM_STUB),1)
    DEVICE_CXX = /usr/bin/clang++-6.0
  endif
endif


# If compile warnings were specified in top level Makefile, use them here
ifeq ($(findstring clang,$(DEVICE_CXX)),clang)
DEVICE_WARNINGS := $(filter-out -Wmaybe-uninitialized,$(WARNINGS))
DEVICE_WARNINGS += -Wsometimes-uninitialized
else
DEVICE_WARNINGS = $(WARNINGS)
endif

-include $(DEVICE_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
umd_device: $(UMD_DEVICE_LIB) 
clean_umd_device:
	rm -rf $(DEVICE_BUILDDIR)

$(UMD_DEVICE_LIB): $(DEVICE_OBJS)
	mkdir -p $(LIBDIR)
	$(DEVICE_CXX) $(CXXFLAGS) $(DEVICE_WARNINGS) $(DEVICE_CXXFLAGS) $(SHARED_LIB_FLAGS) -o $(UMD_DEVICE_LIB) $^ $(LDFLAGS) $(DEVICE_LDFLAGS)

$(DEVICE_OBJDIR)/device/%.o: $(UMD_HOME)/device/%.cpp
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(CXXFLAGS) $(DEVICE_WARNINGS) $(DEVICE_CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEVICE_INCLUDES) -c -o $@ $<
