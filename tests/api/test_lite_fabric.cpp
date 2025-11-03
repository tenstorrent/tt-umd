// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "blackhole/eth_l1_address_map.h"
#include "blackhole/l1_address_map.h"
#include "umd/device/cluster.hpp"
#include "umd/device/lite_fabric/lite_fabric.hpp"
#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"
#include "umd/device/types/blackhole_eth.hpp"

using namespace tt;
using namespace tt::umd;

class LiteFabricFixture : public ::testing::Test {
protected:
    // lite_fabric_chip is a chip on which lite fabric is going to be launched.
    // It is going to be used to issue fabric reads/writes to non fabric chip,
    // which should only have PCIe access to verify fabric reads and writes.
    static std::unique_ptr<LocalChip> fabric_chip;
    static std::unique_ptr<LocalChip> non_fabric_chip;
    static std::vector<CoreCoord> eth_cores_up;
    lite_fabric::HostToLiteFabricInterface<lite_fabric::SENDER_NUM_BUFFERS_ARRAY[0], lite_fabric::CHANNEL_BUFFER_SIZE>
        host_interface;

    static CoreCoord tensix_core;
    static CoreCoord eth_core_transfer;

    static void SetUpTestSuite() {
        std::vector<int> pci_devices_ids = PCIDevice::enumerate_devices();

        if (pci_devices_ids.size() < 2) {
            GTEST_SKIP() << "Skipping lite fabric tests. Lite fabric tests require at least two Blackhole devices to "
                            "be connected to the host.";
        }

        fabric_chip = LocalChip::create(pci_devices_ids[0]);

        if (fabric_chip->get_tt_device()->get_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Skipping lite fabric tests. Lite fabric tests require at least two Blackhole devices to "
                            "be connected to the host.";
        }

        auto eth_cores = fabric_chip->get_soc_descriptor().get_cores(CoreType::ETH, CoordSystem::TRANSLATED);
        eth_cores_up.clear();
        for (auto& eth_core : eth_cores) {
            uint32_t port_status;
            fabric_chip->read_from_device_reg(eth_core, &port_status, 0x7CC04, sizeof(port_status));

            if (port_status == blackhole::PORT_UP) {
                eth_cores_up.push_back(eth_core);
            }
        }

        if (eth_cores_up.empty()) {
            GTEST_SKIP()
                << "Skipping lite fabric tests. Lite fabric tests require at least one Ethernet core to be up.";
        }

        fabric_chip->set_barrier_address_params(
            {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, 0});

        non_fabric_chip = LocalChip::create(pci_devices_ids[1]);
        eth_core_transfer = eth_cores_up[0];
    }

    static void TearDownTestSuite() {
        fabric_chip.reset();
        non_fabric_chip.reset();
        eth_cores_up.clear();
    }

    void SetUp() override {
        if (should_skip_lite_fabric_tests()) {
            GTEST_SKIP() << "Skipping lite fabric tests. Lite fabric tests require at least two Blackhole devices "
                            "connected with ethernet.";
        }
        host_interface = lite_fabric::LiteFabricMemoryMap::make_host_interface(fabric_chip.get()->get_tt_device());
        lite_fabric::launch_lite_fabric(fabric_chip.get(), eth_cores_up);
        std::vector<uint8_t> zero_data(1 << 20, 0);
        non_fabric_chip->write_to_device(tensix_core, zero_data.data(), 0, zero_data.size());
    }

    void TearDown() override {
        if (fabric_chip.get() != nullptr) {
            lite_fabric::terminate_lite_fabric(fabric_chip.get(), eth_cores_up);
        }
    }

    bool should_skip_lite_fabric_tests() {
        std::vector<int> pci_devices_ids = PCIDevice::enumerate_devices();

        if (pci_devices_ids.size() < 2) {
            return true;
        }

        auto chip = LocalChip::create(pci_devices_ids[0]);

        if (chip->get_tt_device()->get_arch() != tt::ARCH::BLACKHOLE) {
            return true;
        }

        if (!fabric_chip || !non_fabric_chip) {
            return true;
        }

        return false;
    }
};

std::unique_ptr<LocalChip> LiteFabricFixture::fabric_chip = nullptr;
std::unique_ptr<LocalChip> LiteFabricFixture::non_fabric_chip = nullptr;
std::vector<CoreCoord> LiteFabricFixture::eth_cores_up = {};
CoreCoord LiteFabricFixture::tensix_core = CoreCoord(1, 2, CoreType::TENSIX, CoordSystem::TRANSLATED);
// Dummy value, it will be overriden inside SetUpTestSuite.
CoreCoord LiteFabricFixture::eth_core_transfer = CoreCoord(0, 0, CoreType::ETH, CoordSystem::TRANSLATED);

TEST_F(LiteFabricFixture, FabricReadWrite4Bytes) {
    for (int i = 0; i < 100; i++) {
        uint32_t test_value = 0xca110000 + i;
        uint32_t test_addr = 0x1000;

        host_interface.write(&test_value, sizeof(test_value), eth_core_transfer, tensix_core, test_addr);

        host_interface.barrier(eth_core_transfer);

        uint32_t fabric_readback = 0;
        host_interface.read(&fabric_readback, sizeof(fabric_readback), eth_core_transfer, tensix_core, test_addr);
        EXPECT_EQ(fabric_readback, test_value);
    }
}

TEST_F(LiteFabricFixture, FabricWriteMMIORead4Bytes) {
    for (int i = 0; i < 100; i++) {
        uint32_t test_value = 0xca11abcd + i;
        uint32_t test_addr = 0x1000;

        host_interface.write(&test_value, sizeof(test_value), eth_core_transfer, tensix_core, test_addr);

        host_interface.barrier(eth_core_transfer);

        uint32_t readback = 0;
        non_fabric_chip->read_from_device(tensix_core, &readback, test_addr, sizeof(readback));
        EXPECT_EQ(readback, test_value);
    }
}

TEST_F(LiteFabricFixture, FabricReadMMIOWrite4Bytes) {
    uint32_t test_value = 0xca11abcd;
    uint32_t test_addr = 0x1000;

    non_fabric_chip->write_to_device(tensix_core, &test_value, test_addr, sizeof(test_value));

    non_fabric_chip->l1_membar({tensix_core});

    uint32_t readback_mmio = 0;
    non_fabric_chip->read_from_device(tensix_core, &readback_mmio, test_addr, sizeof(test_value));
    EXPECT_EQ(test_value, readback_mmio);

    uint32_t readback_fabric = 0;
    host_interface.read(&readback_fabric, sizeof(readback_fabric), eth_core_transfer, tensix_core, test_addr);
    EXPECT_EQ(readback_fabric, test_value);
}

TEST_F(LiteFabricFixture, FabricReadWrite1MB) {
    for (int i = 0; i < 100; i++) {
        uint32_t test_addr = 0x100;

        std::vector<uint8_t> write_data(1 << 13, i + 2);

        host_interface.write(write_data.data(), write_data.size(), eth_core_transfer, tensix_core, test_addr);

        host_interface.barrier(eth_core_transfer);

        std::vector<uint8_t> readback_data(1 << 13, 0);
        host_interface.read(readback_data.data(), readback_data.size(), eth_core_transfer, tensix_core, test_addr);
        EXPECT_EQ(write_data, readback_data);
    }
}

TEST_F(LiteFabricFixture, FabricWrite1MBMMIORead1MB) {
    for (int i = 0; i < 100; i++) {
        uint32_t test_addr = 0x100;

        std::vector<uint8_t> write_data(1 << 20, i + 4);

        host_interface.write(write_data.data(), write_data.size(), eth_core_transfer, tensix_core, test_addr);

        host_interface.barrier(eth_core_transfer);

        std::vector<uint8_t> readback_data(1 << 20, 0);
        non_fabric_chip->read_from_device(tensix_core, readback_data.data(), test_addr, readback_data.size());
        EXPECT_EQ(write_data, readback_data);
    }
}

TEST_F(LiteFabricFixture, FabricARC) {
    // This is an address of ARC status register, the value of it should be set by ARC fw and
    // should be 5, it was chosen for potential easier debug in the future.
    uint32_t test_addr = 0x80030408;

    CoreCoord target_arc_core = CoreCoord(8, 0, CoreType::ARC, CoordSystem::TRANSLATED);

    for (int i = 0; i < 100; i++) {
        uint32_t arc_boot_status_fabric = 1;
        host_interface.read(&arc_boot_status_fabric, sizeof(uint32_t), eth_core_transfer, target_arc_core, test_addr);

        uint32_t arc_boot_status_check = 0;
        non_fabric_chip->read_from_device(
            target_arc_core, &arc_boot_status_check, test_addr, sizeof(arc_boot_status_check));

        EXPECT_EQ(arc_boot_status_fabric, arc_boot_status_check);
    }
}
