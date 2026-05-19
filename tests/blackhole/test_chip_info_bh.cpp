// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

using namespace tt;
using namespace tt::umd;

TEST(BlackholeChipInfo, BasicChipInfo) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->set_power_state(true);
        tt_device->init_tt_device();

        const ChipInfo chip_info = tt_device->get_chip_info();

        EXPECT_TRUE(
            chip_info.board_type == BoardType::P100 || chip_info.board_type == BoardType::P150 ||
            chip_info.board_type == BoardType::P300 || chip_info.board_type == BoardType::UBB_BLACKHOLE);

        switch (chip_info.board_type) {
            case BoardType::P100:
            case BoardType::P150: {
                EXPECT_TRUE(chip_info.asic_location == 0);
                break;
            }
            case BoardType::P300: {
                EXPECT_TRUE(chip_info.asic_location <= 1);
                break;
            }
            case BoardType::UBB_BLACKHOLE: {
                EXPECT_TRUE(chip_info.asic_location <= 8);
                break;
            }
            default: {
                throw std::runtime_error("Unexpected board type for Blackhole.");
            }
        }

        tt_device->set_power_state(false);
    }
}
