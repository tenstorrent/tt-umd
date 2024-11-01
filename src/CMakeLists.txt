add_library(firmware INTERFACE)
add_library(${PROJECT_NAME}::Firmware ALIAS firmware)

target_include_directories(
    firmware
    INTERFACE
        $<$<STREQUAL:$ENV{ARCH_NAME},wormhole_b0>:${CMAKE_CURRENT_SOURCE_DIR}/firmware/riscv/wormhole>
        $<$<STREQUAL:$ENV{ARCH_NAME},grayskull>:${CMAKE_CURRENT_SOURCE_DIR}/firmware/riscv/grayskull>
        $<$<STREQUAL:$ENV{ARCH_NAME},blackhole>:${CMAKE_CURRENT_SOURCE_DIR}/firmware/riscv/blackhole>
)
