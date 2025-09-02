// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/l1_address_map.h"
#include "umd/device/cluster.h"
#include "umd/device/lite_fabric/lite_fabric.hpp"
#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"
#include "umd/device/types/blackhole_eth.hpp"

using namespace tt::umd;

class LiteFabricFixture : public ::testing::Test {
protected:
    // lite_fabric_chip a chip on which lite fabric is going to be launched.
    // It is going to be used to issue fabric reads/writes to non fabric chip,
    // which should only have PCIe access to verify fabric reads and writes.
    static std::unique_ptr<LocalChip> fabric_chip;
    static std::unique_ptr<LocalChip> non_fabric_chip;
    static std::vector<CoreCoord> eth_cores_up;
    lite_fabric::HostToLiteFabricInterface<lite_fabric::SENDER_NUM_BUFFERS_ARRAY[0], lite_fabric::CHANNEL_BUFFER_SIZE>
        host_interface;

    static tt_xy_pair target_tensix;
    static CoreCoord tensix_core;
    static CoreCoord eth_core_transfer;

    bool running_first_test = true;

    static void SetUpTestSuite() {
        tensix_core = CoreCoord(target_tensix.x, target_tensix.y, CoreType::TENSIX, CoordSystem::TRANSLATED);

        std::vector<int> pci_devices_ids = PCIDevice::enumerate_devices();

        fabric_chip = LocalChip::create(pci_devices_ids[0]);

        auto eth_cores = fabric_chip->get_soc_descriptor().get_cores(CoreType::ETH);
        eth_cores_up.clear();
        for (auto& eth_core : eth_cores) {
            uint32_t port_status;
            fabric_chip->read_from_device_reg(eth_core, &port_status, 0x7CC04, sizeof(port_status));

            if (port_status == blackhole::PORT_UP) {
                eth_cores_up.push_back(eth_core);
            }
        }

        fabric_chip->set_barrier_address_params(
            {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 0});

        non_fabric_chip = LocalChip::create(pci_devices_ids[1]);
        eth_core_transfer = eth_cores_up[0];
    }

    void SetUp() override {
        host_interface = lite_fabric::LiteFabricMemoryMap::make_host_interface();
        lite_fabric::set_chip(fabric_chip.get());
        lite_fabric::launch_lite_fabric(fabric_chip.get(), eth_cores_up);
        std::vector<uint8_t> zero_data(1 << 20, 0);
        non_fabric_chip->write_to_device(tensix_core, zero_data.data(), 0, zero_data.size());
    }

    void TearDown() override { lite_fabric::terminate_lite_fabric(fabric_chip.get(), eth_cores_up); }
};

std::unique_ptr<LocalChip> LiteFabricFixture::fabric_chip = nullptr;
std::unique_ptr<LocalChip> LiteFabricFixture::non_fabric_chip = nullptr;
std::vector<CoreCoord> LiteFabricFixture::eth_cores_up = {};
tt_xy_pair LiteFabricFixture::target_tensix = {1, 2};
CoreCoord LiteFabricFixture::tensix_core = CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::TRANSLATED);
// Dummy value, it will be overriden inside SetUpTestSuite.
CoreCoord LiteFabricFixture::eth_core_transfer = CoreCoord(0, 0, CoreType::ETH, CoordSystem::TRANSLATED);

TEST_F(LiteFabricFixture, FabricReadWrite4Bytes) {
    uint32_t test_value = 0xdeadbeef;
    uint32_t test_addr = 0x1000;

    host_interface.write(&test_value, sizeof(test_value), eth_core_transfer, target_tensix, test_addr);

    uint32_t fabric_readback = 0;
    host_interface.read(&fabric_readback, sizeof(fabric_readback), eth_core_transfer, target_tensix, test_addr);
    EXPECT_EQ(fabric_readback, test_value);
}

TEST_F(LiteFabricFixture, FabricWriteMMIORead4Bytes) {
    uint32_t test_value = 0xdeadbeef;
    uint32_t test_addr = 0x1000;

    host_interface.write(&test_value, sizeof(test_value), eth_core_transfer, target_tensix, test_addr);

    uint32_t readback = 0;
    non_fabric_chip->read_from_device(tensix_core, &readback, test_addr, sizeof(readback));
    EXPECT_EQ(readback, test_value);
}

TEST_F(LiteFabricFixture, FabricReadWrite1MB) {
    uint32_t test_addr = 0;

    std::vector<uint8_t> write_data(1 << 20, 2);

    host_interface.write(write_data.data(), write_data.size(), eth_core_transfer, target_tensix, test_addr);

    std::vector<uint8_t> readback_data(1 << 20, 0);
    host_interface.read(readback_data.data(), readback_data.size(), eth_core_transfer, target_tensix, test_addr);
    EXPECT_EQ(write_data, readback_data);
}

TEST_F(LiteFabricFixture, FabricWrite1MBMMIORead1MB) {
    uint32_t test_addr = 0;

    std::vector<uint8_t> write_data(1 << 20, 3);

    host_interface.write(write_data.data(), write_data.size(), eth_core_transfer, target_tensix, test_addr);

    std::vector<uint8_t> readback_data(1 << 20, 0);
    non_fabric_chip->read_from_device(tensix_core, readback_data.data(), test_addr, readback_data.size());
    EXPECT_EQ(write_data, readback_data);
}
