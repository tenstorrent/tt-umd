# Check if this is being built as a standalone project by running cmake from this dir
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This is a root project build from install artifacts
    cmake_minimum_required(VERSION 3.16)
    project(tt_umd_nanobind)

    message(STATUS "Building nanobind as standalone project from UMD install artifacts")

    include(${PROJECT_SOURCE_DIR}/../cmake/example_client.cmake)
endif()

# https://cmake.org/cmake/help/latest/module/FindPython.html
# https://github.com/wjakob/nanobind/blob/master/docs/building.rst
# Use Development.Module to support manylinux
# Python module library should not dynamically link libpython.so or use Development.Embed
find_package(
    Python
    REQUIRED
    COMPONENTS
        Interpreter
        Development.Module
)

# Define the Python interface target
nanobind_add_module(nanobind_tt_umd MODULE py_api_module.cpp py_api_basic_types.cpp py_api_topology_discovery.cpp py_api_telemetry.cpp py_api_tt_device.cpp py_api_cluster.cpp py_api_warm_reset.cpp py_api_soc_descriptor.cpp py_api_logging.cpp)
if(TT_UMD_BUILD_SIMULATION)
    target_compile_definitions(nanobind_tt_umd PRIVATE TT_UMD_BUILD_SIMULATION)
endif()

# Generate stub file for type checking
nanobind_add_stub(
    nanobind_tt_umd_stub
    MODULE tt_umd
    OUTPUT tt_umd.pyi
    RECURSIVE
    PYTHON_PATH ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS nanobind_tt_umd
)

# Generate __init__.py after stubs are created
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
    COMMAND
        ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_init.py ${CMAKE_CURRENT_BINARY_DIR}/tt_umd
        ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
    DEPENDS
        nanobind_tt_umd_stub
        ${CMAKE_CURRENT_SOURCE_DIR}/generate_init.py
    COMMENT "Generating __init__.py based on detected submodules"
)

# Add a custom target to ensure __init__.py is generated
# Make it part of the default build with ALL
add_custom_target(
    nanobind_tt_umd_init
    ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
        nanobind_tt_umd_stub
)

# Set properties for the Python module
set_target_properties(
    nanobind_tt_umd
    PROPERTIES
        OUTPUT_NAME
            "tt_umd" # Name of the Python module
)

# Do not run clang-tidy on the Python module (pulls third-party sources via CPM)
set_target_properties(
    nanobind_tt_umd
    PROPERTIES
        CXX_CLANG_TIDY
            ""
        C_CLANG_TIDY
            ""
)

# Also disable clang-tidy for the underlying third-party static lib if present
if(TARGET nanobind-static)
    set_target_properties(
        nanobind-static
        PROPERTIES
            CXX_CLANG_TIDY
                ""
            C_CLANG_TIDY
                ""
    )
endif()

# Link the Python interface target to libdevice and nanobind
# This is needed by BOTH build types, so it goes before the if/else.
target_link_libraries(
    nanobind_tt_umd
    PRIVATE
        umd::device
        fmt::fmt-header-only
        tt-logger::tt-logger
)

# Function to install common Python package files (__init__.py, stubs, py.typed)
function(install_python_package_files DESTINATION COMPONENT)
    # Generate __init__.py at install time if not already built
    install(
        CODE
            "
        set(INIT_FILE \"${CMAKE_CURRENT_BINARY_DIR}/__init__.py\")
        if(NOT EXISTS \"\${INIT_FILE}\")
            message(STATUS \"Generating __init__.py at install time\")
            execute_process(
                COMMAND \"${Python_EXECUTABLE}\" \"${CMAKE_CURRENT_SOURCE_DIR}/generate_init.py\"
                        \"${CMAKE_CURRENT_BINARY_DIR}/tt_umd\" \"\${INIT_FILE}\"
                RESULT_VARIABLE RESULT
            )
            if(NOT RESULT EQUAL 0)
                message(FATAL_ERROR \"Failed to generate __init__.py\")
            endif()
        endif()
    "
        COMPONENT ${COMPONENT}
    )

    # Install __init__.py, stub files, submodule .py files, and py.typed marker
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py DESTINATION ${DESTINATION} COMPONENT ${COMPONENT})
    install(
        DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/tt_umd/
        DESTINATION ${DESTINATION}
        COMPONENT ${COMPONENT}
        FILES_MATCHING
        PATTERN
        "*.pyi"
        PATTERN
        "*.py"
    )
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/py.typed "")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/py.typed DESTINATION ${DESTINATION} COMPONENT ${COMPONENT})
endfunction()

option(TT_UMD_BUILD_PIP "By default build pip package" ON)
if(TT_UMD_BUILD_PIP)
    # --- PIP WHEEL LOGIC ---
    message(STATUS "Configuring Python bindings for pip wheel")

    # Set RPATH: BUILD_RPATH for stub generation, INSTALL_RPATH for runtime
    set_target_properties(
        nanobind_tt_umd
        PROPERTIES
            BUILD_RPATH
                "$ORIGIN/../device;$ORIGIN/../lib"
            INSTALL_RPATH
                "$ORIGIN"
    )

    # Install the Python module
    install(
        TARGETS
            nanobind_tt_umd
        LIBRARY
            DESTINATION tt_umd
            COMPONENT pip_wheel
    )

    # Install Python package files
    install_python_package_files(tt_umd pip_wheel)

    # Make co-locating libdevice optional to mirror previous behavior
    if(NOT DEFINED TT_UMD_BUILD_PYTHON)
        option(TT_UMD_BUILD_PYTHON "Install libdevice next to Python module" ON)
    endif()

    # Install device library to root for Python package (co-located with the module)
    # Only when building Python bindings to avoid conflicts with main install
    # Skip for static libraries as they are linked statically into the Python module
    if(TT_UMD_BUILD_PYTHON AND NOT TT_UMD_BUILD_STATIC)
        install(FILES $<TARGET_FILE:umd::device> DESTINATION . COMPONENT pip_wheel)
    elseif(TT_UMD_BUILD_PYTHON AND TT_UMD_BUILD_STATIC)
        message(STATUS "Skipping device library install for pip wheel: device is built as static library")
    endif()
else()
    # --- FHS/CPACK LOGIC ---
    message(STATUS "Configuring Python bindings for FHS package")

    # Set RPATH for system lib directory (clear for lib64 per rpmbuild requirements)
    if(CMAKE_INSTALL_LIBDIR MATCHES "lib64")
        set_target_properties(
            nanobind_tt_umd
            PROPERTIES
                INSTALL_RPATH
                    ""
        )
    else()
        set_target_properties(
            nanobind_tt_umd
            PROPERTIES
                INSTALL_RPATH
                    "${CMAKE_INSTALL_LIBDIR}"
        )
    endif()

    # Determine Python installation path
    if(DEFINED Python_SITEARCH AND NOT Python_SITEARCH STREQUAL "")
        set(TT_UMD_PY_DEST "${Python_SITEARCH}")
    elseif(DEFINED Python3_SITEARCH AND NOT Python3_SITEARCH STREQUAL "")
        set(TT_UMD_PY_DEST "${Python3_SITEARCH}")
    else()
        if(CMAKE_INSTALL_LIBDIR MATCHES "lib64")
            set(TT_UMD_PY_DEST
                "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages"
            )
        else()
            set(TT_UMD_PY_DEST
                "${CMAKE_INSTALL_LIBDIR}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/dist-packages"
            )
        endif()
    endif()

    # Install the Python module
    install(
        TARGETS
            nanobind_tt_umd
        LIBRARY
            DESTINATION ${TT_UMD_PY_DEST}/tt_umd
            COMPONENT umd-python
    )

    # Install Python package files
    install_python_package_files(${TT_UMD_PY_DEST}/tt_umd umd-python)
endif()
