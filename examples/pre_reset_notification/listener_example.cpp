// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/warm_reset.hpp"

using namespace tt;
using namespace tt::umd;

bool verify_data(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, int device_id) {
    if (expected.size() != actual.size()) {
        std::cerr << "Device " << device_id << ": Size mismatch! Expected " << expected.size() << " but got "
                  << actual.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < expected.size(); i++) {
        if (expected[i] != actual[i]) {
            std::cerr << "Device " << device_id << ": Data mismatch at index " << i << "! Expected " << expected[i]
                      << " but got " << actual[i] << std::endl;
            return false;
        }
    }

    std::cout << "Device " << device_id << ": Data verification passed!" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    std::atomic<bool> read_device = true;
    // Make listener
    WarmReset::start_monitoring(
        [&read_device]() {
            std::cout << "Set read_device to false\n";
            read_device = false;
        },
        [&read_device]() {
            std::cout << "Set read_device to true\n";
            read_device = true;
        });

    // Prepare and read local devices
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    std::map<int, std::unique_ptr<TTDevice>> tt_devices;

    tt_xy_pair tensix_core;

    for (int pci_device_id : pci_device_ids) {
        tt_devices[pci_device_id] = TTDevice::create(pci_device_id);

        tt_devices[pci_device_id]->init_tt_device();

        ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

        SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

        tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    }

    while (1) {
        if (!read_device) {
            std::cout << "Not reading device" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5'000));
            continue;
        }
        for (int pci_device_id : pci_device_ids) {
            tt_devices[pci_device_id]->write_to_device(
                data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

            tt_devices[pci_device_id]->read_from_device(
                data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

            verify_data(data_write, data_read, pci_device_id);

            data_read = std::vector<uint32_t>(data_write.size(), 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5'000));
    }
    return 0;
}
