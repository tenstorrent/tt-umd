# Check if this is being built as a standalone project by running cmake from this dir
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This is a root project build from install artifacts
    cmake_minimum_required(VERSION 3.16)
    project(tt_umd_simulation)

    message(STATUS "Building simulation as standalone project from UMD install artifacts")

    include(${PROJECT_SOURCE_DIR}/../cmake/example_client.cmake)
endif()

add_library(simulation_common INTERFACE)
target_link_libraries(
    simulation_common
    INTERFACE
        umd::device
        tt-logger::tt-logger
)

target_include_directories(simulation_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_executable(child_process_tt_sim_chip ../device/simulation/child_process_tt_sim_chip.cpp)
target_link_libraries(
    child_process_tt_sim_chip
    PRIVATE
        simulation_common
        device
)
set_target_properties(
    child_process_tt_sim_chip
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/simulation/umd/
        OUTPUT_NAME
            child_process_tt_sim_chip
)

add_custom_target(umd_simulation DEPENDS child_process_tt_sim_chip)
