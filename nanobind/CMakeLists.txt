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
# Interpreter is needed for stub generation
find_package(
    Python
    REQUIRED
    COMPONENTS
        Interpreter
        Development.Module
)

# Define the Python interface target
nanobind_add_module(nanobind_tt_umd MODULE py_api_module.cpp py_api_basic_types.cpp py_api_topology_discovery.cpp py_api_telemetry.cpp py_api_tt_device.cpp py_api_cluster.cpp py_api_warm_reset.cpp py_api_soc_descriptor.cpp py_api_logging.cpp)

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

option(TT_UMD_BUILD_PIP "By default build pip package" ON)
if(TT_UMD_BUILD_PIP)
    # --- PIP WHEEL LOGIC ---

    message(STATUS "Configuring Python bindings for pip wheel")

    # The problem that RPATH is solving is the following:
    # Some third party python package which uses tt_umd gets installed on the system. The artifacts of this package will end up in the same folder.
    # When the third party package tries to include tt_umd module, it will correctly find it. The module will then include the python bindings .so file
    # which is in the same folder and will be correctly found. However, the .so file itself requires libdevice.so (the umd library) file. The system tries to
    # find this in the default system lib directory, but it is not there. The RunPATH variable will say to this lib loading system that it should look for the
    # libdevice.so file in the same folder as the tt_umd module, which is where it is located.
    # 
    # During build, we need a different RPATH so stub generation can find libdevice.so
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

    # Generate __init__.py at install time if it doesn't exist yet
    # This ensures it's available even if the custom target wasn't built
    install(
        CODE
            "
        set(INIT_FILE \"${CMAKE_CURRENT_BINARY_DIR}/__init__.py\")
        if(NOT EXISTS \"\${INIT_FILE}\")
            message(STATUS \"Generating __init__.py at install time\")
            execute_process(
                COMMAND \"${Python_EXECUTABLE}\" \"${CMAKE_CURRENT_SOURCE_DIR}/generate_init.py\"
                        \"${CMAKE_CURRENT_BINARY_DIR}/tt_umd\"
                        \"\${INIT_FILE}\"
                RESULT_VARIABLE RESULT
            )
            if(NOT RESULT EQUAL 0)
                message(FATAL_ERROR \"Failed to generate __init__.py\")
            endif()
        endif()
    "
        COMPONENT pip_wheel
    )

    # Install the generated __init__.py file
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py DESTINATION tt_umd COMPONENT pip_wheel)

    # Install all stub files from tt_umd directory (includes __init__.pyi and submodule stubs)
    install(
        DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/tt_umd/
        DESTINATION tt_umd
        COMPONENT pip_wheel
        FILES_MATCHING
        PATTERN
        "*.pyi"
    )

    # Create and install py.typed marker for PEP 561 compliance
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/py.typed "")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/py.typed DESTINATION tt_umd COMPONENT pip_wheel)

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
    # Configures the build for a .deb/.rpm package.
    # RPATH is set to the system lib dir to find the 'umd-runtime' package.

    message(STATUS "Configuring Python bindings for FHS package")

    # Fedora/RHEL (lib64) rpmbuild forbids relative RUNPATH like 'lib64'.
    # Keep Ubuntu's previous behavior; clear RUNPATH only on lib64 platforms.
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

    # Decide Python install destination (robust across distros/CMake versions):
    # 1) Python-provided site-arch variables
    # 2) Fallback based on libdir heuristic (site-packages for lib64, dist-packages otherwise)
    if(DEFINED Python_SITEARCH AND NOT Python_SITEARCH STREQUAL "")
        set(TT_UMD_PY_DEST "${Python_SITEARCH}")
    elseif(DEFINED Python3_SITEARCH AND NOT Python3_SITEARCH STREQUAL "")
        set(TT_UMD_PY_DEST "${Python3_SITEARCH}")
    else()
        # Heuristic fallback:
        # - RPM/RHEL/Fedora typically use 'site-packages' and lib64 on x86_64
        # - Debian/Ubuntu typically use 'dist-packages' and lib
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

    # Install the Python module to the computed destination
    install(
        TARGETS
            nanobind_tt_umd
        LIBRARY
            DESTINATION ${TT_UMD_PY_DEST}/tt_umd
            COMPONENT umd-python
    )

    # Generate __init__.py at install time if it doesn't exist yet
    # This ensures it's available even if the custom target wasn't built
    install(
        CODE
            "
        set(INIT_FILE \"${CMAKE_CURRENT_BINARY_DIR}/__init__.py\")
        if(NOT EXISTS \"\${INIT_FILE}\")
            message(STATUS \"Generating __init__.py at install time\")
            execute_process(
                COMMAND \"${Python_EXECUTABLE}\" \"${CMAKE_CURRENT_SOURCE_DIR}/generate_init.py\"
                        \"${CMAKE_CURRENT_BINARY_DIR}/tt_umd\"
                        \"\${INIT_FILE}\"
                RESULT_VARIABLE RESULT
            )
            if(NOT RESULT EQUAL 0)
                message(FATAL_ERROR \"Failed to generate __init__.py\")
            endif()
        endif()
    "
        COMPONENT umd-python
    )

    # Install the generated __init__.py file (same as pip wheel)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py DESTINATION ${TT_UMD_PY_DEST}/tt_umd COMPONENT umd-python)

    # Install all stub files from tt_umd directory (includes __init__.pyi and submodule stubs)
    install(
        DIRECTORY
            ${CMAKE_CURRENT_BINARY_DIR}/tt_umd/
        DESTINATION ${TT_UMD_PY_DEST}/tt_umd
        COMPONENT umd-python
        FILES_MATCHING
        PATTERN
        "*.pyi"
    )

    # Create and install py.typed marker for PEP 561 compliance
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/py.typed "")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/py.typed DESTINATION ${TT_UMD_PY_DEST}/tt_umd COMPONENT umd-python)

    # We DO NOT install 'device' here.
    # device/CMakeLists.txt already installs it for 'umd-runtime'.
endif()
