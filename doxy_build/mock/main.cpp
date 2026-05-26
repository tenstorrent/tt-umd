// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>

#include "mock_device_model.hpp"

using namespace tt::umd;

int main() {
    printf("=== PCIe Mock Device ===\n");
    auto device = create_mock_device(IODeviceType::PCIE, 0);
    device->init_device();

    // --- System Memory + Zero-Copy DMA ---
    printf("\n--- System Memory + Zero-Copy DMA ---\n");

    MockSystemMemoryAllocator sysmem;
    auto buffer = sysmem.allocate_buffer(4096);
    printf("Allocated %zu bytes at IOVA 0x%lx\n", buffer->get_size(), buffer->get_iova());

    // Write to host buffer via SystemMemoryBuffer API.
    uint32_t write_data[2] = {0xDEADBEEF, 0xCAFEBABE};
    buffer->write_to_sysmem(write_data, sizeof(write_data), 0);

    CoreCoord core(1, 1);

    // Zero-copy DMA: host buffer -> device (uses IOVA directly).
    device->dma_write_zero_copy(buffer->get_iova(), 0x1000, sizeof(write_data), core);

    // Read back from device via regular path.
    uint32_t readback[2] = {};
    device->read_data(readback, core, 0x1000, sizeof(readback));
    printf("Write: 0x%08X 0x%08X\n", write_data[0], write_data[1]);
    printf(
        "Read:  0x%08X 0x%08X  %s\n",
        readback[0],
        readback[1],
        (readback[0] == 0xDEADBEEF && readback[1] == 0xCAFEBABE) ? "PASS" : "FAIL");

    // Zero-copy read: device -> host buffer.
    auto read_buf = sysmem.allocate_buffer(sizeof(uint32_t));
    device->dma_read_zero_copy(read_buf->get_iova(), 0x1000, sizeof(uint32_t), core);
    uint32_t zc_result = 0;
    read_buf->read_from_sysmem(&zc_result, sizeof(zc_result), 0);
    printf("Zero-copy readback: 0x%08X  %s\n", zc_result, zc_result == 0xDEADBEEF ? "PASS" : "FAIL");

    // Map user buffer with NOC binding.
    uint32_t user_val = 0x1234;
    auto mapped = sysmem.map_user_buffer(&user_val, sizeof(user_val), true);
    printf("Mapped user buf IOVA: 0x%lx, NOC addr: 0x%lx\n", mapped->get_iova(), mapped->get_noc_address().value());

    // --- Regular I/O Round-Trip ---
    printf("\n--- Regular I/O ---\n");
    uint32_t val = 0x42;
    CoreCoord core2(2, 3);
    device->write_data(&val, core2, 0x2000, sizeof(val));
    uint32_t rval = 0;
    device->read_data(&rval, core2, 0x2000, sizeof(rval));
    printf("Write 0x%X to (2,3)@0x2000, read back 0x%X  %s\n", val, rval, rval == val ? "PASS" : "FAIL");

    // --- Device Info ---
    printf("\n--- Device Info ---\n");
    printf("Arch:       BLACKHOLE (enum=%d)\n", (int)device->get_arch());
    printf("Board ID:   0x%lx\n", device->get_board_id());
    printf("Board type: %d\n", (int)device->get_board_type());
    printf("ASIC loc:   %d\n", device->get_asic_location());
    printf("Device ID:  %d\n", device->get_communication_device_id());
    printf("IO type:    %d (PCIE)\n", (int)device->get_communication_device_type());
    printf("Remote:     %s\n", device->is_remote() ? "yes" : "no");

    // --- Clock & Thermal ---
    printf("\n--- Clock & Thermal ---\n");
    printf("Temperature: %.1f C\n", device->get_asic_temperature());
    printf("Clock:       %u MHz\n", device->get_clock_freq());
    printf("Max clock:   %u MHz\n", device->get_max_clock_freq());
    printf("Min clock:   %u MHz\n", device->get_min_clock_freq());
    printf("NUMA node:   %d\n", device->get_numa_node());

    // --- Firmware Telemetry ---
    printf("\n--- Firmware Telemetry ---\n");
    auto *telemetry = device->get_firmware_telemetry_reader();
    for (uint8_t tag = 0; tag < 4; ++tag) {
        if (telemetry->is_entry_available(tag)) {
            printf("Tag %d: %u\n", tag, telemetry->read_entry(tag));
        } else {
            printf("Tag %d: not available\n", tag);
        }
    }

    // --- Firmware Info ---
    printf("\n--- Firmware Info ---\n");
    auto ver = device->get_firmware_version();
    printf("FW version: %u.%u.%u\n", ver.major_, ver.minor_, ver.patch_);
    printf("NOC translation: %s\n", device->get_noc_translation_enabled() ? "enabled" : "disabled");

    // --- Hang Detection ---
    printf("\n--- Hang Detection ---\n");
    bool hung = device->is_bus_hung(0x12345678, TTDevice::HangAction::RETURN);
    printf("Normal data (0x12345678): %s\n", hung ? "HUNG" : "ok");
    hung = device->is_bus_hung(HANG_READ_VALUE, TTDevice::HangAction::RETURN);
    printf("Suspect data (0xFFFFFFFF): %s (BAR confirms healthy)\n", hung ? "HUNG" : "ok");

    // --- RISC Reset ---
    printf("\n--- RISC Reset ---\n");
    device->assert_risc_reset(core, RiscType::ALL);
    auto state = device->get_risc_reset_state(core);
    printf("After assert:   state=%d\n", (int)(uint64_t)state);
    device->deassert_risc_reset(core, RiscType::ALL);
    state = device->get_risc_reset_state(core);
    printf("After deassert: state=%d\n", (int)(uint64_t)state);

    // === JTAG Device ===
    printf("\n=== JTAG Mock Device ===\n");
    auto jtag = create_mock_device(IODeviceType::JTAG, 1);
    jtag->init_device();

    printf("Arch:      BLACKHOLE (enum=%d)\n", (int)jtag->get_arch());
    printf("Device ID: %d\n", jtag->get_communication_device_id());
    printf("IO type:   %d (JTAG)\n", (int)jtag->get_communication_device_type());
    printf("PCIe:      %s\n", jtag->get_pcie_interface() ? "available" : "not available (expected)");

    // Regular I/O still works on JTAG.
    uint32_t jval = 0x99;
    jtag->write_data(&jval, core, 0x3000, sizeof(jval));
    uint32_t jread = 0;
    jtag->read_data(&jread, core, 0x3000, sizeof(jread));
    printf("I/O:       write 0x%X -> read 0x%X  %s\n", jval, jread, jread == jval ? "PASS" : "FAIL");

    printf("\nAll done.\n");
    return 0;
}
