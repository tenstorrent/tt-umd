add_library(tools_common INTERFACE)
target_link_libraries(
    tools_common
    INTERFACE
        umd::Common
        umd::device
        umd::Firmware
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        tt-logger::tt-logger
        cxxopts::cxxopts
)

check_function_exists(
    backtrace
    HAVE_BACKTRACE
)

if(NOT HAVE_BACKTRACE)
    find_library(EXECINFO_LIB execinfo)
    if(EXECINFO_LIB)
        target_link_libraries(tools_common INTERFACE ${EXECINFO_LIB})
    endif()
endif()

add_executable(telemetry telemetry.cpp)
target_link_libraries(telemetry PRIVATE tools_common)
set_target_properties(
    telemetry
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            telemetry
)

add_executable(topology topology.cpp)
target_link_libraries(topology PRIVATE tools_common)
set_target_properties(
    topology
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            topology
)

add_executable(harvesting harvesting.cpp)
target_link_libraries(harvesting PRIVATE tools_common)
set_target_properties(
    harvesting
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            harvesting
)

add_executable(system_health system_health.cpp)
target_link_libraries(system_health PRIVATE tools_common)
set_target_properties(
    system_health
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/tools/umd/
        OUTPUT_NAME
            system_health
)

add_custom_target(
    umd_tools
    DEPENDS
        telemetry
        topology
        harvesting
        system_health
)
