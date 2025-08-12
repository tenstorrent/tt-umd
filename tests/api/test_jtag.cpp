// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "gtest/gtest.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_soc_descriptor.h"
using namespace tt::umd;

// To test JTAG functionality one must add -DENABLE_JTAG_TESTS=ON flag when setting up a build.
// Note: do not enable the flag unless JTAG device is physically connected to the card. Tests will fail.
//
TEST(ApiJTagDeviceTest, BasicJTagIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    std::vector<uint32_t> data_read(data_write.size(), 0);
    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id, true);

        ChipInfo chip_info = tt_device->get_chip_info();

        tt_SocDescriptor soc_desc(
            tt_device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::NOC0)[0];

        tt_device->jtag_write_to_device(data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

        tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::NOC0)[0];
        tt_device->jtag_read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
        ASSERT_EQ(data_write, data_read);
    }
}
