# Every variable in subdir must be prefixed with subdir (emulating a namespace)
UMD_DEVICE_LIB = $(LIBDIR)/libdevice.so
DEVICE_BUILDDIR = $(UMD_HOME)/build
DEVICE_OBJDIR = $(DEVICE_BUILDDIR)/obj
DEVICE_SRCS = \
	device/tt_device.cpp \
	device/tt_silicon_driver.cpp \
	device/tt_silicon_driver_common.cpp \
  device/tt_soc_descriptor.cpp \
  device/tt_cluster_descriptor.cpp \
  device/cpuset_lib.cpp \
  device/util.cpp

DEVICE_INCLUDES=      	\
  -DFMT_HEADER_ONLY     \
  -I$(UMD_HOME)/third_party/fmt/include \
  -I$(UMD_HOME)/common \
  -I$(UMD_HOME)/utils \
  -I$(UMD_HOME)/ \

ifeq ($(BACKEND_VERSIM_STUB),1)
DEVICE_SRCS += device/tt_versim_stub.cpp
else
DEVICE_SRCS += device/tt_versim_device.cpp
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

ifneq ($(BACKEND_VERSIM_STUB),1)
DEVICE_INCLUDES+=      	\
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include         \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include/vltstd  \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/yaml-cpp/include                                   \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/fc4sc/includes                                     \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tclap/include                                      \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/range-v3/include          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/hardware/tdma/tb/tile                                 \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/instructions/inc                       \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/types/inc                              \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/software/command_assembler/inc                        \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/common                                          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core                                     \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/common                              \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/common/inc                          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/monitors                            \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/checkers                            \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/tvm/inc                                               \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/usr_include                                               \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers \
  
  ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_b0_defines
  else
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)   
  endif

# Specify location of VERSIM shared libraries before linking them with DEVICE_LDFLAGS
DEVICE_LDFLAGS = -Wl,-rpath,$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB):$(BUDA_HOME)/common_lib
ifdef DEVICE_VERSIM_INSTALL_ROOT
DEVICE_LDFLAGS += -Wl,-rpath,$(DEVICE_VERSIM_INSTALL_ROOT)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB),-rpath,$(DEVICE_VERSIM_INSTALL_ROOT)/common_lib
endif
DEVICE_LDFLAGS += \
  -lpthread \
	-L$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB) \
	-lz \
	-L$(BUDA_HOME)/common_lib \
	-lz \
	-lyaml-cpp \
	-l:libboost_system.so.1.65.1 \
	-l:libboost_filesystem.so.1.65.1 \
	-l:libz.so.1 \
	-l:libglog.so.0 \
	-l:libicudata.so.60 \
	-l:libicui18n.so.60 \
	-l:libicuuc.so.60 \
	-l:libboost_thread.so.1.65.1 \
	-l:libboost_regex.so.1.65.1 \
	-l:libsqlite3.so.0 \
  -lrt \
	-latomic \
  -l:versim-core.so -l:libc_verilated_hw.so
endif

DEVICE_LDFLAGS += \
	-lyaml-cpp \
  -lhwloc

ifneq ($(filter "$(ARCH_NAME)","wormhole" "wormhole_b0"),)
  DEVICE_CXXFLAGS += -DEN_DRAM_ALIAS
  ifeq ($(DISABLE_ISSUE_3487_FIX), 1)
    DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
  endif
else
  DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
endif
# Compiling VERSIM with /usr/bin/g++ causes build issues (not compatible with boost version)
# Can set the compiler in the top level makefile. Budabackend uses default clang
DEVICE_CXX ?= /usr/bin/clang++-6.0

-include $(DEVICE_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
umd_device: $(UMD_DEVICE_LIB) 
clean_umd_device:
	rm -rf $(DEVICE_BUILDDIR)

$(UMD_DEVICE_LIB): $(DEVICE_OBJS)
	mkdir -p $(LIBDIR)
	$(DEVICE_CXX) $(CXXFLAGS) $(DEVICE_CXXFLAGS) $(SHARED_LIB_FLAGS) -o $(UMD_DEVICE_LIB) $^ $(LDFLAGS) $(DEVICE_LDFLAGS)

$(DEVICE_OBJDIR)/device/%.o: $(UMD_HOME)/device/%.cpp
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(CXXFLAGS) $(DEVICE_CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEVICE_INCLUDES) -c -o $@ $<
