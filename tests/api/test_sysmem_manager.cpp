// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "umd/device/chip_helpers/sysmem_manager.h"

using namespace tt::umd;

const uint32_t HUGEPAGE_REGION_SIZE = 1 << 30;  // 1GB

TEST(ApiSysmemManager, BasicIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids[0]);

    std::unique_ptr<SysmemManager> sysmem = std::make_unique<SysmemManager>(tt_device.get());

    // Initializes system memory with one channel.
    sysmem->init_hugepage(1);

    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    sysmem->write_to_sysmem(0, data_write.data(), 0, data_write.size() * sizeof(uint32_t));

    EXPECT_THROW(
        sysmem->write_to_sysmem(1, data_write.data(), 0, data_write.size() * sizeof(uint32_t)), std::runtime_error);
    EXPECT_THROW(
        sysmem->write_to_sysmem(0, data_write.data(), HUGEPAGE_REGION_SIZE + 1, data_write.size() * sizeof(uint32_t)),
        std::runtime_error);
}
