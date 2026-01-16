// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>

#include <iostream>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;

int main(int argc, char* argv[]) {
    std::vector<int> pci_devices = PCIDevice::enumerate_devices();
    if (pci_devices.empty()) {
        std::cerr << "No devices found" << std::endl;
        return 1;
    }

    std::cout << "Found " << pci_devices.size() << " device(s)" << std::endl;

    for (int device_id : pci_devices) {
        std::cout << "\n=== Device " << device_id << " (Before Initialization) ===" << std::endl;

        std::unique_ptr<TTDevice> device = TTDevice::create(device_id);

        std::cout << "Architecture: "
                  << (device->get_arch() == tt::ARCH::WORMHOLE_B0 ? "Wormhole B0"
                      : device->get_arch() == tt::ARCH::BLACKHOLE ? "Blackhole"
                                                                  : "Unknown")
                  << std::endl;

        std::cout << "PCI Device: " << device->get_pci_device()->get_device_num() << std::endl;

        std::cout << "Testing BAR read/write (without init)..." << std::endl;
        uint32_t test_addr = device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
                             device->get_architecture_implementation()->get_arc_reset_scratch_offset();
        uint32_t original_value = device->bar_read32(test_addr);
        std::cout << "Original value at 0x" << std::hex << test_addr << ": 0x" << original_value << std::dec
                  << std::endl;

        std::cout << "Testing device memory operations (without init)..." << std::endl;
        uint32_t test_data = 0x12345678;
        uint32_t read_data = 0;
        auto test_core = tt_xy_pair(1, 1);
        uint64_t mem_addr = 0x0;

        device->write_to_device(&test_data, test_core, mem_addr, sizeof(test_data));
        device->read_from_device(&read_data, test_core, mem_addr, sizeof(read_data));

        std::cout << "Device memory operation: wrote 0x" << std::hex << test_data << ", read 0x" << read_data
                  << std::dec << std::endl;

        std::cout << "\n=== Now calling init_tt_device() ===" << std::endl;
        device->init_tt_device();

        std::cout << "Clock: " << device->get_clock() << " MHz" << std::endl;
        auto board_id = device->get_board_id();
        std::cout << "Board ID: " << (board_id.has_value() ? fmt::format("0x{:x}", board_id.value()) : "N/A")
                  << std::endl;
        auto temp = device->get_asic_temperature();
        std::cout << "Temperature: " << (temp.has_value() ? fmt::format("{:.2f}°C", temp.value()) : "N/A") << std::endl;

        std::cout << "ArcMessenger available: " << (device->get_arc_messenger() ? "Yes" : "No") << std::endl;
        std::cout << "ArcTelemetryReader available: " << (device->get_arc_telemetry_reader() ? "Yes" : "No")
                  << std::endl;

        ChipInfo chip_info = device->get_chip_info();
        SocDescriptor soc_desc(device->get_arch(), chip_info);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
        if (tensix_cores.empty()) {
            std::cout << "No Tensix cores available" << std::endl;
            continue;
        }

        CoreCoord tensix_core = tensix_cores[0];
        std::cout << tensix_core.str() << std::endl;

        uint32_t init_test_data = 0x87654321;
        uint32_t init_read_data = 0;
        uint64_t init_mem_addr = 0x0;

        device->write_to_device(&init_test_data, tensix_core, init_mem_addr, sizeof(init_test_data));
        device->read_from_device(&init_read_data, tensix_core, init_mem_addr, sizeof(init_read_data));

        std::cout << "Post-init memory operation: wrote 0x" << std::hex << init_test_data << ", read 0x"
                  << init_read_data << std::dec << std::endl;
    }

    std::cout << "\nDemo complete" << std::endl;
    return 0;
}
