cmake_minimum_required(VERSION 3.16)

if(TT_UMD_BUILD_STATIC)
    add_library(tt-umd-workload STATIC)
    message(STATUS "Building tt-umd-workload library as STATIC")
else()
    add_library(tt-umd-workload SHARED)
    message(STATUS "Building tt-umd-workload library as SHARED")
endif()
add_library(${PROJECT_NAME}::tt-umd-workload ALIAS tt-umd-workload)

target_sources(
    tt-umd-workload
    PRIVATE
        src/cluster.cpp
        src/chip.cpp
        src/local_chip.cpp
        src/mock_chip.cpp
        src/remote_chip.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        tt-umd-workload
        PRIVATE
            src/simulation/simulation_chip.cpp
            src/simulation/tt_sim_chip.cpp
            src/simulation/rtl_simulation_chip.cpp
    )
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd-workload
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/tt_umd_workload/cluster.hpp
                api/tt_umd_workload/chip.hpp
                api/tt_umd_workload/local_chip.hpp
                api/tt_umd_workload/mock_chip.hpp
                api/tt_umd_workload/remote_chip.hpp
                api/tt_umd_workload/simulation/simulation_chip.hpp
                api/tt_umd_workload/simulation/tt_sim_chip.hpp
                api/tt_umd_workload/simulation/rtl_simulation_chip.hpp
    )
endif()

target_include_directories(
    tt-umd-workload
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tt_umd/src>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tt_umd/src/common>
)

target_link_libraries(tt-umd-workload PUBLIC tt-umd)

if(TT_UMD_BUILD_SIMULATION)
    target_compile_definitions(tt-umd-workload PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

if(TT_UMD_ENABLE_TRACY)
    target_link_libraries(tt-umd-workload PRIVATE TracyClient)
    target_compile_definitions(tt-umd-workload PUBLIC TRACY_ENABLE)
endif()
