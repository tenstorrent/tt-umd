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
find_package(Python REQUIRED COMPONENTS Development.Module)

# Define the Python interface target
nanobind_add_module(nanobind_tt_umd MODULE py_api_module.cpp py_api_basic_types.cpp py_api_topology_discovery.cpp py_api_telemetry.cpp py_api_tt_device.cpp py_api_cluster.cpp py_api_warm_reset.cpp py_api_soc_descriptor.cpp)

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
    set_target_properties(
        nanobind_tt_umd
        PROPERTIES
            BUILD_WITH_INSTALL_RPATH
                true
            INSTALL_RPATH
                "$ORIGIN"
    )

    # Install the Python module
    install(
        TARGETS
            nanobind_tt_umd
        LIBRARY
            DESTINATION .
            COMPONENT pip_wheel
    )

    # Make co-locating libdevice optional to mirror previous behavior
    if(NOT DEFINED TT_UMD_BUILD_PYTHON)
        option(TT_UMD_BUILD_PYTHON "Install libdevice next to Python module" ON)
    endif()

    # Install device library to root for Python package (co-located with the module)
    # Only when building Python bindings to avoid conflicts with main install
    if(TT_UMD_BUILD_PYTHON)
        install(FILES $<TARGET_FILE:umd::device> DESTINATION . COMPONENT pip_wheel)
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
            DESTINATION ${TT_UMD_PY_DEST}
            COMPONENT umd-python
    )

    # We DO NOT install 'device' here.
    # device/CMakeLists.txt already installs it for 'umd-runtime'.
endif()
