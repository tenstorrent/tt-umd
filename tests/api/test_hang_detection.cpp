// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

static constexpr uint64_t WH_NOC_HANG_ADDR = 0xFFB11030;
static constexpr uint64_t BH_NOC_HANG_ADDR = 0xFFB14000;

static uint32_t read_hang_check_via_bar(TTDevice* tt_device) {
    return tt_device->bar_read32(tt_device->get_architecture_implementation()->get_read_checking_offset());
}

static uint32_t read_hang_check_via_noc(TTDevice* tt_device, const SocDescriptor& soc_desc, uint32_t noc_index = 0) {
    const auto* arch_impl = tt_device->get_architecture_implementation();
    uint32_t value = 0;

    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        tt_xy_pair arc_core = soc_desc.get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];
        uint64_t scratch6_noc_addr =
            arch_impl->get_arc_apb_noc_base_address() + arch_impl->get_arc_reset_scratch_offset() + 6 * 4;

        NocIdSwitcher noc_switcher(static_cast<NocId>(noc_index));
        tt_device->read_from_device(&value, arc_core, scratch6_noc_addr, sizeof(value));
    } else {
        tt_xy_pair pcie_core = soc_desc.get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
        uint64_t noc_node_id_addr =
            arch_impl->get_noc_reg_base(CoreType::PCIE, noc_index) + arch_impl->get_noc_node_id_offset();

        NocIdSwitcher noc_switcher(static_cast<NocId>(noc_index));
        tt_device->read_from_device(&value, pcie_core, noc_node_id_addr, sizeof(value));
    }

    return value;
}

TEST(HangDetectionTest, HangCheckRegisterReadEquivalence) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No PCI devices found.";
    }

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        SocDescriptor soc_desc(tt_device->get_arch(), tt_device->get_chip_info());

        uint32_t bar_value = read_hang_check_via_bar(tt_device.get());
        uint32_t noc_value = read_hang_check_via_noc(tt_device.get(), soc_desc, 0);

        log_info(LogUMD, "Device {}: hang-check BAR=0x{:08X}  NOC=0x{:08X}", pci_device_id, bar_value, noc_value);

        EXPECT_NE(bar_value, 0xFFFFFFFF) << "BAR read returned all ones on device " << pci_device_id;
        EXPECT_NE(noc_value, 0xFFFFFFFF) << "NOC read returned all ones on device " << pci_device_id;
        EXPECT_EQ(bar_value, noc_value) << "BAR and NOC reads differ on device " << pci_device_id;
    }
}

TEST(HangDetectionTest, TestNocHangDetection) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No PCI devices found.";
    }

    if (is_arm_platform()) {
        GTEST_SKIP() << "Skipping on ARM64 – NOC hang can lock up the system.";
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    SocDescriptor soc_desc(tt_device->get_arch(), tt_device->get_chip_info());
    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    uint32_t bar_baseline = read_hang_check_via_bar(tt_device.get());
    ASSERT_NE(bar_baseline, 0xFFFFFFFF) << "Hardware already hung before test started.";

    uint32_t noc0_baseline = read_hang_check_via_noc(tt_device.get(), soc_desc, 0);
    ASSERT_NE(noc0_baseline, 0xFFFFFFFF) << "NOC0 appears hung before test started.";

    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        log_info(
            LogUMD,
            "WH: Intentionally hanging NOCs by reading 0x{:08X} on tensix core ({}, {})",
            WH_NOC_HANG_ADDR,
            tensix_core.x,
            tensix_core.y);

        uint32_t dummy;
        tt_device->read_from_device(&dummy, tensix_core, WH_NOC_HANG_ADDR, sizeof(dummy));

        uint32_t bar_hung = read_hang_check_via_bar(tt_device.get());
        EXPECT_EQ(bar_hung, 0xFFFFFFFF) << "Expected BAR read to return all ones after hanging NOC on WH.";

        log_info(LogUMD, "WH: Performing warm reset to recover from NOC hang.");
        WarmReset::warm_reset();

    } else if (tt_device->get_arch() == tt::ARCH::BLACKHOLE) {
        tt_device->set_risc_reset_state(tensix_core, static_cast<uint32_t>(TENSIX_ASSERT_SOFT_RESET));

        log_info(
            LogUMD,
            "BH: Hanging NOC1 by reading 0x{:08X} via NOC1 on soft-reset tensix ({}, {}).",
            BH_NOC_HANG_ADDR,
            tensix_core.x,
            tensix_core.y);

        {
            NocIdSwitcher noc1(NocId::NOC1);
            uint32_t dummy;
            tt_device->read_from_device(&dummy, tensix_core, BH_NOC_HANG_ADDR, sizeof(dummy));
            std::cout << "Value: " << std::hex << dummy << "\n";
            EXPECT_EQ(dummy, 0xFFFFFFFF) << "Expected NOC read to return all ones after hanging NOC on BH.";
        }

        uint32_t noc0_value = read_hang_check_via_noc(tt_device.get(), soc_desc, static_cast<uint8_t>(NocId::NOC0));
        log_info(LogUMD, "BH: NOC0 read after NOC1 hang: 0x{:08X}", noc0_value);
        EXPECT_NE(noc0_value, 0xFFFFFFFF) << "NOC0 should still be functional after hanging NOC1 on BH.";
        EXPECT_EQ(noc0_value, noc0_baseline) << "NOC0 read should match the pre-hang baseline value.";

        WarmReset::warm_reset();
    } else {
        GTEST_SKIP() << "Unsupported architecture for NOC hang test.";
    }

    auto cluster = std::make_unique<Cluster>();
    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after warm reset.";

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    SocDescriptor soc_desc_post(tt_device->get_arch(), tt_device->get_chip_info());
    uint32_t bar_recovered = read_hang_check_via_bar(tt_device.get());
    EXPECT_NE(bar_recovered, 0xFFFFFFFF) << "BAR read still returns all ones after warm reset.";

    uint32_t noc_recovered = read_hang_check_via_noc(tt_device.get(), soc_desc_post, 0);
    EXPECT_NE(noc_recovered, 0xFFFFFFFF) << "NOC read still returns all ones after warm reset.";

    log_info(LogUMD, "Post-reset: BAR=0x{:08X}  NOC=0x{:08X}", bar_recovered, noc_recovered);
}

TEST(HangDetectionTest, DISABLED_TestDeviceHangDetection) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No PCI devices found.";
    }

    if (is_arm_platform()) {
        GTEST_SKIP() << "Skipping on ARM64 – NOC hang can lock up the system.";
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    SocDescriptor soc_desc(tt_device->get_arch(), tt_device->get_chip_info());
    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    ASSERT_FALSE(tt_device->is_hardware_hung()) << "is_hardware_hung() returned true before any hang.";

    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        log_info(LogUMD, "WH: Hanging NOC via read of 0x{:08X}.", WH_NOC_HANG_ADDR);
        uint32_t dummy;
        tt_device->read_from_device(&dummy, tensix_core, WH_NOC_HANG_ADDR, sizeof(dummy));
    } else if (tt_device->get_arch() == tt::ARCH::BLACKHOLE) {
        log_info(LogUMD, "BH: Soft-resetting tensix, then hanging NOC0 via 0x{:08X}.", BH_NOC_HANG_ADDR);
        tt_device->set_risc_reset_state(tensix_core, static_cast<uint32_t>(TENSIX_ASSERT_SOFT_RESET));

        uint32_t dummy;
        tt_device->read_from_device(&dummy, tensix_core, BH_NOC_HANG_ADDR, sizeof(dummy));
    } else {
        GTEST_SKIP() << "Unsupported architecture for device hang test.";
    }

    EXPECT_TRUE(tt_device->is_hardware_hung()) << "is_hardware_hung() did not detect the hang.";

    EXPECT_THROW(tt_device->detect_hang_read(), std::runtime_error);

    log_info(LogUMD, "Performing warm reset to recover.");
    WarmReset::warm_reset();

    auto cluster = std::make_unique<Cluster>();
    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after warm reset.";

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    EXPECT_FALSE(tt_device->is_hardware_hung()) << "is_hardware_hung() still true after warm reset.";
}
