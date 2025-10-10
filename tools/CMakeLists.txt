# Check if this is being built as a standalone project by running cmake from this dir
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This is a root project build from install artifacts
    cmake_minimum_required(VERSION 3.16)
    project(tt_umd_tools)

    message(STATUS "Building tools as standalone project from UMD install artifacts")

    include(${PROJECT_SOURCE_DIR}/../cmake/example_client.cmake)
endif()

add_library(tools_common INTERFACE)
target_link_libraries(
    tools_common
    INTERFACE
        umd::device
        tt-logger::tt-logger
        cxxopts::cxxopts
)

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
