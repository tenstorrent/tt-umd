# Install script for directory: /home/runner/work/tt-umd/tt-umd/device

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "umd-runtime" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/runner/work/tt-umd/tt-umd/_codeql_build_dir/device/libdevice.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdevice.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "umd-runtime" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "umd-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/cluster.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/driver_atomics.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/warm_reset.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/cluster_descriptor.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_io.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/soc_descriptor.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/arc" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/arc_messenger.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/arc_telemetry_reader.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/blackhole_arc_message_queue.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/blackhole_arc_messenger.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/smbus_arc_telemetry_reader.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/wormhole_arc_messenger.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/wormhole_arc_telemetry_reader.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arc/spi_tt_device.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/arch" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arch/architecture_implementation.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arch/blackhole_implementation.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arch/wormhole_implementation.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/arch/grendel_implementation.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/chip" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip/chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip/local_chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip/mock_chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip/remote_chip.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/chip_helpers" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip_helpers/sysmem_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip_helpers/simulation_sysmem_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip_helpers/silicon_sysmem_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip_helpers/sysmem_buffer.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/chip_helpers/tlb_manager.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/coordinates" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/coordinates/blackhole_coordinate_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/coordinates/coordinate_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/coordinates/wormhole_coordinate_manager.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/firmware" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/firmware/firmware_info_provider.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/firmware/wormhole_18_3_firmware_info_provider.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/firmware/blackhole_18_7_firmware_info_provider.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/firmware/firmware_utils.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/jtag" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/jtag/jtag_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/jtag/jtag.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/logging" TYPE FILE FILES "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/logging/config.hpp")
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/pcie" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/pcie/pci_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/pcie/tlb_handle.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/pcie/tlb_window.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/simulation" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/simulation/simulation_chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/simulation/tt_sim_chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/simulation/rtl_simulation_chip.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/simulation/simulation_host.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/topology" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/topology/topology_discovery_blackhole.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/topology/topology_discovery_wormhole.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/topology/topology_discovery.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/topology/topology_utils.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/tt_device" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/blackhole_tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/wormhole_tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/remote_wormhole_tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/rtl_simulation_tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/tt_sim_tt_device.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/remote_communication.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_device/remote_communication_legacy_firmware.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/tt_kmd_lib" TYPE FILE FILES "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/tt_kmd_lib/tt_kmd_lib.h")
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/types" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/core_coordinates.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/tensix_soft_reset_options.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/noc_id.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/risc_type.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/arch.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/blackhole_arc.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/blackhole_eth.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/cluster_descriptor_types.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/cluster_types.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/communication_protocol.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/core_coordinates.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/telemetry.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/tlb.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/wormhole_dram.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/wormhole_telemetry.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/types/xy_pair.hpp"
    )
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/umd/device/utils" TYPE FILE FILES
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/lock_manager.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/kmd_versions.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/semver.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/robust_mutex.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/common.hpp"
    "/home/runner/work/tt-umd/tt-umd/device/api/umd/device/utils/timeouts.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "umd-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/umd/umdTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/umd/umdTargets.cmake"
         "/home/runner/work/tt-umd/tt-umd/_codeql_build_dir/device/CMakeFiles/Export/a66ff87e1dd248bcbf3c902600a65777/umdTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/umd/umdTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/umd/umdTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/umd" TYPE FILE FILES "/home/runner/work/tt-umd/tt-umd/_codeql_build_dir/device/CMakeFiles/Export/a66ff87e1dd248bcbf3c902600a65777/umdTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/umd" TYPE FILE FILES "/home/runner/work/tt-umd/tt-umd/_codeql_build_dir/device/CMakeFiles/Export/a66ff87e1dd248bcbf3c902600a65777/umdTargets-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/runner/work/tt-umd/tt-umd/_codeql_build_dir/device/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
