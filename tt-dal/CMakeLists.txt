cmake_minimum_required(VERSION 3.16)

project(
    ttdal
    VERSION 0.1.0
    DESCRIPTION "Tenstorrent Device Access Library"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

set(TTDAL_SOURCES
    src/lib.c
    src/arch/wh.c
    src/arch/bh.c
)

add_library(ttdal STATIC ${TTDAL_SOURCES})

target_include_directories(ttdal
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tt-kmd
)

# Pure C library
set_target_properties(ttdal PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)

# Install library and header
install(TARGETS ttdal
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/ttdal.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(examples)
endif()

# Documentation
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
    set(DOXYGEN_PROJECT_NAME "Tenstorrent Device Access Library")
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    set(DOXYGEN_PROJECT_BRIEF "Low-level C API for Tenstorrent accelerators")

    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ttdal.h
        COMMENT "Generating API documentation with Doxygen"
    )
endif()
