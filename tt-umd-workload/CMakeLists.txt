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
        src/chip/chip.cpp
        src/chip/local_chip.cpp
        src/chip/mock_chip.cpp
        src/chip/remote_chip.cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_sources(
        tt-umd-workload
        PRIVATE
            src/simulation/simulation_chip.cpp
            src/simulation/tt_sim_chip.cpp
            src/simulation/rtl_simulation_chip.cpp
    )
    target_compile_definitions(tt-umd-workload PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23)
    target_sources(
        tt-umd-workload
        PUBLIC
            FILE_SET api
            TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/api
            FILES
                api/tt-umd-workload/cluster.hpp
                api/tt-umd-workload/chip/chip.hpp
                api/tt-umd-workload/chip/local_chip.hpp
                api/tt-umd-workload/chip/mock_chip.hpp
                api/tt-umd-workload/chip/remote_chip.hpp
                api/tt-umd-workload/simulation/simulation_chip.hpp
                api/tt-umd-workload/simulation/tt_sim_chip.hpp
                api/tt-umd-workload/simulation/rtl_simulation_chip.hpp
    )
endif()

target_include_directories(
    tt-umd-workload
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../tt-umd/src>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../tt-umd/src/common>
)

target_link_libraries(
    tt-umd-workload
    PUBLIC
        umd::tt-umd
    PRIVATE
        fmt::fmt-header-only
        spdlog::spdlog_header_only
        tt-logger::tt-logger
        yaml-cpp::yaml-cpp
)

if(TT_UMD_BUILD_SIMULATION)
    target_compile_definitions(tt-umd-workload PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

if(TT_UMD_ENABLE_TRACY)
    target_link_libraries(tt-umd-workload PRIVATE TracyClient)
    target_compile_definitions(tt-umd-workload PUBLIC TRACY_ENABLE)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23 AND NOT TT_UMD_BUILD_STATIC)
    install(
        TARGETS
            tt-umd-workload
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT umd-dev
        LIBRARY
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT umd-runtime
        FILE_SET
        api
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT umd-dev
    )
endif()

if(TT_UMD_BUILD_STATIC)
    set_target_properties(
        tt-umd-workload
        PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY
                ${PROJECT_BINARY_DIR}/lib
    )
else()
    set_target_properties(
        tt-umd-workload
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY
                ${PROJECT_BINARY_DIR}/lib
    )
endif()
