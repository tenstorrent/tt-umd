// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "l1_address_map.h"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/grayskull_implementation.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

tt_xy_pair get_any_tensix_core(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            return blackhole::TENSIX_CORES[0];
        case tt::ARCH::WORMHOLE_B0:
            return wormhole::TENSIX_CORES[0];
        case tt::ARCH::GRAYSKULL:
            return grayskull::TENSIX_CORES[0];
        default:
            throw std::runtime_error("Invalid architecture");
    }
}

TEST(TTDeviceTest, BasicTTDeviceIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);

        tt_xy_pair tensix_core = get_any_tensix_core(tt_device->get_arch());

        tt_device->write_to_device(data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

        tt_device->read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

        ASSERT_EQ(data_write, data_read);

        data_read = std::vector<uint32_t>(data_write.size(), 0);
    }
}
