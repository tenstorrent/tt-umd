include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Configure the Config file
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

# Install the Config and ConfigVersion files
install(
    FILES
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    COMPONENT umd-dev
)

set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_PACKAGE_VENDOR "Tenstorrent, Inc.")
set(CPACK_PACKAGE_NAME "tt_umd")

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tenstorrent User Mode Driver")
set(CPACK_PACKAGE_CONTACT "support@tenstorrent.com")

# Enable per-component packages for DEB/RPM and explicit one-per-component grouping
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
# Do not use grouping; generate packages per component
set(CPACK_COMPONENTS_GROUPING IGNORE)

# Turn component dependencies into package dependencies
set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)
set(CPACK_RPM_ENABLE_COMPONENT_DEPENDS ON)

# Explicit component list, include Python only for FHS (non-pip) builds
set(_UMD_COMPONENTS
    umd-runtime
    umd-dev
)
if(TT_UMD_BUILD_PYTHON AND NOT TT_UMD_BUILD_PIP)
    list(APPEND _UMD_COMPONENTS umd-python)
endif()
set(CPACK_COMPONENTS_ALL "${_UMD_COMPONENTS}")

# Stable per-component package names for DEB/RPM generators
set(CPACK_DEBIAN_UMD_RUNTIME_PACKAGE_NAME "umd-runtime")
set(CPACK_DEBIAN_UMD_DEV_PACKAGE_NAME "umd-dev")
set(CPACK_DEBIAN_UMD_PYTHON_PACKAGE_NAME "umd-python")

set(CPACK_RPM_UMD_RUNTIME_PACKAGE_NAME "umd-runtime")
set(CPACK_RPM_UMD_DEV_PACKAGE_NAME "umd-dev")
set(CPACK_RPM_UMD_PYTHON_PACKAGE_NAME "umd-python")

# Debian-specific defaults
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "support@tenstorrent.com")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# RPM-specific defaults
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RPM_PACKAGE_GROUP "Development/Libraries")

# 1. The runtime library package (libdevice.so)
cpack_add_component(
    umd-runtime
    DISPLAY_NAME "Tenstorrent UMD Runtime"
    DESCRIPTION "Runtime library for Tenstorrent devices"
)

# 2. The development package (headers, cmake files)
cpack_add_component(
    umd-dev
    DISPLAY_NAME "Tenstorrent UMD Development"
    DESCRIPTION "Headers and CMake files for UMD"
    DEPENDS
        umd-runtime # Makes the -dev package depend on the runtime
)

# 3. The new Python package
cpack_add_component(
    umd-python
    DISPLAY_NAME "Python FHS Bindings"
    DESCRIPTION "Python bindings for Tenstorrent UMD"
    DEPENDS
        umd-runtime # Makes the python package depend on the runtime
)

include(CPack)
