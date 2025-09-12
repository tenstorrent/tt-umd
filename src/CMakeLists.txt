add_library(firmware INTERFACE)
add_library(${PROJECT_NAME}::Firmware ALIAS firmware)

target_include_directories(
    firmware
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/firmware/riscv>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/firmware/riscv>
)
